#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "pes/fpk.hpp"
#include "pes/skl.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"

namespace {

// The rig is built from the user's own PES install; without one there is
// nothing to test against.
pes::PesVfs& SharedVfs() {
  static pes::PesVfs vfs;
  return vfs;
}

const pesrig::Rig* SharedRig() {
  static std::optional<pesrig::Rig> rig;
  static bool tried = false;
  if (!tried) {
    tried = true;
    const char* root = std::getenv("PES21_ROOT");
    if (!root) return nullptr;
    pes::PesVfs& vfs = SharedVfs();
    std::string err;
    if (!vfs.Mount(root, &err)) ADD_FAILURE() << err;
    rig = pesrig::BuildRig(vfs, &err);
    if (!rig) ADD_FAILURE() << err;
  }
  return rig ? &*rig : nullptr;
}

#define REQUIRE_RIG()                                                \
  const pesrig::Rig* rig = SharedRig();                              \
  if (!rig) GTEST_SKIP() << "PES21_ROOT not set: no PES install to read"

const pesrig::RigBone& Bone(const pesrig::Rig& rig, const std::string& gf) {
  for (const auto& b : rig.bones)
    if (b.gfName == gf) return b;
  throw std::runtime_error("no bone " + gf);
}

// body_anim_skel.ask stores floats; its values survive the round trip exactly.
constexpr float ASK_TOLERANCE = 1e-6f;
// A W that should be identity may carry float noise from the .skl matrices.
// The arm chain in body_anim_skel.ask drops a few micrometres per bone
// (authored float noise); "dead horizontal" means level within 0.1 mm.
constexpr float TPOSE_LEVEL_TOLERANCE_M = 1e-4f;
constexpr float IDENTITY_W_TOLERANCE = 1e-4f;
constexpr int RIG_JOINTS = 58;  // 20 body + 38 finger joints

}  // namespace

TEST(PesRig, JointCountAndHierarchy) {
  REQUIRE_RIG();
  ASSERT_EQ(rig->bones.size(), size_t(RIG_JOINTS));
  EXPECT_EQ(rig->bones[0].gfName, "body");
  EXPECT_EQ(rig->bones[0].parent, -1);
  for (size_t i = 1; i < rig->bones.size(); ++i) EXPECT_LT(rig->bones[i].parent, int(i)) << rig->bones[i].gfName;
}

// The mover sits at sk_root_hip's height; hip's local equals GF's tracked
// player.object after the Fox->GF change of basis.
TEST(PesRig, AnimBindFromAsk) {
  REQUIRE_RIG();
  const auto& body = Bone(*rig, "body");
  EXPECT_NEAR(body.animGlobalPos.x, 0.0f, ASK_TOLERANCE);
  EXPECT_NEAR(body.animGlobalPos.y, 1.096120f, ASK_TOLERANCE);
  EXPECT_NEAR(body.animGlobalPos.z, 0.0f, ASK_TOLERANCE);
  const auto& hip = Bone(*rig, "hip");
  EXPECT_NEAR(hip.animLocalPos.x, 0.0f, ASK_TOLERANCE);
  EXPECT_NEAR(hip.animLocalPos.y, -0.104039f, ASK_TOLERANCE);
  EXPECT_NEAR(hip.animLocalPos.z, -0.018350f, ASK_TOLERANCE);
}

// T-pose: both hands straight out along +-X at shoulder height.
TEST(PesRig, AnimPoseIsTPose) {
  REQUIRE_RIG();
  for (const char* side : {"left", "right"}) {
    const auto& shoulder = Bone(*rig, std::string(side) + "_shoulder");
    const auto& hand = Bone(*rig, std::string(side) + "_hand");
    EXPECT_NEAR(hand.animGlobalPos.y, shoulder.animGlobalPos.y, TPOSE_LEVEL_TOLERANCE_M) << side;
  }
}

TEST(PesRig, AlignmentRotations) {
  REQUIRE_RIG();
  for (const auto& b : rig->bones) {
    const auto& q = b.align;
    EXPECT_NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0f, 1e-5f) << b.gfName;
  }
  for (const char* gf : {"body", "middle", "chest", "neck", "left_clavicle", "right_clavicle"}) {
    const auto& q = Bone(*rig, gf).align;
    EXPECT_NEAR(std::fabs(q.w), 1.0f, IDENTITY_W_TOLERANCE) << gf;
  }
  // Arms swing roughly 45 degrees between the render and anim poses.
  const auto& arm = Bone(*rig, "left_shoulder").align;
  EXPECT_LT(std::fabs(arm.w), std::cos(0.5 * 30.0 * M_PI / 180.0)) << "upper arm W should be a large rotation";
}

// Fingers ride the hand: every finger keeps W(hand) and sits beyond the wrist.
TEST(PesRig, FingersFollowHand) {
  REQUIRE_RIG();
  const auto& hand = Bone(*rig, "left_hand");
  const auto& tip = Bone(*rig, "left_middle_dip");
  EXPECT_FLOAT_EQ(tip.align.w, hand.align.w);
  EXPECT_GT(tip.animGlobalPos.x, hand.animGlobalPos.x);
}

TEST(PesRig, ResolveBone) {
  REQUIRE_RIG();
  auto id = [&](const char* name) { return pesrig::ResolveBone(*rig, name, nullptr); };
  EXPECT_EQ(rig->bones[*id("sk_thigh_l")].gfName, "left_thigh");
  EXPECT_EQ(rig->bones[*id("dsk_forearm_r")].gfName, "right_elbow");
  EXPECT_EQ(rig->bones[*id("dsk_sleeve_a_l")].gfName, "left_shoulder");
  EXPECT_EQ(rig->bones[*id("skf_mouth")].gfName, "head");
  EXPECT_FALSE(id("unknown_bone").has_value());
  const pes::Vec3 nearHead{0, 1.75f, 0.05f};
  EXPECT_EQ(rig->bones[*pesrig::ResolveBone(*rig, "unknown_bone", &nearHead)].gfName, "head");
}

namespace {
std::vector<pes::SklBone> PackSkeleton(const std::string& fpkPath, const std::string& member) {
  std::string err;
  auto blob = SharedVfs().Read(fpkPath, &err);
  EXPECT_TRUE(blob) << err;
  if (!blob) return {};
  auto fpk = pes::FpkArchive::Parse(*blob, &err);
  for (const auto& e : fpk->entries)
    if (e.path.ends_with(member)) return *pes::ParseSkl(std::span(*blob).subspan(e.offset, e.size), &err);
  ADD_FAILURE() << "no " << member << " in " << fpkPath;
  return {};
}
}  // namespace

// A part pack's own .skl says which of PES's two skeletons it was authored on:
// boots on the anim T-pose (feet straight down), face packs on the render pose.
TEST(PesRig, PartSkeletonNamesThePoseItWasAuthoredOn) {
  REQUIRE_RIG();
  EXPECT_EQ(pesrig::ClassifyPartPose(*rig, PackSkeleton("Asset/model/character/boots/k0062/#Win/boots.fpk", "boots.skl")),
            pesrig::PartPose::Anim);
  EXPECT_EQ(pesrig::ClassifyPartPose(*rig, PackSkeleton("Asset/model/character/#Win/common_package.fpk", "/body.skl")),
            pesrig::PartPose::Render);
  EXPECT_EQ(pesrig::ClassifyPartPose(*rig, PackSkeleton("Asset/model/character/#Win/common_package.fpk", "/common/face.skl")),
            pesrig::PartPose::Render);
}

// Every boots pack ships a boots.skl, and each one is authored on one of the two
// PES skeletons, never neither. Both occur: most on the render pose, some
// (k0062 among them) on the anim pose, so the pose must be read per pack.
TEST(PesRig, EveryBootsPackIsAuthoredOnOneOfTheTwoSkeletons) {
  REQUIRE_RIG();
  int render = 0, anim = 0;
  for (const std::string& path : SharedVfs().List("Asset/model/character/boots/"))
    if (path.ends_with("/#Win/boots.fpk")) {
      const pesrig::PartPose pose = pesrig::ClassifyPartPose(*rig, PackSkeleton(path, "boots.skl"));
      EXPECT_NE(pose, pesrig::PartPose::Unknown) << path;
      (pose == pesrig::PartPose::Render ? render : anim) += pose != pesrig::PartPose::Unknown;
    }
  EXPECT_GT(render, 0);
  EXPECT_GT(anim, 0);
  std::printf("boots packs: %d render pose, %d anim pose\n", render, anim);
}

TEST(PesRig, PartSkeletonOnNeitherPoseIsUnknown) {
  REQUIRE_RIG();
  pes::SklBone foot;
  foot.name = "sk_foot_l";
  foot.matrix = {1, 0, 0, 0.5f, 0, 1, 0, 0.5f, 0, 0, 1, 0.5f};  // nowhere near either ankle
  EXPECT_EQ(pesrig::ClassifyPartPose(*rig, {foot}), pesrig::PartPose::Unknown);
}

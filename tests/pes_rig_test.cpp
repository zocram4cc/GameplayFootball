#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <cstring>
#include <unordered_map>

#include "pes/ask.hpp"
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

// Every boots pack ships a boots.skl, and each one is authored on one of the
// two PES skeletons, never neither. Which of the two depends on the pack: the
// anim pose is the default (PES's own common boots.skl is on it, matching the
// .ask to 2e-7 m), and this install's download packs also ship render-pose
// ones. So the anim branch is asserted everywhere; the render branch only
// where such a pack exists, and reported when it does.
TEST(PesRig, EveryBootsPackIsAuthoredOnOneOfTheTwoSkeletons) {
  REQUIRE_RIG();
  int render = 0, anim = 0, unknown = 0;
  std::string neither;
  for (const std::string& path : SharedVfs().List("Asset/model/character/boots/"))
    if (path.ends_with("/#Win/boots.fpk")) {
      const pesrig::PartPose pose = pesrig::ClassifyPartPose(*rig, PackSkeleton(path, "boots.skl"));
      if (pose == pesrig::PartPose::Unknown) neither = neither.empty() ? path : neither + ", " + path;
      (pose == pesrig::PartPose::Render ? render : pose == pesrig::PartPose::Anim ? anim : unknown) += 1;
    }
  EXPECT_EQ(unknown, 0) << "a boots pack sits on neither PES skeleton: " << neither;
  EXPECT_GT(anim, 0) << "no boots pack authored on the anim skeleton";
  std::printf("boots packs: %d render pose, %d anim pose, %d on neither\n", render, anim, unknown);
}

// PES's own boots skeleton, the one a boots pack with no record falls back to
// (docs/re/pes-kit-assembly.md 8.3: BootsList default), sits on the anim pose.
TEST(PesRig, PessOwnBootsSkeletonIsOnTheAnimPose) {
  REQUIRE_RIG();
  EXPECT_EQ(pesrig::ClassifyPartPose(*rig, PackSkeleton("Asset/model/character/#Win/common_package.fpk", "/common/boots.skl")),
            pesrig::PartPose::Anim);
}

// A pack on the render pose is a real case, not a curiosity: this install's
// download packs ship some. The assertion is conditional so a vanilla install
// without them still passes.
TEST(PesRig, RenderPoseBootsPacksAreRecognisedWhenPresent) {
  REQUIRE_RIG();
  bool sawRender = false;
  for (const std::string& path : SharedVfs().List("Asset/model/character/boots/"))
    if (path.ends_with("/#Win/boots.fpk")) {
      if (pesrig::ClassifyPartPose(*rig, PackSkeleton(path, "boots.skl")) == pesrig::PartPose::Render)
        sawRender = true;
    }
  if (!sawRender) GTEST_SKIP() << "no render-pose boots pack in this install";
  std::printf("render-pose boots packs are authored on body.skl's pose\n");
}

TEST(PesRig, PartSkeletonOnNeitherPoseIsUnknown) {
  REQUIRE_RIG();
  pes::SklBone foot;
  foot.name = "sk_foot_l";
  foot.matrix = {1, 0, 0, 0.5f, 0, 1, 0, 0.5f, 0, 0, 1, 0.5f};  // nowhere near either ankle
  EXPECT_EQ(pesrig::ClassifyPartPose(*rig, {foot}), pesrig::PartPose::Unknown);
}

// hand_anim_skel.ask is version 2: a 0x14-byte header (u32 2, u32 count, 12
// zero bytes), 28-byte records {i32 nameOffset (self-relative), i32 parent,
// f32 tx,ty,tz, 0, 0}: positions only, world-aligned frames.
TEST(Ask, ReadsVersionTwoPositionsOnly) {
  std::vector<uint8_t> b(0x14 + 2 * 28, 0);
  auto put = [&](size_t at, auto v) { std::memcpy(&b[at], &v, sizeof v); };
  put(0, uint32_t(2));
  put(4, uint32_t(2));
  const size_t names = b.size();
  const std::string n0 = "skh_hand_l", n1 = "skh_index_mata_l";
  b.insert(b.end(), n0.begin(), n0.end()), b.push_back(0);
  b.insert(b.end(), n1.begin(), n1.end()), b.push_back(0);
  put(0x14, int32_t(names - 0x14));
  put(0x14 + 4, int32_t(-1));
  put(0x14 + 28, int32_t(names + n0.size() + 1 - (0x14 + 28)));
  put(0x14 + 28 + 4, int32_t(0));
  put(0x14 + 28 + 8, 0.0519f);
  put(0x14 + 28 + 12, 0.0096f);
  put(0x14 + 28 + 16, -0.0096f);
  std::string err;
  auto bones = pes::ParseAsk(b, &err);
  ASSERT_TRUE(bones) << err;
  ASSERT_EQ(bones->size(), 2u);
  EXPECT_EQ((*bones)[1].name, "skh_index_mata_l");
  EXPECT_EQ((*bones)[1].parent, 0);
  EXPECT_FLOAT_EQ((*bones)[1].local.x, 0.0519f);
  EXPECT_FLOAT_EQ((*bones)[1].local.z, -0.0096f);
  EXPECT_FLOAT_EQ((*bones)[1].rotation.w, 1.0f);
}

namespace {
// Hand-local globals of Hand/CharacterAssets/hand_anim_skel.ask (left hand).
std::unordered_map<std::string, pes::Vec3> HandAnimOffsets() {
  std::string err;
  auto blob = SharedVfs().Read("common/anime/FoxAnim/Hand/CharacterAssets/hand_anim_skel.ask", &err);
  EXPECT_TRUE(blob) << err;
  auto bones = pes::ParseAsk(*blob, &err);
  EXPECT_TRUE(bones) << err;
  std::unordered_map<std::string, pes::Vec3> global;
  for (const auto& b : *bones) {
    pes::Vec3 g = b.local;
    if (b.parent >= 0) {
      const auto& p = global.at((*bones)[b.parent].name);
      g = {g.x + p.x, g.y + p.y, g.z + p.z};
    }
    global[b.name] = g;
  }
  return global;
}
}  // namespace

// The fingers' anim pose is PES's own hand animation skeleton, the one the 162
// hand ganis are authored on (mirrored in x for the right hand), not a
// derivation: hand_l.skl's render fingers map onto it by one rotation with
// 0.004 mm residual.
TEST(PesRig, FingersSitOnTheHandAnimationSkeleton) {
  REQUIRE_RIG();
  constexpr float HAND_SKELETON_TOLERANCE_M = 1e-4f;
  const auto offsets = HandAnimOffsets();
  for (const char* side : {"left", "right"}) {
    const auto& hand = Bone(*rig, std::string(side) + "_hand");
    for (const auto& b : rig->bones) {
      if (b.pesBone.rfind("skh_", 0) != 0 || b.pesBone.back() != side[0]) continue;
      pes::Vec3 want = offsets.at(b.pesBone.substr(0, b.pesBone.size() - 1) + "l");
      if (side[0] == 'r') want.x = -want.x;
      EXPECT_NEAR(b.animGlobalPos.x - hand.animGlobalPos.x, want.x, HAND_SKELETON_TOLERANCE_M) << b.pesBone;
      EXPECT_NEAR(b.animGlobalPos.y - hand.animGlobalPos.y, want.y, HAND_SKELETON_TOLERANCE_M) << b.pesBone;
      EXPECT_NEAR(b.animGlobalPos.z - hand.animGlobalPos.z, want.z, HAND_SKELETON_TOLERANCE_M) << b.pesBone;
    }
  }
}

// The skin carries each render-pose finger by W(hand); it must land on the
// finger's anim joint, or the hand mesh tears from its bones.
TEST(PesRig, HandAlignmentCarriesRenderFingersOntoTheirJoints) {
  REQUIRE_RIG();
  constexpr float FINGER_LANDING_TOLERANCE_M = 1e-4f;
  for (const char* side : {"left", "right"}) {
    const auto& hand = Bone(*rig, std::string(side) + "_hand");
    const auto& q = hand.align;
    auto rot = [&](float x, float y, float z) {  // q * v * q^-1
      const float tx = 2 * (q.y * z - q.z * y), ty = 2 * (q.z * x - q.x * z), tz = 2 * (q.x * y - q.y * x);
      return pes::Vec3{x + q.w * tx + (q.y * tz - q.z * ty), y + q.w * ty + (q.z * tx - q.x * tz),
                       z + q.w * tz + (q.x * ty - q.y * tx)};
    };
    for (const auto& b : rig->bones) {
      if (b.pesBone.rfind("skh_", 0) != 0 || b.pesBone.back() != side[0]) continue;
      const pes::Vec3 r = rot(b.renderGlobalPos.x - hand.renderGlobalPos.x, b.renderGlobalPos.y - hand.renderGlobalPos.y,
                              b.renderGlobalPos.z - hand.renderGlobalPos.z);
      EXPECT_NEAR(r.x, b.animGlobalPos.x - hand.animGlobalPos.x, FINGER_LANDING_TOLERANCE_M) << b.pesBone;
      EXPECT_NEAR(r.y, b.animGlobalPos.y - hand.animGlobalPos.y, FINGER_LANDING_TOLERANCE_M) << b.pesBone;
      EXPECT_NEAR(r.z, b.animGlobalPos.z - hand.animGlobalPos.z, FINGER_LANDING_TOLERANCE_M) << b.pesBone;
    }
  }
}

// PES body clips converted for the engine (onthepitch/pesclips.hpp): a
// synthetic clip checks the verbatim basis map, the 10 ms resampling and the
// player line; one install test converts every imported clip and loads it.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>

#include "onthepitch/pesclips.hpp"
#include "rig/clip_pose.hpp"
#include "rig/rig_names.hpp"
#include "onthepitch/player/humanoid/animcollection.hpp"
#include "utils/animation.hpp"

using namespace blunted;

namespace {

pesrig::Rig SyntheticRig() {
  pesrig::Rig rig;
  const auto& names = pesrig::RigNodeNames();
  for (int j = 0; j < pesrig::BODY_NODE_COUNT; ++j) {
    pesrig::RigBone b;
    b.gfName = names[j].gfName;
    b.pesBone = names[j].pesBone;
    b.parent = j == 0 ? -1 : 0;
    rig.bones.push_back(b);
  }
  rig.bones[0].animGlobalPos = {0.0f, 1.0f, 0.0f};  // body bind, Fox Y up
  return rig;
}

pes::GaniSegment QuatTrack(pes::Quat a, pes::Quat b, uint32_t frames) {
  pes::GaniSegment s;
  s.type = pes::GANI_QUAT;
  s.frames = {0, frames};
  s.quats = {a, b};
  return s;
}

pes::GaniSegment VecTrack(pes::Vec4 a, pes::Vec4 b, uint32_t frames) {
  pes::GaniSegment s;
  s.type = pes::GANI_VECTOR3;
  s.frames = {0, frames};
  s.vecs = {a, b};
  return s;
}

TEST(PesClips, ConvertsVerbatimOntoTheEngineBasis) {
  const pesrig::Rig rig = SyntheticRig();
  pes::PesClip clip;
  clip.name = "synthetic";
  clip.motionNo = 7;
  const uint32_t frames = 3;  // 0.1 s at 1/30 s
  clip.gani.frameCount = frames;
  clip.gani.units.resize(11);
  // RIG_ROOT: no rotation, moves +Z (Fox forward) by 16384 raw = 1 m.
  clip.gani.units[pesrig::PES_ROOT_UNIT].segments = {
      QuatTrack({}, {}, frames), VecTrack({0, 0, 0, 0}, {0, 0, 16384, 0}, frames)};
  clip.gani.units[pesrig::PES_MOTION_UNIT].segments = {QuatTrack({}, {}, frames),
                                                      VecTrack({}, {}, frames)};
  // sk_neck (unit 9 seg 0) turned 90 deg about Fox +Y (up).
  const float h = std::sqrt(0.5f);
  clip.gani.units[9].segments = {QuatTrack({0, h, 0, h}, {0, h, 0, h}, frames)};

  Animation anim;
  anim.LoadText(clip.name, pes::PesClipAnimText(clip, rig));
  // 0.1 s at 10 ms: frames 0..10.
  EXPECT_EQ(anim.GetFrameCount(), 11);
  Quaternion q;
  Vector3 pos;
  ASSERT_TRUE(anim.GetKeyFrame("neck", 5, q, pos, true, false));
  // (x, y, z, w) -> (x, -z, y, w): Fox up becomes engine up (+Z).
  EXPECT_NEAR(q.elements[0], 0.0f, 1e-5f);
  EXPECT_NEAR(q.elements[1], 0.0f, 1e-5f);
  EXPECT_NEAR(q.elements[2], h, 1e-5f);
  EXPECT_NEAR(q.elements[3], h, 1e-5f);
  // The root's +Z (Fox forward) becomes engine -Y, starting at the origin.
  ASSERT_TRUE(anim.GetKeyFrame("player", 10, q, pos, false, true));
  EXPECT_NEAR(pos.coords[0], 0.0f, 1e-4f);
  EXPECT_NEAR(pos.coords[1], -1.0f, 1e-4f);
  EXPECT_NEAR(pos.coords[2], 0.0f, 1e-4f);
  EXPECT_EQ(anim.GetAnimType(), "special");
  EXPECT_EQ(anim.GetVariable("specialvar1"), std::to_string(int(pes::PesClipFamily::kDodge)));
  EXPECT_EQ(anim.GetVariable("specialvar2"), "7");
}

TEST(PesClipsInstall, EveryDodgeClipConvertsAndLoads) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto rig = pesrig::BuildRig(vfs, &err);
  ASSERT_TRUE(rig.has_value()) << err;
  auto clips = pes::LoadPesClips(*exe, vfs, &err);
  ASSERT_TRUE(clips.has_value()) << err;
  EXPECT_GT(clips->clips.size(), 1u);
  // Every clip of the dodge family: the ball dodges (dodge_*), the human
  // dodges (avoidjump_/avoidslide_) and the goal-net dodges (goalnet_*).
  for (const auto& clip : clips->clips) {
    // The dodge family, and the tackle family stage C added: every clip of the
    // set belongs to one of them, and each has its own conversion test below.
    const bool dodgeFamily =
        clip.name.rfind("dodge_", 0) == 0 || clip.name.rfind("avoid", 0) == 0 ||
        clip.name.rfind("goalnet_", 0) == 0 || clip.name.rfind("rapid", 0) == 0;
    const bool tackleFamily = clip.family == pes::PesClipFamily::kTackle;
    EXPECT_TRUE(dodgeFamily || tackleFamily) << clip.name;
    if (!dodgeFamily) continue;  // the tackle family has its own test below
    Animation anim;
    anim.LoadText(clip.name, pes::PesClipAnimText(clip, *rig));
    const pesrig::ClipSampler sampler(clip.gani, *rig);
    EXPECT_NEAR(anim.GetFrameCount() - 1, sampler.LengthSeconds() / 0.01, 1.0) << clip.name;
    Quaternion q;
    Vector3 pos;
    ASSERT_TRUE(anim.GetKeyFrame("player", 0, q, pos, false, true)) << clip.name;
  }
}

TEST(PesClipsInstall, EveryTackleClipConvertsAndLoads) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto rig = pesrig::BuildRig(vfs, &err);
  ASSERT_TRUE(rig.has_value()) << err;
  auto clips = pes::LoadPesClips(*exe, vfs, &err);
  ASSERT_TRUE(clips.has_value()) << err;

  // Whole families, not a sample: the tackle family is what a standing tackle
  // plays, so every one of its clips has to convert or the sub-kind's pick
  // would name a clip the engine cannot load.
  const char* tacklePrefixes[] = {"tacklefoot_", "tacklehold_", "tacklepulled_", "tackleshoulder_",
                                  "block090_", "js_idle_0_0_000_y00_tacklefoot_passcut"};
  int tackled = 0;
  for (const auto& clip : clips->clips) {
    if (clip.family != pes::PesClipFamily::kTackle) continue;
    ++tackled;
    Animation anim;
    anim.LoadText(clip.name, pes::PesClipAnimText(clip, *rig));
    const pesrig::ClipSampler sampler(clip.gani, *rig);
    EXPECT_NEAR(anim.GetFrameCount() - 1, sampler.LengthSeconds() / 0.01, 1.0) << clip.name;
    Quaternion q;
    Vector3 pos;
    ASSERT_TRUE(anim.GetKeyFrame("player", 0, q, pos, false, true)) << clip.name;
  }
  // The install's tackle family is 86 clips; fewer means a prefix was dropped.
  EXPECT_GE(tackled, 86);
}

TEST(PesClipsInstall, ASpecialQueryNamesExactlyOneTackleClip) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto rig = pesrig::BuildRig(vfs, &err);
  ASSERT_TRUE(rig.has_value()) << err;
  auto clips = pes::LoadPesClips(*exe, vfs, &err);
  ASSERT_TRUE(clips.has_value()) << err;
  AnimCollection anims(*rig);
  anims.AddPesClips(*clips);
  // A named tackle clip resolves to exactly itself and its mirror.
  const pes::PesClip* wanted = nullptr;
  for (const auto& clip : clips->clips)
    if (clip.name == "tacklefoot_mid_0_0_000") wanted = &clip;
  ASSERT_NE(wanted, nullptr);
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = e_FunctionType_Special;
  query.properties.Set("specialvar1", int(pes::PesClipFamily::kTackle));
  query.properties.Set("specialvar2", wanted->motionNo);
  DataSet found;
  anims.CrudeSelection(found, query);
  ASSERT_EQ(found.size(), 2u);  // as authored and mirrored
  for (int id : found) EXPECT_EQ(anims.GetAnim(id)->GetName().rfind(wanted->name, 0), 0u);
}

}  // namespace

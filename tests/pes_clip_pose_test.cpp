#include <gtest/gtest.h>

#include <cmath>

#include <cstdlib>
#include <string>

#include "pes/clips.hpp"
#include "pes/gani.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"
#include "rig/clip_pose.hpp"
#include "rig/rig_names.hpp"

namespace {

constexpr float BIND_BODY_Y_M = 1.096120f;  // sk_root_hip height in body_anim_skel.ask
constexpr float RAW_PER_METRE = 16384.0f;   // docs/re/pes-re-animselect.md 1: 163.8 raw/cm
constexpr float EPS = 1e-5f;

// A rig with the real joint names and order; only the body's bind height matters here.
pesrig::Rig TestRig() {
  pesrig::Rig rig;
  std::vector<std::string> names;
  for (const auto& n : pesrig::RigNodeNames()) {
    pesrig::RigBone b;
    b.gfName = n.gfName;
    b.pesBone = n.pesBone;
    int parent = -1;
    for (size_t i = 0; i < names.size(); ++i)
      if (names[i] == n.gfParent) parent = int(i);
    b.parent = parent;
    rig.bones.push_back(b);
    names.push_back(n.gfName);
  }
  rig.bones[0].animGlobalPos = {0, BIND_BODY_Y_M, 0};
  return rig;
}

pes::Quat AboutY(float rad) { return {0, std::sin(rad / 2), 0, std::cos(rad / 2)}; }

pes::GaniSegment QuatTrack(std::vector<uint32_t> frames, std::vector<pes::Quat> keys) {
  pes::GaniSegment s;
  s.type = pes::GANI_QUAT;
  s.frames = std::move(frames);
  s.quats = std::move(keys);
  return s;
}

pes::GaniSegment VecTrack(std::vector<uint32_t> frames, std::vector<pes::Vec4> keys) {
  pes::GaniSegment s;
  s.type = pes::GANI_VECTOR3;
  s.frames = std::move(frames);
  s.vecs = std::move(keys);
  return s;
}

// Units 0 (RIG_ROOT: quat + position), 1 (mover: quat + position), 2 (dsk_hip quat).
pes::Gani Clip() {
  pes::Gani g;
  g.frameCount = 10;
  g.units.resize(3);
  g.units[0].segments = {QuatTrack({0, 10}, {pes::Quat{}, pes::Quat{}}),
                         VecTrack({0, 10}, {pes::Vec4{}, pes::Vec4{RAW_PER_METRE, 0, 0, 0}})};
  g.units[1].segments = {QuatTrack({0, 10}, {pes::Quat{}, pes::Quat{}}),
                         VecTrack({0, 10}, {pes::Vec4{}, pes::Vec4{0, RAW_PER_METRE / 10, 0, 0}})};
  g.units[2].segments = {QuatTrack({0, 10}, {pes::Quat{}, AboutY(float(M_PI) / 2)})};
  return g;
}

int Joint(const pesrig::Rig& rig, const char* gf) {
  for (size_t i = 0; i < rig.bones.size(); ++i)
    if (rig.bones[i].gfName == gf) return int(i);
  return -1;
}

}  // namespace

TEST(ClipPose, MappedTrackDrivesItsJointWithNlerp) {
  const auto rig = TestRig();
  const pesrig::ClipSampler sampler(Clip(), rig);
  const auto mid = sampler.Sample(5.0);
  const pes::Quat expected = AboutY(float(M_PI) / 4);  // nlerp halfway between 0 and 90 deg
  const pes::Quat& hip = mid.local[Joint(rig, "hip")];
  EXPECT_NEAR(hip.y, expected.y, EPS);
  EXPECT_NEAR(hip.w, expected.w, EPS);
}

TEST(ClipPose, UnmappedJointsStayAtRest) {
  const auto rig = TestRig();
  const auto pose = pesrig::ClipSampler(Clip(), rig).Sample(10.0);
  for (const char* gf : {"chest", "left_hand", "left_index_mcp"}) {
    EXPECT_NEAR(pose.local[Joint(rig, gf)].w, 1.0f, EPS) << gf;
  }
}

// body = RIG_ROOT o mover; positions are raw / 16384 metres, the mover's added
// to its bind height.
TEST(ClipPose, BodyComposesRootAndMover) {
  const auto rig = TestRig();
  const auto end = pesrig::ClipSampler(Clip(), rig).Sample(10.0);
  EXPECT_NEAR(end.bodyPosition.x, 1.0f, EPS);
  EXPECT_NEAR(end.bodyPosition.y, BIND_BODY_Y_M + 0.1f, EPS);
  EXPECT_NEAR(end.bodyPosition.z, 0.0f, EPS);
  EXPECT_NEAR(end.bodyRotation.w, 1.0f, EPS);
}

TEST(ClipPose, ClampsOutsideTheClip) {
  const auto rig = TestRig();
  const pesrig::ClipSampler sampler(Clip(), rig);
  EXPECT_NEAR(sampler.Sample(-3.0).bodyPosition.x, 0.0f, EPS);
  EXPECT_NEAR(sampler.Sample(99.0).bodyPosition.x, 1.0f, EPS);
  EXPECT_DOUBLE_EQ(sampler.LengthSeconds(), 10 * pesrig::PES_FRAME_S);
}

// A clip must play at the speed PES plays it. A live player in a pure run_3_3
// blend (playback 0.998) moved at 3.88 m/s (docs/re/pes-re-animselect.md 1);
// the clip's own root over its body, sampled in seconds, must match.
TEST(ClipPoseInstall, Run33MovesAtTheSpeedSeenInTheGame) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  constexpr double LIVE_RUN_3_3_SPEED_MPS = 3.88;
  // Live speed was averaged over 4.5 s of play, with per-player root scale
  // 0.97-1.08 (same report): within 10 %.
  constexpr double SPEED_TOLERANCE_MPS = 0.39;
  constexpr double SPAN_S = 0.3;  // inside one cycle at either frame rate
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto rig = pesrig::BuildRig(vfs, &err);
  ASSERT_TRUE(rig) << err;
  auto blob = pes::FindBodyClip(vfs, pes::ClipId("run_3_3"), &err);
  ASSERT_TRUE(blob) << err;
  auto gani = pes::ParseGani(*blob, &err);
  ASSERT_TRUE(gani) << err;
  pesrig::ClipSampler sampler(*gani, *rig);
  ASSERT_GE(sampler.LengthSeconds(), SPAN_S);
  const auto a = sampler.Sample(0.0), b = sampler.Sample(SPAN_S / pesrig::PES_FRAME_S);
  const double travelled = std::hypot(b.bodyPosition.x - a.bodyPosition.x, b.bodyPosition.z - a.bodyPosition.z);
  EXPECT_NEAR(travelled / SPAN_S, LIVE_RUN_3_3_SPEED_MPS, SPEED_TOLERANCE_MPS);
}

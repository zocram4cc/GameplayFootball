#include <gtest/gtest.h>

#include <cmath>

#include <cstdlib>
#include <string>

#include <algorithm>
#include <optional>

#include "pes/clips.hpp"
#include "pes/handseq.hpp"
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

namespace {
double AngleDeg(const pes::Quat& q) { return 2.0 * std::acos(std::min(1.0, std::fabs(double(q.w)))) * 180.0 / M_PI; }

struct HandFixture {
  pes::PesVfs vfs;
  std::optional<pesrig::Rig> rig;
  std::optional<pes::HandSequences> seq;
  std::optional<pesrig::HandPoseLibrary> poses;
};
HandFixture* Hands() {
  static HandFixture f;
  static bool tried = false;
  if (!tried) {
    tried = true;
    const char* root = std::getenv("PES21_ROOT");
    if (!root) return nullptr;
    std::string err;
    if (!f.vfs.Mount(root, &err)) return ADD_FAILURE() << err, nullptr;
    f.rig = pesrig::BuildRig(f.vfs, &err);
    f.seq = pes::LoadHandSequences(f.vfs, nullptr, &err);
    if (f.seq) f.poses = pesrig::LoadHandPoses(f.vfs, f.seq->poses, &err);
    if (!f.rig || !f.poses) return ADD_FAILURE() << err, nullptr;
  }
  return f.poses ? &f : nullptr;
}
int PoseIndex(const HandFixture& f, const std::string& stem) {
  return int(std::find(f.seq->poses.begin(), f.seq->poses.end(), stem) - f.seq->poses.begin());
}
int PesJoint(const pesrig::Rig& rig, const std::string& pes) {  // by PES bone name
  for (size_t i = 0; i < rig.bones.size(); ++i)
    if (rig.bones[i].pesBone == pes) return int(i);
  return -1;
}
}  // namespace

// normal.gani is a gently curled hand: index mcp 15, pip 32, dip 20 degrees
// (decoded in GameplayFootball; AGENTS.md history).
TEST(HandPoseInstall, NormalIsAGentlyCurledHand) {
  auto* f = Hands();
  if (!f) GTEST_SKIP() << "PES21_ROOT not set";
  constexpr double CURL_TOLERANCE_DEG = 3.0;
  std::vector<pes::Quat> local(f->rig->bones.size());
  pesrig::ApplyHandPose(*f->rig, *f->poses, {{PoseIndex(*f, "normal"), 1.0f}}, false, local);
  EXPECT_NEAR(AngleDeg(local[PesJoint(*f->rig, "skh_index_mcp_l")]), 15.0, CURL_TOLERANCE_DEG);
  EXPECT_NEAR(AngleDeg(local[PesJoint(*f->rig, "skh_index_pip_l")]), 32.0, CURL_TOLERANCE_DEG);
  EXPECT_NEAR(AngleDeg(local[PesJoint(*f->rig, "skh_index_dip_l")]), 20.0, CURL_TOLERANCE_DEG);
}

// The right hand wears the left-authored pose mirrored across x = 0.
TEST(HandPoseInstall, RightHandIsTheMirroredLeft) {
  auto* f = Hands();
  if (!f) GTEST_SKIP() << "PES21_ROOT not set";
  std::vector<pes::Quat> left(f->rig->bones.size()), right(f->rig->bones.size());
  const std::vector<pes::HandWeight> pose = {{PoseIndex(*f, "nigiri"), 1.0f}};
  pesrig::ApplyHandPose(*f->rig, *f->poses, pose, false, left);
  pesrig::ApplyHandPose(*f->rig, *f->poses, pose, true, right);
  for (const char* bone : {"skh_thumb_mcp", "skh_index_pip", "skh_ring_dip"}) {
    const auto& l = left[PesJoint(*f->rig, std::string(bone) + "_l")];
    const auto& r = right[PesJoint(*f->rig, std::string(bone) + "_r")];
    EXPECT_NEAR(r.x, l.x, 1e-6f) << bone;
    EXPECT_NEAR(r.y, -l.y, 1e-6f) << bone;
    EXPECT_NEAR(r.z, -l.z, 1e-6f) << bone;
    EXPECT_NEAR(r.w, l.w, 1e-6f) << bone;
  }
}

// Blended poses land between their parts.
TEST(HandPoseInstall, AnEvenBlendSitsBetweenTheTwoPoses) {
  auto* f = Hands();
  if (!f) GTEST_SKIP() << "PES21_ROOT not set";
  const int a = PoseIndex(*f, "normal"), b = PoseIndex(*f, "nigiri"), j = PesJoint(*f->rig, "skh_middle_pip_l");
  std::vector<pes::Quat> pa(f->rig->bones.size()), pb = pa, mix = pa;
  pesrig::ApplyHandPose(*f->rig, *f->poses, {{a, 1.0f}}, false, pa);
  pesrig::ApplyHandPose(*f->rig, *f->poses, {{b, 1.0f}}, false, pb);
  pesrig::ApplyHandPose(*f->rig, *f->poses, {{a, 0.5f}, {b, 0.5f}}, false, mix);
  const double lo = std::min(AngleDeg(pa[j]), AngleDeg(pb[j])), hi = std::max(AngleDeg(pa[j]), AngleDeg(pb[j]));
  EXPECT_GT(hi - lo, 10.0) << "normal and nigiri should differ at the middle pip";
  EXPECT_GT(AngleDeg(mix[j]), lo);
  EXPECT_LT(AngleDeg(mix[j]), hi);
}

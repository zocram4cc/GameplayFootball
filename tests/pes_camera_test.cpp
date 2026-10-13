#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "pes/camera.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"

// PES's Dynamic Wide in-play camera (docs/re/pes-re-stadium-camera.md 4.3-4.6).

namespace {
// Synthetic parameters: every value distinct, so a swapped field shows.
pes::DynamicWideParams Synthetic() {
  pes::DynamicWideParams p{};
  p.far = {1.0f, 20.0f, 0.4f, 0.5f, 50.0f, -10.0f, 20.0f, 40.0f, 1.3f};
  p.near = {2.0f, 30.0f, 0.3f, 0.7f, 30.0f, -5.0f, 15.0f, 35.0f, 1.2f};
  p.marginUnder = 5.0f;
  p.marginUpper = 10.0f;
  for (auto& r : p.presets) r = p.far;
  p.presets[1].pan = 1.5f;  // zoom axis
  p.presets[2].pan = 1.1f;  // height axis
  p.presets[3].pan = 1.7f;
  p.farFovZoomedOutDeg = 18.0f;
  p.farPosYHigh = 0.52f;
  p.nearPosYBase = 0.38f;
  p.nearPosYHeightSpan = 0.07f;
  p.nearFovBaseDeg = 28.0f;
  p.nearFovZoomSpanDeg = 3.0f;
  p.nearPosZ = 35.0f;
  p.zMin = -8.0f;
  p.zMax = 22.0f;
  p.panBlend = 0.2f;
  p.liftBaseM = 4.0f;
  p.liftMaxM = 5.0f;
  p.leadBlend = 0.7f;
  p.clampXMarginM = 1.0f;
  p.clampZMarginM = 3.0f;
  p.smoothStepUp = 1.1f;
  p.smoothCap = 10000.0f;
  p.smoothFloor = 1.0f;
  p.kSpringGain = 0.02f;
  p.kSpringStepLimit = 0.005f;
  return p;
}
constexpr float HALF_WIDTH_M = 34.0f;
constexpr float HALF_LENGTH_M = 52.5f;  // PES FieldInfo +0x00 is 52.56; the GF pitch is 52.5 + line
// The lift's time scale for the synthetic params: 60 / the 54 Hz reference the
// exe reads (CameraTargetRE item 3), i.e. what LoadDynamicWide hands out.
constexpr float LIFT_SCALE = 60.0f / 54.0f;
}  // namespace

// t = 0 on the far touchline side (target z = -(W - marginUpper)), 1 on the near one.
TEST(DynamicWide, BlendsFarToNearByTargetZ) {
  const auto p = Synthetic();
  const float zoom = 1.0f, height = 0.0f;  // u = 0, h = 0
  auto far = pes::DynamicWideShot(p, zoom, height, -(HALF_WIDTH_M - p.marginUpper), HALF_WIDTH_M, LIFT_SCALE);
  EXPECT_FLOAT_EQ(far.record.fov, p.far.fov);
  EXPECT_FLOAT_EQ(far.record.posZ, p.far.posZ);
  EXPECT_FLOAT_EQ(far.record.posY, p.far.posY);
  auto near = pes::DynamicWideShot(p, zoom, height, HALF_WIDTH_M - p.marginUnder, HALF_WIDTH_M, LIFT_SCALE);
  EXPECT_FLOAT_EQ(near.record.fov, p.nearFovBaseDeg);
  EXPECT_FLOAT_EQ(near.record.posZ, p.nearPosZ);
  EXPECT_FLOAT_EQ(near.record.posY, p.nearPosYBase);
  EXPECT_FLOAT_EQ(near.record.lookY, p.near.lookY);
  EXPECT_FLOAT_EQ(near.record.xLimit, p.near.xLimit);
  auto mid = pes::DynamicWideShot(p, zoom, height, 0.5f * ((HALF_WIDTH_M - p.marginUnder) - (HALF_WIDTH_M - p.marginUpper)),
                                   HALF_WIDTH_M, LIFT_SCALE);
  EXPECT_NEAR(mid.record.posZ, 0.5f * (p.far.posZ + p.nearPosZ), 1e-4f);
  EXPECT_FLOAT_EQ(mid.record.zMin, p.zMin);
  EXPECT_FLOAT_EQ(mid.record.zMax, p.zMax);
}

TEST(DynamicWide, SlidersMoveFovPosYAndPan) {
  const auto p = Synthetic();
  const float farZ = -(HALF_WIDTH_M - p.marginUpper);
  auto wide = pes::DynamicWideShot(p, 0.0f, 1.0f, farZ, HALF_WIDTH_M, LIFT_SCALE);  // u = 1, h = 1
  EXPECT_FLOAT_EQ(wide.record.fov, p.farFovZoomedOutDeg);
  EXPECT_FLOAT_EQ(wide.record.posY, p.farPosYHigh);
  // Pan: bilinear over the four presets, a = zoom, b = height: preset 2 here.
  EXPECT_NEAR(wide.panEff, 1.0f + (p.presets[2].pan - 1.0f) * p.panBlend, 1e-6f);
  auto zoomed = pes::DynamicWideShot(p, 1.0f, 1.0f, farZ, HALF_WIDTH_M, LIFT_SCALE);
  EXPECT_NEAR(zoomed.panEff, 1.0f + (p.presets[3].pan - 1.0f) * p.panBlend, 1e-6f);
}

TEST(DynamicWide, PoseFollowsTheTargetFromThePositiveZTouchline) {
  const auto p = Synthetic();
  auto shot = pes::DynamicWideShot(p, 1.0f, 0.0f, 0.0f, HALF_WIDTH_M, LIFT_SCALE);
  const float k = 1.0f;
  auto pose = pes::FramePose(shot, 10.0f, 0.0f, 0.0f, k, HALF_LENGTH_M, HALF_WIDTH_M);
  EXPECT_NEAR(pose.cz - pose.tz, shot.record.posZ * k, 1e-4f);
  EXPECT_NEAR(pose.cy, shot.record.posZ * shot.record.posY * k, 1e-4f);
  EXPECT_NEAR(pose.cx, 10.0f * (1.0f + k * (1.0f / shot.panEff - 1.0f)), 1e-4f);
  EXPECT_FLOAT_EQ(pose.vfovDeg, shot.record.fov);
  // target.y = LookY + the lift; a focus y at or below -liftBaseM lifts nothing.
  auto flat = pes::FramePose(shot, 10.0f, 0.0f, -p.liftBaseM * LIFT_SCALE, k, HALF_LENGTH_M, HALF_WIDTH_M);
  EXPECT_FLOAT_EQ(flat.ty, shot.record.lookY);
  // Target clamps: z into [zMin, zMax], |x| to X_limit (both at k = 1).
  auto clamped = pes::FramePose(shot, 1000.0f, -1000.0f, -p.liftBaseM * LIFT_SCALE, k, HALF_LENGTH_M,
                               HALF_WIDTH_M);
  EXPECT_FLOAT_EQ(clamped.tz, shot.record.zMin);
  EXPECT_FLOAT_EQ(clamped.tx, shot.record.xLimit);
}

// The k spring: v' = clamp((goal - k)*gain - v, +-limit*k); converges on goal.
// The gain and the limit come from the exe, so the springs used here carry
// distinct values and a swapped constant would change the trajectory.
TEST(DynamicWide, ZoomSpringConvergesWithoutOvershootingFar) {
  // A loose step limit lets the gain bind, so the trajectory is a direct read
  // of it; starting far from the goal keeps the steps large.
  pes::ZoomSpring s{2.0f, 0.0f, 0.01f, 0.5f};
  for (int i = 0; i < 60; ++i) s.Step(1.0f);
  EXPECT_NEAR(s.k, 1.547157f, 1e-4f);
  // A tight limit instead reads the limit: both constants are exe-derived, so
  // neither may be assumed.
  pes::ZoomSpring tight{2.0f, 0.0f, 0.01f, 0.001f};
  for (int i = 0; i < 60; ++i) tight.Step(1.0f);
  EXPECT_NEAR(tight.k, 1.558226f, 1e-4f);
  pes::ZoomSpring settled{2.0f, 0.0f, 0.01f, 0.5f};
  for (int i = 0; i < 2000; ++i) settled.Step(1.0f);
  EXPECT_NEAR(settled.k, 1.0f, 1e-3f);
}

// Live sample 20:39 (report 4.6): sliders fit zoom 0.79 / height 0.20,
// target (-5.31, 0.90, -8.00), cam (-4.99, 19.16, 36.96), vFOV 23.26 deg, k ~ 1.001.
TEST(DynamicWideInstall, ReproducesTheLiveCamera) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto pe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(pe) << err;
  auto p = pes::LoadDynamicWide(*pe, vfs, &err);
  ASSERT_TRUE(p) << err;
  constexpr float LIVE_ZOOM = 0.79f, LIVE_HEIGHT = 0.20f, LIVE_K = 1.001f;
  constexpr float FIELD_HALF_WIDTH_M = 34.06f;  // FieldInfo +0x08, live
  constexpr float FIELD_HALF_LENGTH_M = 52.56f; // FieldInfo +0x00, live
  auto shot = pes::DynamicWideShot(*p, LIVE_ZOOM, LIVE_HEIGHT, -8.00f, FIELD_HALF_WIDTH_M, p->liftNumerator / p->fpsRef);
  auto pose = pes::FramePose(shot, -5.31f, -8.00f, 0.0f, LIVE_K, FIELD_HALF_LENGTH_M, FIELD_HALF_WIDTH_M);
  EXPECT_NEAR(pose.vfovDeg, 23.26f, 0.05f);
  EXPECT_NEAR(pose.cx, -4.99f, 0.05f);
  EXPECT_NEAR(pose.cy, 19.16f, 0.1f);
  EXPECT_NEAR(pose.cz, 36.96f, 0.1f);
}

// Item 1: per-frame rate. PES's reference FPS is .rdata 0x14259d624's
// initializer source, read through the .data 54 holder at 0x14351ae54
// (report CameraTargetRE item 3); 60 is the lift's numerator at 0x14259a764.
// The live sim step is observed at 1/48 (docs/re/pes-re-injury-fatigue.md 4b)
// and is unverified in-match, so nothing here uses it.
TEST(DynamicWideStep, UsesTheExeFrameRateReference) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto pe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(pe) << err;
  auto p = pes::LoadDynamicWide(*pe, vfs, &err);
  ASSERT_TRUE(p) << err;
  EXPECT_NEAR(1.0f / p->fpsRef, 1.0f / 54.0f, 1e-6f);
  EXPECT_NEAR(p->liftNumerator / p->fpsRef, 60.0f / 54.0f, 1e-4f);
  EXPECT_NEAR(p->liftNumerator, 60.0f, 1e-4f);
}

// Item 1: the lift factor scales with the exe's frame time, so a synthetic
// frame scale is the input the caller passes.
TEST(DynamicWide, LiftScalesByTheExeFrameTime) {
  auto p = Synthetic();
  constexpr float LOOK_Y = 0.7f;
  p.far.lookY = p.near.lookY = LOOK_Y;
  auto shot = pes::DynamicWideShot(p, 1.0f, 0.0f, 0.0f, HALF_WIDTH_M, LIFT_SCALE);
  constexpr float FOCUS_Y = -2.0f;  // (FOCUS_Y + 4)*scale = 2.222, below the ceiling
  constexpr float SCALE = LIFT_SCALE;
  const float expected = std::clamp((FOCUS_Y + p.liftBaseM) * SCALE, 0.0f, p.liftMaxM);
  auto pose = pes::FramePose(shot, 0.0f, 0.0f, FOCUS_Y, 1.0f, HALF_LENGTH_M, HALF_WIDTH_M);
  EXPECT_NEAR(pose.ty, LOOK_Y + expected, 1e-5f);
  // The shot carries the scale, so a slower frame time moves the target.
  auto slow = pes::DynamicWideShot(p, 1.0f, 0.0f, 0.0f, HALF_WIDTH_M, LIFT_SCALE * 0.5f);
  EXPECT_FLOAT_EQ(slow.liftBaseM, p.liftBaseM);
  EXPECT_FLOAT_EQ(slow.liftTimeScale, LIFT_SCALE * 0.5f);
  auto slowPose = pes::FramePose(slow, 0.0f, 0.0f, FOCUS_Y, 1.0f, HALF_LENGTH_M, HALF_WIDTH_M);
  EXPECT_NEAR(slowPose.ty, LOOK_Y + 0.5f * expected, 1e-5f);
  // The lift clamps at the exe's ceiling, and at 0 on the ground, so the
  // ball-height term never pushes the target below LookY.
  auto clamped = pes::FramePose(shot, 0.0f, 0.0f, 100.0f, 1.0f, HALF_LENGTH_M, HALF_WIDTH_M);
  EXPECT_FLOAT_EQ(clamped.ty, LOOK_Y + 5.0f);
  auto floor = pes::FramePose(shot, 0.0f, 0.0f, -100.0f, 1.0f, HALF_LENGTH_M, HALF_WIDTH_M);
  EXPECT_FLOAT_EQ(floor.ty, LOOK_Y);
}

// Item 2: the running smoothing weight (report CameraTargetRE 3a). The
// recurrence is tgt = (prev + w*raw)/(1 + w) (asm 0x1408a3cd1-0x1408a3d41:
// mulss (%rsi) then addss prev, divss (w+1)); its fixed point is raw, and at
// w = 1 the camera moves half the remaining distance per frame.
TEST(DynamicWideSmoothing, RecurrenceConvergesOnTheRawFocus) {
  const auto p = Synthetic();
  pes::TargetSmoothing s(p);
  const pes::Vec3 raw{10.0f, 10.0f, 10.0f};
  s.Reset({0.0f, 0.0f, 0.0f});
  s.Step(raw, false);  // w = 1 (floor), from prev = 0
  EXPECT_FLOAT_EQ(s.target().x, 5.0f);
  EXPECT_FLOAT_EQ(s.target().y, 5.0f);
  EXPECT_FLOAT_EQ(s.target().z, 5.0f);
  // 40 more steps at w = 1: the remaining distance halves each frame, so the
  // target must land on raw (not stall at raw/2).
  for (int i = 0; i < 40; ++i) s.Step(raw, false);
  EXPECT_NEAR(s.target().x, raw.x, 1e-4f);
  EXPECT_NEAR(s.target().y, raw.y, 1e-4f);
  EXPECT_NEAR(s.target().z, raw.z, 1e-4f);
  // A heavier weight jumps further in one step, from the same start.
  pes::TargetSmoothing heavy(p);
  heavy.Reset({0.0f, 0.0f, 0.0f});
  heavy.Step(raw, true);  // w = 1.1
  EXPECT_NEAR(heavy.target().x, 1.1f * raw.x / 2.1f, 1e-6f);
  EXPECT_NEAR(heavy.weight(), 1.1f, 1e-6f);
}

// The exe's own guard: the whole smoothing block is skipped when the weight
// reaches its cap (comiss 0x1408a3cd1 against .rdata 0x14259d634 = 10000), so
// at w = 10000 the stored target must not move at all.
TEST(DynamicWideSmoothing, AtTheWeightCapTheTargetDoesNotMove) {
  const auto p = Synthetic();
  pes::TargetSmoothing s(p);
  s.Reset({0.0f, 0.0f, 0.0f});
  for (int i = 0; i < 120; ++i) s.Step({1.0f, 0.0f, 0.0f}, true);
  ASSERT_FLOAT_EQ(s.weight(), p.smoothCap);
  const pes::Vec3 held = s.target();
  s.Step({50.0f, 0.0f, 0.0f}, true);
  EXPECT_FLOAT_EQ(s.target().x, held.x);  // skipped, not blended
  EXPECT_FLOAT_EQ(s.target().y, held.y);
  EXPECT_FLOAT_EQ(s.target().z, held.z);
}

TEST(DynamicWideSmoothing, WeightGrowsWhileMarginTimeIsActiveAndFallsBack) {
  const auto p = Synthetic();
  pes::TargetSmoothing s(p);
  s.Reset({0.0f, 0.0f, 0.0f});
  for (int i = 0; i < 120; ++i) s.Step({1.0f, 0.0f, 0.0f}, true);
  EXPECT_FLOAT_EQ(s.weight(), p.smoothCap);  // the cap, not merely "past it"
  const float snapped = s.target().x;
  for (int i = 0; i < 120; ++i) s.Step({1.0f, 0.0f, 0.0f}, false);
  EXPECT_FLOAT_EQ(s.weight(), p.smoothFloor);  // floor at 1
  // Decayed to the floor, one step towards a raw focus 21 away halves the
  // remaining distance (the raw/2 fixed point of the first step).
  s.Step({snapped + 21.0f, 0.0f, 0.0f}, false);
  EXPECT_NEAR(s.target().x, snapped + 10.5f, 1e-4f);
}

// Item 4: focus lead. out = w*focus + (1-w)*ball, w = 0.7 * |f-b|_xyz/|f-b|_xz
// (report CameraTargetRE item 2). Zero horizontal distance must not divide
// by zero: the lead then collapses, so the focus passes through unchanged.
TEST(DynamicWide, LeadPullsTheFocusTowardsTheBall) {
  const auto p = Synthetic();
  const pes::Vec3 focus{30.0f, 2.0f, -8.0f}, ball{0.0f, 0.0f, 0.0f};
  const float a = std::sqrt(30.0f * 30.0f + 4.0f + 64.0f) / std::sqrt(30.0f * 30.0f + 64.0f);
  const float w = p.leadBlend * a;
  auto out = pes::LeadFocus(focus, ball, p.leadBlend);
  EXPECT_NEAR(out.x, w * focus.x, 1e-4f);
  EXPECT_NEAR(out.y, w * focus.y, 1e-4f);
  EXPECT_NEAR(out.z, w * focus.z, 1e-4f);
  auto vertical = pes::LeadFocus(focus, {30.0f, 0.0f, -8.0f}, p.leadBlend);
  EXPECT_FLOAT_EQ(vertical.x, focus.x);
  EXPECT_FLOAT_EQ(vertical.y, focus.y);
  EXPECT_FLOAT_EQ(vertical.z, focus.z);
  auto alone = pes::LeadFocus(ball, ball, p.leadBlend);
  EXPECT_FLOAT_EQ(alone.x, 0.0f);
  EXPECT_FLOAT_EQ(alone.y, 0.0f);
  EXPECT_FLOAT_EQ(alone.z, 0.0f);
}

// Item 3/4: k-blended clamps (report CameraTargetRE item 4). xHi =
// (L+1) - ((L+1) - X_limit)*k; zHi = (W+3) - ((W+3) - Z_max)*k; zLo =
// -(W+3) - (-(W+3) - Z_min)*k.
TEST(DynamicWide, ClampsBlendFromTheWideBoundsByK) {
  const auto p = Synthetic();
  auto shot = pes::DynamicWideShot(p, 1.0f, 0.0f, 0.0f, HALF_WIDTH_M, LIFT_SCALE);
  constexpr float L = 52.56f, W = 34.06f;
  const auto at = [&](float k, float z) { return pes::FramePose(shot, 1000.0f, z, 0.0f, k, L, W); };
  const auto settled = at(1.0f, -1000.0f);
  EXPECT_FLOAT_EQ(settled.tx, shot.record.xLimit);
  EXPECT_FLOAT_EQ(settled.tz, shot.record.zMin);
  const auto wide = at(0.0f, -1000.0f);
  EXPECT_NEAR(wide.tx, L + p.clampXMarginM, 1e-4f);
  EXPECT_NEAR(wide.tz, -(W + p.clampZMarginM), 1e-4f);
  const auto half = at(0.5f, -1000.0f);
  EXPECT_NEAR(half.tx, 0.5f * ((L + p.clampXMarginM) + shot.record.xLimit), 1e-4f);
  EXPECT_NEAR(half.tz, 0.5f * (-(W + p.clampZMarginM) + shot.record.zMin), 1e-4f);
  // The z upper bound blends the same way.
  auto hi = at(1.0f, 1000.0f);
  EXPECT_FLOAT_EQ(hi.tz, shot.record.zMax);
  auto hiWide = at(0.0f, 1000.0f);
  EXPECT_NEAR(hiWide.tz, W + p.clampZMarginM, 1e-4f);
}

// Item 4 / item 6, synthetic: the clamp margins and the near-Fov span come out
// of the params, so distinct synthetic values prove the reads are wired, not
// transcribed literals.
TEST(DynamicWide, MarginsAndSpansComeFromTheParams) {
  auto p = Synthetic();
  p.clampXMarginM = 2.0f;
  p.clampZMarginM = 4.0f;
  p.leadBlend = 0.5f;
  p.nearFovZoomSpanDeg = 7.0f;
  auto shot = pes::DynamicWideShot(p, 0.0f, 0.0f, 0.0f, HALF_WIDTH_M, LIFT_SCALE);  // u = 1
  auto pose = pes::FramePose(shot, 1000.0f, 0.0f, 0.0f, 0.0f, HALF_LENGTH_M, HALF_WIDTH_M);
  EXPECT_NEAR(pose.tx, HALF_LENGTH_M + 2.0f, 1e-4f);
  // Both z bounds ride the margin: lower at -W-margin, upper at +W+margin.
  auto hi = pes::FramePose(shot, 0.0f, 1000.0f, 0.0f, 0.0f, HALF_LENGTH_M, HALF_WIDTH_M);
  EXPECT_NEAR(hi.tz, HALF_WIDTH_M + 4.0f, 1e-4f);
  auto far = pes::DynamicWideShot(p, 0.0f, 0.0f, HALF_WIDTH_M - p.marginUnder, HALF_WIDTH_M, LIFT_SCALE);
  EXPECT_FLOAT_EQ(far.record.fov, p.nearFovBaseDeg - 7.0f);
  const pes::Vec3 focus{10.0f, 1.0f, -4.0f}, ball{0.0f, 0.0f, 0.0f};
  auto out = pes::LeadFocus(focus, ball, p.leadBlend);
  const float a = std::sqrt(117.0f) / std::sqrt(116.0f);
  EXPECT_NEAR(out.x, 0.5f * a * focus.x, 1e-4f);
}

// Item 6: the near-Fov zoom span, the clamp margins and the lift constants are
// .rdata, read from the exe (report CameraTargetRE items 1, 4, 6).
TEST(DynamicWideInstall, ReadsTheCameraConstantsFromTheExe) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto pe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(pe) << err;
  auto p = pes::LoadDynamicWide(*pe, vfs, &err);
  ASSERT_TRUE(p) << err;
  EXPECT_FLOAT_EQ(p->nearFovZoomSpanDeg, 3.0f);   // .rdata 0x14258c264
  EXPECT_FLOAT_EQ(p->clampXMarginM, 1.0f);        // .rdata 0x14256ae1c
  EXPECT_FLOAT_EQ(p->clampZMarginM, 3.0f);        // .rdata 0x14258c264
  EXPECT_FLOAT_EQ(p->liftBaseM, 4.0f);            // .rdata 0x1425656c0
  EXPECT_FLOAT_EQ(p->liftMaxM, 5.0f);             // .rdata 0x14259222c
  EXPECT_NEAR(p->leadBlend, 0.7f, 1e-6f);         // .rdata 0x14257a194
  EXPECT_NEAR(p->smoothStepUp, 1.1f, 1e-6f);      // .rdata 0x1425a3590
  EXPECT_FLOAT_EQ(p->smoothCap, 10000.0f);        // .rdata 0x14259d634
  EXPECT_NEAR(p->kSpringGain, 0.02f, 1e-6f);      // .rdata 0x1425c4894
  EXPECT_NEAR(p->kSpringStepLimit, 0.005f, 1e-6f);// .rdata 0x1425b8708
}

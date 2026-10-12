#include <gtest/gtest.h>

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
  return p;
}
constexpr float HALF_WIDTH_M = 34.0f;
}  // namespace

// t = 0 on the far touchline side (target z = -(W - marginUpper)), 1 on the near one.
TEST(DynamicWide, BlendsFarToNearByTargetZ) {
  const auto p = Synthetic();
  const float zoom = 1.0f, height = 0.0f;  // u = 0, h = 0
  auto far = pes::DynamicWideShot(p, zoom, height, -(HALF_WIDTH_M - p.marginUpper), HALF_WIDTH_M);
  EXPECT_FLOAT_EQ(far.record.fov, p.far.fov);
  EXPECT_FLOAT_EQ(far.record.posZ, p.far.posZ);
  EXPECT_FLOAT_EQ(far.record.posY, p.far.posY);
  auto near = pes::DynamicWideShot(p, zoom, height, HALF_WIDTH_M - p.marginUnder, HALF_WIDTH_M);
  EXPECT_FLOAT_EQ(near.record.fov, p.nearFovBaseDeg);
  EXPECT_FLOAT_EQ(near.record.posZ, p.nearPosZ);
  EXPECT_FLOAT_EQ(near.record.posY, p.nearPosYBase);
  EXPECT_FLOAT_EQ(near.record.lookY, p.near.lookY);
  EXPECT_FLOAT_EQ(near.record.xLimit, p.near.xLimit);
  auto mid = pes::DynamicWideShot(p, zoom, height, 0.5f * ((HALF_WIDTH_M - p.marginUnder) - (HALF_WIDTH_M - p.marginUpper)),
                                   HALF_WIDTH_M);
  EXPECT_NEAR(mid.record.posZ, 0.5f * (p.far.posZ + p.nearPosZ), 1e-4f);
  EXPECT_FLOAT_EQ(mid.record.zMin, p.zMin);
  EXPECT_FLOAT_EQ(mid.record.zMax, p.zMax);
}

TEST(DynamicWide, SlidersMoveFovPosYAndPan) {
  const auto p = Synthetic();
  const float farZ = -(HALF_WIDTH_M - p.marginUpper);
  auto wide = pes::DynamicWideShot(p, 0.0f, 1.0f, farZ, HALF_WIDTH_M);  // u = 1, h = 1
  EXPECT_FLOAT_EQ(wide.record.fov, p.farFovZoomedOutDeg);
  EXPECT_FLOAT_EQ(wide.record.posY, p.farPosYHigh);
  // Pan: bilinear over the four presets, a = zoom, b = height: preset 2 here.
  EXPECT_NEAR(wide.panEff, 1.0f + (p.presets[2].pan - 1.0f) * p.panBlend, 1e-6f);
  auto zoomed = pes::DynamicWideShot(p, 1.0f, 1.0f, farZ, HALF_WIDTH_M);
  EXPECT_NEAR(zoomed.panEff, 1.0f + (p.presets[3].pan - 1.0f) * p.panBlend, 1e-6f);
}

TEST(DynamicWide, PoseFollowsTheTargetFromThePositiveZTouchline) {
  const auto p = Synthetic();
  auto shot = pes::DynamicWideShot(p, 1.0f, 0.0f, 0.0f, HALF_WIDTH_M);
  const float k = 1.0f;
  auto pose = pes::FramePose(shot, 10.0f, 0.0f, 0.0f, k);
  EXPECT_NEAR(pose.cz - pose.tz, shot.record.posZ * k, 1e-4f);
  EXPECT_NEAR(pose.cy, shot.record.posZ * shot.record.posY * k, 1e-4f);
  EXPECT_NEAR(pose.cx, 10.0f * (1.0f + k * (1.0f / shot.panEff - 1.0f)), 1e-4f);
  EXPECT_FLOAT_EQ(pose.vfovDeg, shot.record.fov);
  EXPECT_FLOAT_EQ(pose.ty, shot.record.lookY);
  // Target clamps: z into [zMin, zMax], |x| to X_limit.
  auto clamped = pes::FramePose(shot, 1000.0f, -1000.0f, 0.0f, k);
  EXPECT_FLOAT_EQ(clamped.tz, shot.record.zMin);
  EXPECT_FLOAT_EQ(clamped.tx, shot.record.xLimit);
}

// The k spring: v' = clamp((goal - k)*0.02 - v, +-0.005k); converges on goal.
TEST(DynamicWide, ZoomSpringConvergesWithoutOvershootingFar) {
  pes::ZoomSpring s{0.8f, 0.0f};
  float peak = 0.0f;
  for (int i = 0; i < 2000; ++i) peak = std::max(peak, (s.Step(1.0f), s.k));
  EXPECT_NEAR(s.k, 1.0f, 1e-3f);
  EXPECT_LT(peak, 1.1f);
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
  auto shot = pes::DynamicWideShot(*p, LIVE_ZOOM, LIVE_HEIGHT, -8.00f, FIELD_HALF_WIDTH_M);
  auto pose = pes::FramePose(shot, -5.31f, -8.00f, 0.0f, LIVE_K);
  EXPECT_NEAR(pose.vfovDeg, 23.26f, 0.05f);
  EXPECT_NEAR(pose.cx, -4.99f, 0.05f);
  EXPECT_NEAR(pose.cy, 19.16f, 0.1f);
  EXPECT_NEAR(pose.cz, 36.96f, 0.1f);
}

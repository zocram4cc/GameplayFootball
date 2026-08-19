// The orbit camera a model viewer needs, and the framing that fits a model in shot.
//
// Troubleshooting a model inside a running match is the wrong tool. This is the
// camera for a standalone viewer: it orbits a model, frames it from its own bounds,
// and has no idea a pitch exists. Reusable rather than debug-only, because an EDIT
// mode will want the same thing.

#include <gtest/gtest.h>

#include <cmath>

#include "utils/viewercamera.hpp"

namespace ViewerCamera = blunted::ViewerCamera;

TEST(ViewerCamera, ItFramesAModelFromItsOwnBounds) {
  // a 1.8 m figure standing on the ground
  const auto shot = ViewerCamera::Frame({-0.4f, -0.3f, 0.0f}, {0.4f, 0.3f, 1.8f}, 35.0f);
  // looks at the middle of it
  EXPECT_NEAR(shot.target[2], 0.9f, 1e-4);
  // and stands far enough back that the whole height fits the lens
  EXPECT_GT(shot.distance, 1.8f * 0.5f / std::tan(35.0f * 0.5f * 3.14159265f / 180.0f));
}

TEST(ViewerCamera, ABiggerModelPushesTheCameraBack) {
  const auto small = ViewerCamera::Frame({0, 0, 0}, {0.2f, 0.2f, 0.4f}, 35.0f);
  const auto large = ViewerCamera::Frame({0, 0, 0}, {2.0f, 2.0f, 4.0f}, 35.0f);
  EXPECT_GT(large.distance, small.distance);
}

TEST(ViewerCamera, ANarrowerLensPushesItBackToo) {
  const auto wide = ViewerCamera::Frame({0, 0, 0}, {1, 1, 2}, 60.0f);
  const auto narrow = ViewerCamera::Frame({0, 0, 0}, {1, 1, 2}, 20.0f);
  EXPECT_GT(narrow.distance, wide.distance);
}

TEST(ViewerCamera, AModelOfNothingStillGivesAUsableShot) {
  // a mesh that arrived empty must not divide by its own size
  const auto shot = ViewerCamera::Frame({0, 0, 0}, {0, 0, 0}, 35.0f);
  EXPECT_GT(shot.distance, 0.0f);
  EXPECT_TRUE(std::isfinite(shot.distance));
}

TEST(ViewerCamera, OrbitingPutsTheCameraRoundTheTarget) {
  ViewerCamera::Shot shot = ViewerCamera::Frame({0, 0, 0}, {1, 1, 2}, 35.0f);
  shot.yaw = 0.0f;
  shot.pitch = 0.0f;
  const auto front = ViewerCamera::Position(shot);
  // at yaw 0 the camera sits on -y, looking towards +y
  EXPECT_NEAR(front[0], shot.target[0], 1e-4);
  EXPECT_NEAR(front[1], shot.target[1] - shot.distance, 1e-3);

  shot.yaw = 3.14159265f * 0.5f;
  const auto side = ViewerCamera::Position(shot);
  EXPECT_NEAR(side[0], shot.target[0] + shot.distance, 1e-3);
  EXPECT_NEAR(side[1], shot.target[1], 1e-3);
}

TEST(ViewerCamera, PitchLiftsItWithoutChangingTheDistance) {
  ViewerCamera::Shot shot = ViewerCamera::Frame({0, 0, 0}, {1, 1, 2}, 35.0f);
  shot.pitch = 0.5f;
  const auto at = ViewerCamera::Position(shot);
  const float dx = at[0] - shot.target[0], dy = at[1] - shot.target[1],
              dz = at[2] - shot.target[2];
  EXPECT_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), shot.distance, 1e-3);
  EXPECT_GT(at[2], shot.target[2]);
}

TEST(ViewerCamera, PitchIsClampedShortOfLookingStraightDown) {
  // straight down loses the up vector and the view flips
  ViewerCamera::Shot shot = ViewerCamera::Frame({0, 0, 0}, {1, 1, 2}, 35.0f);
  shot.pitch = 99.0f;
  const auto clamped = ViewerCamera::Clamp(shot);
  EXPECT_LT(clamped.pitch, 3.14159265f * 0.5f);
  shot.pitch = -99.0f;
  EXPECT_GT(ViewerCamera::Clamp(shot).pitch, -3.14159265f * 0.5f);
}

TEST(ViewerCamera, ZoomNeverPassesThroughTheModel) {
  ViewerCamera::Shot shot = ViewerCamera::Frame({0, 0, 0}, {1, 1, 2}, 35.0f);
  shot.distance = -5.0f;
  EXPECT_GT(ViewerCamera::Clamp(shot).distance, 0.0f);
}

// A turntable: N frames all the way round, for a headless viewer that writes stills
// instead of opening a window.
TEST(ViewerCamera, ATurntableGoesAllTheWayRoundOnce) {
  const auto shot = ViewerCamera::Frame({0, 0, 0}, {1, 1, 2}, 35.0f);
  EXPECT_NEAR(ViewerCamera::TurntableYaw(shot, 0, 8), 0.0f, 1e-5);
  EXPECT_NEAR(ViewerCamera::TurntableYaw(shot, 4, 8), 3.14159265f, 1e-4);
  EXPECT_LT(ViewerCamera::TurntableYaw(shot, 7, 8), 2.0f * 3.14159265f);
}

TEST(ViewerCamera, ATurntableOfOneFrameDoesNotDivideByZero) {
  const auto shot = ViewerCamera::Frame({0, 0, 0}, {1, 1, 2}, 35.0f);
  EXPECT_TRUE(std::isfinite(ViewerCamera::TurntableYaw(shot, 0, 1)));
}

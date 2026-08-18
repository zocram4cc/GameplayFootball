// Keeping an imported shot out of the players it is filming.
//
// PES's entrance camerawork is composed around PES's own players, and several
// of its cuts are deliberately tight - a face, a shoulder, the line seen past
// somebody's arm. Played over a 4cc cast, whose characters carry hats, wings and
// props PES never allowed for, those same cuts put the lens inside a chest: the
// frame fills with a single shirt and the walk-on disappears behind it.
//
// The shot is worth keeping, so it is dollied straight back along its own view
// axis until the nearest thing it is looking at clears the lens. The framing,
// the lens and the move are all PES's; only the distance changes, and only when
// something is too close.

#include <gtest/gtest.h>

#include "onthepitch/camerastandoff.hpp"

using blunted::Vector3;

namespace {
constexpr float kClear = 2.5f;  // how close a body may come to the lens
}

TEST(CameraStandoff, AnEmptySceneNeedsNoPush) {
  EXPECT_FLOAT_EQ(CameraStandoff::PushBack({}, Vector3(0, 0, 2), Vector3(0, 1, 0), kClear), 0.0f);
}

TEST(CameraStandoff, SomebodyWellDownTheLineIsNoProblem) {
  const std::vector<Vector3> cast = {Vector3(0, 20, 0)};
  EXPECT_FLOAT_EQ(CameraStandoff::PushBack(cast, Vector3(0, 0, 2), Vector3(0, 1, 0), kClear), 0.0f);
}

TEST(CameraStandoff, SomebodyAgainstTheLensPushesItBack) {
  // Half a metre in front of a lens two metres up, so the body's chest is
  // sqrt(0.5^2 + 1.1^2) = 1.21 m away and it has to retreat the rest.
  const std::vector<Vector3> cast = {Vector3(0, 0.5f, 0)};
  EXPECT_NEAR(CameraStandoff::PushBack(cast, Vector3(0, 0, 2), Vector3(0, 1, 0), kClear), 1.292f,
              0.01f);
}

TEST(CameraStandoff, TheNearestOneDecides) {
  const std::vector<Vector3> cast = {Vector3(0, 2.0f, 0), Vector3(0, 0.5f, 0), Vector3(0, 8, 0)};
  EXPECT_NEAR(CameraStandoff::PushBack(cast, Vector3(0, 0, 2), Vector3(0, 1, 0), kClear), 1.292f,
              0.01f);
}

TEST(CameraStandoff, SomebodyBehindTheCameraIsNotInShot) {
  const std::vector<Vector3> cast = {Vector3(0, -0.5f, 0)};
  EXPECT_FLOAT_EQ(CameraStandoff::PushBack(cast, Vector3(0, 0, 2), Vector3(0, 1, 0), kClear), 0.0f);
}

TEST(CameraStandoff, SomebodyOffToTheSideIsNotInShotEither) {
  // Beside the lens, not in front of it: pushing back for him would only take
  // the camera away from what it is filming.
  const std::vector<Vector3> cast = {Vector3(6.0f, 0.3f, 0)};
  EXPECT_FLOAT_EQ(CameraStandoff::PushBack(cast, Vector3(0, 0, 2), Vector3(0, 1, 0), kClear), 0.0f);
}

TEST(CameraStandoff, HeightIsMeasuredToTheBodyNotTheFeet) {
  // The cast positions are on the ground and the camera is at head height; a
  // player two metres away is not "two metres below the lens" away.
  const std::vector<Vector3> cast = {Vector3(0, 1.0f, 0)};
  const float push = CameraStandoff::PushBack(cast, Vector3(0, 0, 1.6f), Vector3(0, 1, 0), kClear);
  // Measured on the ground this would be 2.5 - 1.0; to the chest it is less.
  EXPECT_LT(push, 1.5f);
  EXPECT_NEAR(push, 1.279f, 0.01f);
}

TEST(CameraStandoff, ThePushIsNeverNegative) {
  const std::vector<Vector3> cast = {Vector3(0, 30, 0)};
  EXPECT_GE(CameraStandoff::PushBack(cast, Vector3(0, 0, 2), Vector3(0, 1, 0), kClear), 0.0f);
}

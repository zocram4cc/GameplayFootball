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

#include <cmath>

#include <gtest/gtest.h>

#include "onthepitch/camerastandoff.hpp"

using blunted::Vector3;
using CameraStandoff::Body;

namespace {
constexpr float kClear = 2.5f;  // how close a body's surface may come to the lens
const Vector3 kEye(0, 0, 2);
const Vector3 kAim(0, 1, 0);

Body Man(float x, float y, float radius = 0.0f) { return Body{Vector3(x, y, 0), radius}; }

// Where the pushed-back eye ends up relative to the body's centre (0.9 m up).
float DistanceAfter(const std::vector<Body>& cast, float push) {
  const Vector3 eye = kEye - kAim * push;
  Vector3 centre = cast.front().position;
  centre.coords[2] += 0.9f;
  return (centre - eye).GetLength();
}
}  // namespace

TEST(CameraStandoff, AnEmptySceneNeedsNoPush) {
  EXPECT_FLOAT_EQ(CameraStandoff::PushBack({}, kEye, kAim, kClear), 0.0f);
}

TEST(CameraStandoff, SomebodyWellDownTheLineIsNoProblem) {
  EXPECT_FLOAT_EQ(CameraStandoff::PushBack({Man(0, 20)}, kEye, kAim, kClear), 0.0f);
}

TEST(CameraStandoff, SomebodyAgainstTheLensIsPushedToExactlyTheClearance) {
  // Half a metre in front of a lens two metres up: his chest is 1.21 m away, and
  // the dolly backs off until it is exactly the clearance away - no more, since
  // every metre of push is a metre of PES's framing lost.
  const std::vector<Body> cast = {Man(0, 0.5f)};
  const float push = CameraStandoff::PushBack(cast, kEye, kAim, kClear);
  EXPECT_GT(push, 0.0f);
  EXPECT_NEAR(DistanceAfter(cast, push), kClear, 0.01f);
}

TEST(CameraStandoff, TheNearestOneDecides) {
  const std::vector<Body> cast = {Man(0, 0.5f), Man(0, 2.0f), Man(0, 8)};
  EXPECT_NEAR(CameraStandoff::PushBack(cast, kEye, kAim, kClear),
              CameraStandoff::PushBack({Man(0, 0.5f)}, kEye, kAim, kClear), 1e-4f);
}

TEST(CameraStandoff, SomebodyBehindTheCameraIsNotInShot) {
  EXPECT_FLOAT_EQ(CameraStandoff::PushBack({Man(0, -0.5f)}, kEye, kAim, kClear), 0.0f);
}

TEST(CameraStandoff, SomebodyOffToTheSideIsNotInShotEither) {
  // Beside the lens, not in front of it: pushing back for him would only take
  // the camera away from what it is filming.
  EXPECT_FLOAT_EQ(CameraStandoff::PushBack({Man(6.0f, 0.3f)}, kEye, kAim, kClear), 0.0f);
}

TEST(CameraStandoff, ABroadBodyCountsUntilHisMeshHasPassed) {
  // Centre half a metre behind the lens plane, mesh 1.2 m wide either way: his
  // shoulder is still in front of the glass. A slim man there has gone by.
  EXPECT_FLOAT_EQ(CameraStandoff::PushBack({Man(0, -0.5f, 0.0f)}, kEye, kAim, kClear), 0.0f);
  EXPECT_GT(CameraStandoff::PushBack({Man(0, -0.5f, 1.2f)}, kEye, kAim, kClear), 0.0f);
}

TEST(CameraStandoff, ABroadBodyIsKeptOutByItsOwnHalfWidth) {
  // The 03-09 black frames: Wario's centre 2.8 m from the lens, beyond a 2.2 m
  // clearance, with a 2.4 m wide mesh whose glove was over the lens. Counting
  // his radius pushes the camera his half-width further.
  const std::vector<Body> slim = {Man(0, 2.8f, 0.0f)};
  const std::vector<Body> wario = {Man(0, 2.8f, 1.2f)};
  EXPECT_FLOAT_EQ(CameraStandoff::PushBack(slim, kEye, kAim, 2.2f), 0.0f);
  const float push = CameraStandoff::PushBack(wario, kEye, kAim, 2.2f);
  EXPECT_GT(push, 0.0f);
  EXPECT_NEAR(DistanceAfter(wario, push), 2.2f + 1.2f, 0.01f);
}

TEST(CameraStandoff, HeightIsMeasuredToTheBodyNotTheFeet) {
  // The cast positions are on the ground and the camera is at head height; a
  // player one metre away is not "one metre plus the lens height" away.
  const std::vector<Body> cast = {Man(0, 1.0f)};
  const float push = CameraStandoff::PushBack(cast, Vector3(0, 0, 1.6f), kAim, kClear);
  EXPECT_LT(push, 1.5f);
  EXPECT_GT(push, 0.0f);
}

TEST(CameraStandoff, ThePushIsNeverNegative) {
  EXPECT_GE(CameraStandoff::PushBack({Man(0, 30)}, kEye, kAim, kClear), 0.0f);
  // Just inside the clearance but far off the axis: the exit is behind the eye.
  EXPECT_GE(CameraStandoff::PushBack({Man(2.4f, 0.1f)}, kEye, kAim, kClear), 0.0f);
}

// Putting an imported staging where our pitch is.
//
// PES authors its entrance choreography in its own stadium's coordinates, and
// its walk-on packs start outside the field because that is where its tunnel
// mouth is: ent_009_st000 walks its cast from y -48 to y -38, and this pitch
// ends at y 36 (gametypes.hpp: x is the long axis at 55, y the short one at
// 36), so the whole walk happens past the touchline and finishes two metres
// short of it. Framed by a camera that follows the cast - which is the only way
// to film a walkout in a venue the camerawork was not authored for - it showed
// the squads strolling across open ground beside the ground.
//
// The motion is PES's and worth keeping; only its placement is wrong. So a
// staging that finishes outside the playing area is translated until it
// finishes on the centre spot, and one that already finishes inside is left
// exactly where its author put it.

#include <gtest/gtest.h>

#include "onthepitch/staginganchor.hpp"

using blunted::Vector3;

namespace {
// gametypes.hpp: x runs goal to goal, y touchline to touchline
constexpr float kHalfX = 55.0f;
constexpr float kHalfY = 36.0f;
}  // namespace

TEST(StagingAnchor, AStagingThatFinishesOnThePitchIsLeftAlone) {
  // the anthem pack lines the squads up along the halfway line
  const Vector3 offset =
      StagingAnchor::OnPitchOffset(Vector3(0.0f, -2.0f, 0.0f), kHalfX, kHalfY);
  EXPECT_FLOAT_EQ(offset.coords[0], 0.0f);
  EXPECT_FLOAT_EQ(offset.coords[1], 0.0f);
}

TEST(StagingAnchor, AWalkOnThatFinishesPastTheTouchlineIsBroughtToTheCentreSpot) {
  // ent_009_st000 finishes around y -38
  const Vector3 offset =
      StagingAnchor::OnPitchOffset(Vector3(-0.9f, -38.0f, 0.0f), kHalfX, kHalfY);
  EXPECT_NEAR(offset.coords[0], 0.9f, 0.001f);
  EXPECT_NEAR(offset.coords[1], 38.0f, 0.001f);
}

TEST(StagingAnchor, TheEdgesOfThePitchCountAsOnIt) {
  EXPECT_FLOAT_EQ(
      StagingAnchor::OnPitchOffset(Vector3(0.0f, -kHalfY + 1.0f, 0.0f), kHalfX, kHalfY).coords[1],
      0.0f);
  EXPECT_FLOAT_EQ(
      StagingAnchor::OnPitchOffset(Vector3(kHalfX - 1.0f, 0.0f, 0.0f), kHalfX, kHalfY).coords[0],
      0.0f);
}

TEST(StagingAnchor, AStagingBeyondAGoalLineIsBroughtInToo) {
  const Vector3 offset = StagingAnchor::OnPitchOffset(Vector3(-70.0f, 0.0f, 0.0f), kHalfX, kHalfY);
  EXPECT_NEAR(offset.coords[0], 70.0f, 0.001f);
}

TEST(StagingAnchor, TheHeightIsNeverTouched) {
  // The choreography's own z is what keeps feet on the ground.
  const Vector3 offset =
      StagingAnchor::OnPitchOffset(Vector3(0.0f, -48.0f, 3.2f), kHalfX, kHalfY);
  EXPECT_FLOAT_EQ(offset.coords[2], 0.0f);
}

TEST(StagingAnchor, ANonsensePitchLeavesEverythingWhereItIs) {
  // Better an unmoved staging than one translated by a garbage measurement.
  EXPECT_FLOAT_EQ(StagingAnchor::OnPitchOffset(Vector3(0.0f, -48.0f, 0.0f), 0.0f, 0.0f).coords[1],
                  0.0f);
}

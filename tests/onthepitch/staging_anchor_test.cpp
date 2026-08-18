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
// The motion is PES's and worth keeping; only its placement is wrong. Anchoring
// its finish to the centre spot was worse than useless - it put the whole ten
// metres of walk in the middle of the field, so the squads never walked in from
// anywhere. What a walk-on needs is its *start* just outside the nearest line,
// so PES's own motion carries the cast onto the pitch. A staging that already
// starts inside is left exactly where its author put it.

#include <gtest/gtest.h>

#include "onthepitch/staginganchor.hpp"

using blunted::Vector3;

namespace {
// gametypes.hpp: x runs goal to goal, y touchline to touchline
constexpr float kHalfX = 55.0f;
constexpr float kHalfY = 36.0f;
}  // namespace

TEST(StagingAnchor, AStagingThatStartsOnThePitchIsLeftAlone) {
  // the anthem pack stands the squads on the halfway line to begin with
  const Vector3 offset = StagingAnchor::WalkOnOffset(Vector3(0.0f, -2.0f, 0.0f), kHalfX, kHalfY);
  EXPECT_FLOAT_EQ(offset.coords[0], 0.0f);
  EXPECT_FLOAT_EQ(offset.coords[1], 0.0f);
}

TEST(StagingAnchor, AWalkOnStartsJustOutsideTheLineItComesInOver) {
  // ent_009_st000 starts around y -48 and walks ten metres inward
  const Vector3 offset = StagingAnchor::WalkOnOffset(Vector3(-0.7f, -47.8f, 0.0f), kHalfX, kHalfY);
  EXPECT_FLOAT_EQ(offset.coords[0], 0.0f);  // nothing wrong with where it sits along the pitch
  const float startY = -47.8f + offset.coords[1];
  EXPECT_LT(startY, -kHalfY);                      // still outside, so they walk in
  EXPECT_GT(startY, -kHalfY - 8.0f);               // and only just
}

TEST(StagingAnchor, TheWalkEndsUpOnThePitch) {
  // the ten metres PES walks them has to finish inside the touchline
  const Vector3 offset = StagingAnchor::WalkOnOffset(Vector3(-0.7f, -47.8f, 0.0f), kHalfX, kHalfY);
  const float finishY = -37.9f + offset.coords[1];
  EXPECT_GT(finishY, -kHalfY);
  EXPECT_LT(finishY, 0.0f);
}

TEST(StagingAnchor, TheEdgesOfThePitchCountAsOnIt) {
  EXPECT_FLOAT_EQ(
      StagingAnchor::WalkOnOffset(Vector3(0.0f, -kHalfY + 1.0f, 0.0f), kHalfX, kHalfY).coords[1],
      0.0f);
  EXPECT_FLOAT_EQ(
      StagingAnchor::WalkOnOffset(Vector3(kHalfX - 1.0f, 0.0f, 0.0f), kHalfX, kHalfY).coords[0],
      0.0f);
}

TEST(StagingAnchor, AWalkOnFromBeyondAGoalLineComesInThatWay) {
  const Vector3 offset = StagingAnchor::WalkOnOffset(Vector3(-70.0f, 0.0f, 0.0f), kHalfX, kHalfY);
  const float startX = -70.0f + offset.coords[0];
  EXPECT_LT(startX, -kHalfX);
  EXPECT_GT(startX, -kHalfX - 8.0f);
  EXPECT_FLOAT_EQ(offset.coords[1], 0.0f);
}

TEST(StagingAnchor, TheHeightIsNeverTouched) {
  // The choreography's own z is what keeps feet on the ground.
  const Vector3 offset = StagingAnchor::WalkOnOffset(Vector3(0.0f, -48.0f, 3.2f), kHalfX, kHalfY);
  EXPECT_FLOAT_EQ(offset.coords[2], 0.0f);
}

TEST(StagingAnchor, ANonsensePitchLeavesEverythingWhereItIs) {
  // Better an unmoved staging than one translated by a garbage measurement.
  EXPECT_FLOAT_EQ(StagingAnchor::WalkOnOffset(Vector3(0.0f, -48.0f, 0.0f), 0.0f, 0.0f).coords[1],
                  0.0f);
}

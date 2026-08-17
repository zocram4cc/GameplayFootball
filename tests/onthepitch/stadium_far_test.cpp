// How far the camera has to see for a given stadium.
//
// The gameplay far plane is 200-250 m, which is plenty for a football stadium and
// not nearly enough for a pack whose surroundings are the view. Planet Namek's
// sky dome is 1154 m across and reaches 625 m up: entirely beyond the cap, so it
// was clipped away and what appeared instead was the engine's own gradient sky.
// The dome's texture is green everywhere, top to bottom - if we were seeing the
// dome at all, it would not be pale blue.
//
// Match already floors the far plane at 500 m when a separate skydome_object is
// loaded, for exactly this reason. A stadium that carries its sky in its own mesh
// needs the same treatment, and only the converter knows how far out that
// geometry goes - so it writes the distance beside the .object and the engine
// reads it.

#include <gtest/gtest.h>

#include "onthepitch/stadiumfar.hpp"

TEST(StadiumFar, TheSidecarSitsBesideTheStadiumObject) {
  EXPECT_EQ(StadiumFar::SidecarPath("media/objects/stadiums/pes_st017/pes_st017.object"),
            "media/objects/stadiums/pes_st017/farplane.txt");
  EXPECT_EQ(StadiumFar::SidecarPath("stadium.object"), "farplane.txt");
  EXPECT_EQ(StadiumFar::SidecarPath(""), "");
}

TEST(StadiumFar, AStadiumThatNeedsMoreDistanceGetsIt) {
  // Namek: geometry out to about 690 m
  EXPECT_FLOAT_EQ(StadiumFar::ChooseFarCap(250.0f, 690.0f), 690.0f);
}

TEST(StadiumFar, AStadiumThatNeedsLessKeepsTheGameplayCap) {
  // A normal stadium fits well inside it; widening the frustum for nothing only
  // costs depth precision.
  EXPECT_FLOAT_EQ(StadiumFar::ChooseFarCap(250.0f, 120.0f), 250.0f);
}

TEST(StadiumFar, NoSidecarChangesNothing) {
  EXPECT_FLOAT_EQ(StadiumFar::ChooseFarCap(250.0f, 0.0f), 250.0f);
  EXPECT_FLOAT_EQ(StadiumFar::ChooseFarCap(250.0f, -5.0f), 250.0f);
}

TEST(StadiumFar, AnAbsurdRequestIsCappedRatherThanWreckingDepthPrecision) {
  // A pack with one stray vertex a hundred kilometres out must not drag the far
  // plane with it; the depth buffer would lose the pitch.
  EXPECT_FLOAT_EQ(StadiumFar::ChooseFarCap(250.0f, 100000.0f), StadiumFar::kMaxFarCap);
  EXPECT_LE(StadiumFar::kMaxFarCap, 5000.0f);
}

TEST(StadiumFar, ReadingADistanceIgnoresWhatIsNotOne) {
  EXPECT_FLOAT_EQ(StadiumFar::ParseDistance("690.5"), 690.5f);
  EXPECT_FLOAT_EQ(StadiumFar::ParseDistance("  690  \n"), 690.0f);
  EXPECT_FLOAT_EQ(StadiumFar::ParseDistance(""), 0.0f);
  EXPECT_FLOAT_EQ(StadiumFar::ParseDistance("not a number"), 0.0f);
  EXPECT_FLOAT_EQ(StadiumFar::ParseDistance("-40"), 0.0f);
}

// Where a stadium's sun is.
//
// Match::SetRandomSunParams picks a direction at random for every kickoff -
// literally random(-1.7, 1.7) on two axes, with up to a tenth of jitter on the
// colour - so shadows fall a different way in every match and never the way the
// reference broadcast's do. PES does not guess: each ground ships
// light/#Win/light_st<slot>_af_fpkd_extracted/*.fox2.xml, which gives a place, a
// date and a time (Planet Namek: Buenos Aires, 8 April 2019, noon, with the
// ground turned 96 degrees off north) and so fixes the sun to the degree.
//
// The astronomy is done at import time - tools/pes21_import/stadium_lighting.py
// works it out and writes lighting.txt beside the stadium - so all the engine
// has to do is read a direction and use it instead of rolling dice.

#include <gtest/gtest.h>

#include "onthepitch/scenelighting.hpp"

TEST(SceneLighting, TheSidecarSitsBesideTheStadiumObject) {
  EXPECT_EQ(SceneLighting::SidecarPath("media/objects/stadiums/pes_st017/pes_st017.object"),
            "media/objects/stadiums/pes_st017/lighting.txt");
  EXPECT_EQ(SceneLighting::SidecarPath(""), "");
}

TEST(SceneLighting, NoSidecarLeavesTheEngineToItsOwnSun) {
  const SceneLighting::Sun sun = SceneLighting::Parse("");
  EXPECT_FALSE(sun.valid);
}

TEST(SceneLighting, NameksSunIsReadBack) {
  // what stadium_lighting.py computes for it: 46.5 degrees up, just west of
  // north, turned by the ground's own northAngle
  const SceneLighting::Sun sun = SceneLighting::Parse("sun -0.617 -0.306 0.725\nsun_lux 150000\n");
  ASSERT_TRUE(sun.valid);
  EXPECT_NEAR(sun.direction[0], -0.617f, 0.001f);
  EXPECT_NEAR(sun.direction[1], -0.306f, 0.001f);
  EXPECT_NEAR(sun.direction[2], 0.725f, 0.001f);
  EXPECT_NEAR(sun.lux, 150000.0f, 1.0f);
}

TEST(SceneLighting, CommentsAndBlankLinesAreIgnored) {
  const SceneLighting::Sun sun = SceneLighting::Parse(
      "# where this ground's sun is\n\nsun 0 0 1\n# and how bright\nsun_lux 90000\n");
  ASSERT_TRUE(sun.valid);
  EXPECT_NEAR(sun.direction[2], 1.0f, 0.001f);
}

TEST(SceneLighting, TheDirectionComesBackNormalised) {
  // The light is placed along this, so its length must not scale it.
  const SceneLighting::Sun sun = SceneLighting::Parse("sun 0 0 4\n");
  ASSERT_TRUE(sun.valid);
  EXPECT_NEAR(sun.direction[2], 1.0f, 0.001f);
}

TEST(SceneLighting, ASunBelowThePitchIsRefused) {
  // Underlighting a stadium shadows everything in it; better the engine's own.
  EXPECT_FALSE(SceneLighting::Parse("sun 0 0 -1\n").valid);
  EXPECT_FALSE(SceneLighting::Parse("sun 1 0 0\n").valid);
}

TEST(SceneLighting, RubbishIsRefusedRatherThanPointedSomewhere) {
  EXPECT_FALSE(SceneLighting::Parse("sun over there\n").valid);
  EXPECT_FALSE(SceneLighting::Parse("sun 0 0 0\n").valid);
  EXPECT_FALSE(SceneLighting::Parse("nonsense\n").valid);
}

TEST(SceneLighting, ALuxWithoutADirectionIsNotALighting) {
  EXPECT_FALSE(SceneLighting::Parse("sun_lux 150000\n").valid);
}

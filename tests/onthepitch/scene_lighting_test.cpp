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

#include <cmath>

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

// How much fog the ground wants. PES's atmosphere carries it per stadium, and
// Planet Namek's says none at all ("influenceOfFog" 0) - which matters, because
// this engine washes everything distant with a quarter of the horizon's colour,
// and on a green sky that turned the rock formations from their own colour into
// flat green. The reference broadcast has them pink in front of a green sky.
TEST(SceneLighting, WithoutASidecarTheEnginesOwnFogIsUnchanged) {
  EXPECT_FLOAT_EQ(SceneLighting::Parse("").fog, 1.0f);
}

TEST(SceneLighting, AGroundThatWantsNoFogGetsNone) {
  const SceneLighting::Sun sun = SceneLighting::Parse("sun 0 0 1\nfog 0\n");
  ASSERT_TRUE(sun.valid);
  EXPECT_FLOAT_EQ(sun.fog, 0.0f);
}

TEST(SceneLighting, APartialFogIsKeptAsGiven) {
  EXPECT_NEAR(SceneLighting::Parse("sun 0 0 1\nfog 0.35\n").fog, 0.35f, 0.001f);
}

TEST(SceneLighting, ANonsenseFogIsClampedRatherThanBelieved) {
  EXPECT_FLOAT_EQ(SceneLighting::Parse("sun 0 0 1\nfog -2\n").fog, 0.0f);
  EXPECT_FLOAT_EQ(SceneLighting::Parse("sun 0 0 1\nfog 5\n").fog, 1.0f);
  EXPECT_FLOAT_EQ(SceneLighting::Parse("sun 0 0 1\nfog lots\n").fog, 1.0f);
}

// A ground that ships no lighting at all.
//
// Six of the nine converted grounds came out of cpk extractions, which keep PES's
// binary atmosphere (.atsh/.rpd/.pcsp) rather than the readable XML the 4cc pack
// downloads carry, so there is no sidecar to import and the engine fell back to
// the dice roll - random(-1.7, 1.7) on x and y over a height multiplier of 1.3.
// That puts the sun near the zenith more often than not, which drives noonBias to
// 1 and lights the ground with the full cool noon sun straight overhead; those six
// were the ones that came out washed to white in the capture sheets, at medians of
// 0.55-0.57 against the broadcast's 0.434, while the three with a sidecar sat at
// 0.26-0.41 and kept their own colour.
//
// So the fallback is a fixed mid-afternoon sun rather than a guess: shadows fall
// the same way every kickoff, which is what a broadcast looks like, and no ground
// is lit from directly above unless PES says it is.
TEST(SceneLighting, WithoutASidecarTheSunIsFixedRatherThanRolled) {
  const SceneLighting::Sun first = SceneLighting::DefaultSun(0.0f);
  const SceneLighting::Sun second = SceneLighting::DefaultSun(0.0f);
  ASSERT_TRUE(first.valid);
  for (int i = 0; i < 3; ++i) EXPECT_FLOAT_EQ(first.direction[i], second.direction[i]);
}

TEST(SceneLighting, TheDefaultSunIsAMidAfternoonOne) {
  // Not overhead: an overhead sun is what flattened the stands to white. PES's
  // own grounds sit around 40-50 degrees, Namek's at 46.
  const SceneLighting::Sun sun = SceneLighting::DefaultSun(0.0f);
  const float elevation = std::asin(sun.direction[2]) * 180.0f / 3.14159265f;
  EXPECT_GT(elevation, 30.0f);
  EXPECT_LT(elevation, 55.0f);
}

TEST(SceneLighting, TheDefaultSunIsNormalised) {
  for (float timeOfDay : {0.0f, 0.5f, 1.0f}) {
    const SceneLighting::Sun sun = SceneLighting::DefaultSun(timeOfDay);
    const float length = std::sqrt(sun.direction[0] * sun.direction[0] +
                                  sun.direction[1] * sun.direction[1] +
                                  sun.direction[2] * sun.direction[2]);
    EXPECT_NEAR(length, 1.0f, 0.001f);
  }
}

TEST(SceneLighting, LaterInTheDayTheDefaultSunIsLower) {
  // The pre-match screen's time-of-day selector still has to mean something.
  EXPECT_LT(SceneLighting::DefaultSun(1.0f).direction[2],
            SceneLighting::DefaultSun(0.5f).direction[2]);
  EXPECT_LT(SceneLighting::DefaultSun(0.5f).direction[2],
            SceneLighting::DefaultSun(0.0f).direction[2]);
}

TEST(SceneLighting, TheDefaultSunStaysAboveTheHorizon) {
  // Even at night: the floodlights do the lighting then, and a sun below the
  // pitch shadows everything above it.
  EXPECT_GT(SceneLighting::DefaultSun(1.0f).direction[2], 0.0f);
}

TEST(SceneLighting, AGroundsOwnSunOutranksTheDefault) {
  const SceneLighting::Sun own = SceneLighting::Parse("sun -0.617 -0.306 0.725\n");
  ASSERT_TRUE(own.valid);
  EXPECT_NE(own.direction[0], SceneLighting::DefaultSun(0.0f).direction[0]);
}

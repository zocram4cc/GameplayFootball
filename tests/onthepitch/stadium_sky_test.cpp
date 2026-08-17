// A stadium's sky colours.
//
// The engine paints the empty background with a view-direction gradient in
// postprocess.frag, between two hardcoded blues, and fills the gap below the
// horizon with a near-white fog colour. That is why Planet Namek - whose sky is
// green overhead, yellow-green at the horizon, over teal water - reads as an
// overcast afternoon in England, and why everything past the pitch blows out white.
//
// The dome geometry is imported but is not being rasterised (its pixels measure as
// the fog fill, i.e. cleared depth). The gradient, on the other hand, demonstrably
// draws every frame - so the stadium's own colours are sampled from its sky texture
// at conversion time and the gradient is driven from those. No clouds and no moons,
// but the right sky.
//
// The defaults have to stay exactly what the shader had, so a stadium that ships no
// colours looks as it did.

#include <gtest/gtest.h>

#include "onthepitch/stadiumsky.hpp"

TEST(StadiumSky, TheSidecarSitsBesideTheStadiumObject) {
  EXPECT_EQ(StadiumSky::SidecarPath("media/objects/stadiums/pes_st017/pes_st017.object"),
            "media/objects/stadiums/pes_st017/sky.txt");
  EXPECT_EQ(StadiumSky::SidecarPath(""), "");
}

TEST(StadiumSky, WithoutASidecarTheEnginesOwnGradientIsUnchanged) {
  const StadiumSky::Colours defaults = StadiumSky::Parse("");
  EXPECT_FALSE(defaults.valid);
  // postprocess.frag's own constants, before any of this existed
  EXPECT_NEAR(defaults.zenith[0], 0.32f, 0.001f);
  EXPECT_NEAR(defaults.zenith[1], 0.52f, 0.001f);
  EXPECT_NEAR(defaults.zenith[2], 0.78f, 0.001f);
  EXPECT_NEAR(defaults.horizon[0], 0.78f, 0.001f);
  EXPECT_NEAR(defaults.horizon[1], 0.85f, 0.001f);
  EXPECT_NEAR(defaults.horizon[2], 0.93f, 0.001f);
}

TEST(StadiumSky, NameksColoursAreReadBack) {
  // sampled from namekbackground: green overhead, yellow-green at the horizon
  const StadiumSky::Colours sky =
      StadiumSky::Parse("zenith 0.013 0.440 0.001\nhorizon 0.277 0.708 0.112\n");
  ASSERT_TRUE(sky.valid);
  EXPECT_NEAR(sky.zenith[1], 0.440f, 0.001f);
  EXPECT_NEAR(sky.horizon[1], 0.708f, 0.001f);
  EXPECT_NEAR(sky.horizon[0], 0.277f, 0.001f);
}

TEST(StadiumSky, TheOrderOfTheLinesDoesNotMatter) {
  const StadiumSky::Colours sky =
      StadiumSky::Parse("horizon 0.2 0.7 0.1\nzenith 0.0 0.4 0.0\n");
  ASSERT_TRUE(sky.valid);
  EXPECT_NEAR(sky.zenith[1], 0.4f, 0.001f);
  EXPECT_NEAR(sky.horizon[1], 0.7f, 0.001f);
}

TEST(StadiumSky, HalfASidecarIsNotUsedAtAll) {
  // A zenith with no horizon would blend the stadium's sky into the engine's, which
  // is worse than either; better to keep the one that was designed.
  EXPECT_FALSE(StadiumSky::Parse("zenith 0.0 0.4 0.0\n").valid);
  EXPECT_FALSE(StadiumSky::Parse("horizon 0.2 0.7 0.1\n").valid);
}

TEST(StadiumSky, RubbishIsIgnoredRatherThanPaintedOnTheSky) {
  EXPECT_FALSE(StadiumSky::Parse("zenith lots of green\nhorizon a bit less\n").valid);
  EXPECT_FALSE(StadiumSky::Parse("nonsense\n").valid);
}

TEST(StadiumSky, ChannelsOutsideTheRangeAreClamped) {
  const StadiumSky::Colours sky =
      StadiumSky::Parse("zenith -1 2 0.5\nhorizon 0.5 0.5 0.5\n");
  ASSERT_TRUE(sky.valid);
  EXPECT_FLOAT_EQ(sky.zenith[0], 0.0f);
  EXPECT_FLOAT_EQ(sky.zenith[1], 1.0f);
}

TEST(StadiumSky, TheFogTakesTheHorizonSoTheGroundGapMatchesTheSky) {
  // The band below the horizon is filled with the fog colour; left near-white it is
  // the white void past Namek's pitch.
  const StadiumSky::Colours sky =
      StadiumSky::Parse("zenith 0.0 0.44 0.0\nhorizon 0.28 0.71 0.11\n");
  const float* fog = StadiumSky::FogColour(sky);
  EXPECT_NEAR(fog[1], 0.71f, 0.001f);
  const StadiumSky::Colours none = StadiumSky::Parse("");
  EXPECT_NEAR(StadiumSky::FogColour(none)[0], 0.85f, 0.001f);  // the shader's own
}

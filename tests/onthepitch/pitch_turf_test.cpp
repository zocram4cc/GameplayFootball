// Which seamless grass tile the procedural pitch is generated from.
//
// The pitch is not a texture the stadium supplies: proceduralpitch.cpp builds
// the diffuse, specular and normal maps at match start from a seamless grass
// tile, Perlin noise and the line-marking overlay, then overwrites the texture
// resources named pitch_0N.png with the result. That is why pointing a converted
// stadium's pitch.ase at its own turf does not work - the engine fetches
// pitch_0N.png by name, and with the name gone the fetch comes back empty and the
// match dies right after generating the bitmaps.
//
// So a stadium that wants its own colour - Planet Namek's pitch is teal blue, not
// green - has to hand its turf to the generator instead, by shipping it next to
// its .object as turf.png.

#include <gtest/gtest.h>

#include "onthepitch/pitchturf.hpp"

TEST(PitchTurf, WithoutAStadiumTheStockGrassIsUsed) {
  EXPECT_EQ(PitchTurf::GrassTexturePath("", true), PitchTurf::kStockGrassTexture);
  EXPECT_EQ(PitchTurf::GrassTexturePath("", false), PitchTurf::kStockGrassTexture);
}

TEST(PitchTurf, AStadiumThatShipsTurfNextToItsObjectProvidesIt) {
  EXPECT_EQ(PitchTurf::GrassTexturePath(
                "media/objects/stadiums/pes_st017/pes_st017.object", true),
            "media/objects/stadiums/pes_st017/turf.png");
}

TEST(PitchTurf, AStadiumWithoutTurfFallsBackToTheStockGrass) {
  EXPECT_EQ(PitchTurf::GrassTexturePath(
                "media/objects/stadiums/pes_st060/pes_st060.object", false),
            PitchTurf::kStockGrassTexture);
}

TEST(PitchTurf, ACandidateIsOfferedEvenForAnObjectInTheCurrentDirectory) {
  // No directory part: the candidate is turf.png beside it, not "/turf.png".
  EXPECT_EQ(PitchTurf::GrassTexturePath("stadium.object", true), "turf.png");
}

TEST(PitchTurf, TheCandidateIsWhatTheCallerHasToTestForExistence) {
  // The caller checks the filesystem, so the candidate has to be derivable
  // without one - otherwise this could not be a pure function at all.
  EXPECT_EQ(PitchTurf::TurfCandidate("media/objects/stadiums/x/y.object"),
            "media/objects/stadiums/x/turf.png");
  EXPECT_EQ(PitchTurf::TurfCandidate(""), "");
}

// The pitch's base colour, which is where the green came from.
//
// GetPitchDiffuseColor built it from constants - g = 46 * brightness - and then
// blended the seamless tile in at only 30%, so handing the generator Namek's teal
// turf still produced a green pitch. When a stadium supplies its own turf, that
// turf's own colour is the pitch's colour; the generator still lays the noise,
// the fake ambient occlusion and the line markings over it.

TEST(PitchBaseColour, WithoutAStadiumTurfTheEnginesOwnGreenIsUnchanged) {
  // the original arithmetic: contrast 0.4, brightness 2, ratio 0.5 -> rToB 1
  const PitchTurf::Colour c = PitchTurf::BaseColour(false, 0, 0, 0, 0.5f);
  EXPECT_NEAR(c.r, (35 - 0.4f * 10) * 1.0f * 2.0f, 0.01f);
  EXPECT_NEAR(c.g, 46 * 2.0f, 0.01f);
  EXPECT_NEAR(c.b, (25 - 0.4f * 10) * 1.0f * 2.0f, 0.01f);
}

TEST(PitchBaseColour, TheRedToBlueRatioStillTiltsTheEnginesGreen) {
  const PitchTurf::Colour redder = PitchTurf::BaseColour(false, 0, 0, 0, 1.0f);
  const PitchTurf::Colour bluer = PitchTurf::BaseColour(false, 0, 0, 0, 0.0f);
  EXPECT_GT(redder.r, bluer.r);
  EXPECT_LT(redder.b, bluer.b);
}

TEST(PitchBaseColour, AStadiumTurfIsTakenAtItsOwnColour) {
  // Namek's turf averages a teal; the pitch has to come out teal, not a green
  // pitch with a hint of teal.
  const PitchTurf::Colour c = PitchTurf::BaseColour(true, 24.0f, 95.0f, 110.0f, 0.5f);
  EXPECT_NEAR(c.r, 24.0f, 0.01f);
  EXPECT_NEAR(c.g, 95.0f, 0.01f);
  EXPECT_NEAR(c.b, 110.0f, 0.01f);
}

TEST(PitchBaseColour, AStadiumTurfIgnoresTheRedToBlueRatio) {
  // That setting exists to tune GF's own grass; it must not repaint an imported
  // stadium's turf.
  const PitchTurf::Colour a = PitchTurf::BaseColour(true, 24.0f, 95.0f, 110.0f, 0.0f);
  const PitchTurf::Colour b = PitchTurf::BaseColour(true, 24.0f, 95.0f, 110.0f, 2.0f);
  EXPECT_NEAR(a.r, b.r, 0.01f);
  EXPECT_NEAR(a.b, b.b, 0.01f);
}

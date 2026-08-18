// Which pitch art a ground gets painted with.
//
// The engine grows its pitch procedurally and then blends one image over the whole
// of it by that image's alpha (proceduralpitch.cpp: the overlay is sampled across
// pitchFullHalfW/H, so 60 by 40 metres either way). That image was one file for
// every stadium, media/textures/pitch/overlay.png - the same crest, wear and
// mowing on Planet Namek as on a Buenos Aires ground.
//
// PES paints its own on every pitch, and the converter now rasterises it out of
// the pack's own pitch model into pitch_overlay.png beside the stadium, the way
// lighting.txt, sky.txt and farplane.txt already sit there. So a ground that
// brought its own art gets it, and one that did not keeps the shared file.

#include <gtest/gtest.h>

#include "onthepitch/pitchoverlay.hpp"

TEST(PitchOverlay, TheSidecarSitsBesideTheStadiumObject) {
  EXPECT_EQ(PitchOverlay::SidecarPath("media/objects/stadiums/pes_st002/pes_st002.object"),
            "media/objects/stadiums/pes_st002/pitch_overlay.png");
}

TEST(PitchOverlay, AWindowsPathIsUnderstoodToo) {
  EXPECT_EQ(PitchOverlay::SidecarPath("media\\objects\\stadiums\\pes_st017\\pes_st017.object"),
            "media\\objects\\stadiums\\pes_st017\\pitch_overlay.png");
}

TEST(PitchOverlay, NoStadiumIsNoSidecar) {
  EXPECT_EQ(PitchOverlay::SidecarPath(""), "");
  EXPECT_EQ(PitchOverlay::SidecarPath("pes_st002.object"), "pitch_overlay.png");
}

TEST(PitchOverlay, AGroundWithItsOwnArtGetsIt) {
  EXPECT_EQ(PitchOverlay::Choose("media/objects/stadiums/pes_st002/pitch_overlay.png", true),
            "media/objects/stadiums/pes_st002/pitch_overlay.png");
}

TEST(PitchOverlay, AGroundWithoutOneKeepsTheSharedFile) {
  EXPECT_EQ(PitchOverlay::Choose("media/objects/stadiums/pes_st011/pitch_overlay.png", false),
            PitchOverlay::kSharedOverlay);
  EXPECT_EQ(PitchOverlay::Choose("", false), PitchOverlay::kSharedOverlay);
}

TEST(PitchOverlay, AMissingStadiumNeverPointsAtNothing) {
  // A path that does not exist must not reach IMG_Load: no overlay is fatal there.
  EXPECT_EQ(PitchOverlay::Choose("", true), PitchOverlay::kSharedOverlay);
}

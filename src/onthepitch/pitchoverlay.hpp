// Which pitch art a ground gets painted with.
//
// The engine grows its pitch procedurally and then blends one image over the whole
// of it by that image's alpha - proceduralpitch.cpp samples the overlay across
// pitchFullHalfW/H, 60 by 40 metres either way, so it covers the field and its
// rim. Until now that was one file for every stadium: the same crest, the same
// worn goalmouths on every ground in the game.
//
// PES paints its own on every pitch, in the pack's own pitch model - a flat sheet
// with the mowing bands, the wear and the club's crest in decal textures over it.
// tools/pes21_import/pitch_overlay.py rasterises those into pitch_overlay.png
// beside the stadium, where lighting.txt, sky.txt and farplane.txt already live.

#ifndef _HPP_ONTHEPITCH_PITCHOVERLAY
#define _HPP_ONTHEPITCH_PITCHOVERLAY

#include <string>

namespace PitchOverlay {

// What every ground used to be painted with, and still is without its own.
extern const char* kSharedOverlay;

// pitch_overlay.png beside the given stadium object, or "" when there is no stadium.
std::string SidecarPath(const std::string& stadiumObjectPath);

// The image to paint with: the stadium's own when it brought one.
std::string Choose(const std::string& sidecarPath, bool sidecarExists);

}  // namespace PitchOverlay

#endif

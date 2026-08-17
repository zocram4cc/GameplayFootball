// Which seamless grass tile the procedural pitch is built from.
//
// proceduralpitch.cpp generates the pitch's diffuse, specular and normal maps at
// match start from a seamless grass tile, Perlin noise and the line-marking
// overlay, then overwrites the texture resources the pitch geometry already
// loaded under the names pitch_0N.png. A stadium therefore cannot simply point
// its pitch materials at its own turf - the generator would not find the names it
// overwrites, and nothing would be drawn.
//
// What a stadium can do is hand its turf to the generator, by shipping it beside
// its .object as turf.png. PES stadiums carry their own ground colour
// (<stadium>_turf000_bsm), and Planet Namek's is teal blue rather than green.

#ifndef _HPP_ONTHEPITCH_PITCHTURF
#define _HPP_ONTHEPITCH_PITCHTURF

#include <string>

namespace PitchTurf {

extern const char* const kStockGrassTexture;

// turf.png beside the given stadium object, or "" if there is no stadium.
std::string TurfCandidate(const std::string& stadiumObjectPath);

// The stadium's own turf when it ships one, the stock grass otherwise. The
// caller tests the candidate for existence, so this stays free of the filesystem.
std::string GrassTexturePath(const std::string& stadiumObjectPath, bool stadiumTurfExists);

struct Colour {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

// The pitch's base colour, before the generator lays noise, fake ambient
// occlusion and the line markings over it.
//
// GF's own grass is a built-in green tilted by "graphics_pitchredtoblueratio". A
// stadium that ships its own turf is taken at that turf's average colour instead:
// blending it in at the generator's 30% left Planet Namek's teal pitch green.
// turfR/turfG/turfB are 0..255 channel averages of the turf tile.
Colour BaseColour(bool haveStadiumTurf, float turfR, float turfG, float turfB,
                  float redToBlueRatio);

}  // namespace PitchTurf

#endif

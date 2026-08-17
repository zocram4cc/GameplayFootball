// A stadium's own sky colours.
//
// postprocess.frag paints the empty background with a view-direction gradient
// between two blues, and fills the band below the horizon with a near-white fog
// colour. On Planet Namek - green overhead, yellow-green at the horizon, teal water
// around the pitch - that reads as an overcast afternoon somewhere else, and
// everything past the pitch blows out white.
//
// The pack's own sky dome is imported but is not being rasterised; the gradient
// draws every frame. So the converter samples the dome's texture and writes the two
// colours beside the .object as sky.txt, and the gradient is driven from them:
//
//   zenith  <r> <g> <b>
//   horizon <r> <g> <b>
//
// Without a sidecar the defaults are exactly the constants the shader carried, so
// nothing that worked before changes.

#ifndef _HPP_ONTHEPITCH_STADIUMSKY
#define _HPP_ONTHEPITCH_STADIUMSKY

#include <string>

namespace StadiumSky {

struct Colours {
  float zenith[3];
  float horizon[3];
  bool valid = false;  // false: the engine's own gradient, untouched
};

std::string SidecarPath(const std::string& stadiumObjectPath);

// Both lines are needed; anything missing or unreadable leaves valid false.
Colours Parse(const std::string& text);

// What to fill the band below the horizon with: the stadium's horizon when it has
// one, the shader's own near-white otherwise.
const float* FogColour(const Colours& colours);

}  // namespace StadiumSky

#endif

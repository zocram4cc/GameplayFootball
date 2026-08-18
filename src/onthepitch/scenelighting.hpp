// Where a stadium's sun is.
//
// Match::SetRandomSunParams picks a direction at random for every kickoff -
// random(-1.7, 1.7) on two axes, with up to a tenth of jitter on the colour - so
// shadows fall a different way in every match, and never the way the reference
// broadcast's do. PES does not guess. Each ground ships
// light/#Win/light_st<slot>_af_fpkd_extracted/*.fox2.xml, which gives a place, a
// date and a time - Planet Namek's is Buenos Aires, 8 April 2019, noon, with the
// ground turned 96 degrees off north - and that fixes the sun to the degree.
//
// tools/pes21_import/stadium_lighting.py does the astronomy at import time and
// writes lighting.txt beside the stadium's .object, so all the engine reads is a
// direction:
//
//     sun -0.617 -0.306 0.725
//     sun_lux 150000
//     fog 0

#ifndef _HPP_ONTHEPITCH_SCENELIGHTING
#define _HPP_ONTHEPITCH_SCENELIGHTING

#include <string>

namespace SceneLighting {

struct Sun {
  // Towards the sun, normalised, in the engine's axes (z up).
  float direction[3] = {0.0f, 0.0f, 1.0f};
  // PES's own illuminance for it. Kept for anyone who wants to drive exposure
  // from it; the engine's brightness model is its own.
  float lux = 0.0f;
  // How much of the engine's own fog this ground wants, 0..1. It washes
  // everything distant with a quarter of the horizon's colour, which on a green
  // sky turned Namek's rock formations from their own colour into flat green -
  // and PES's atmosphere for that ground asks for no fog at all. 1 leaves the
  // engine as it was.
  float fog = 1.0f;
  bool valid = false;
};

// lighting.txt beside the given .object; empty for an empty path.
std::string SidecarPath(const std::string& stadiumObjectPath);

// A sun that is missing, malformed, or below the horizon is not valid: the
// engine keeps its own rather than underlighting the ground.
Sun Parse(const std::string& text);

// Where the sun goes for a ground that ships no lighting of its own - six of the
// nine converted ones, whose packs came out of cpk extractions and so kept PES's
// binary atmosphere instead of the readable XML. The engine used to roll
// random(-1.7, 1.7) on two axes over a height multiplier of 1.3, which puts the
// sun near the zenith more often than not and washed those six to white. This is
// a fixed mid-afternoon sun instead: the same shadows every kickoff, and never
// straight overhead. timeOfDay is the pre-match selector, 0 day .. 1 night, and
// only lowers it.
Sun DefaultSun(float timeOfDay);

}  // namespace SceneLighting

#endif

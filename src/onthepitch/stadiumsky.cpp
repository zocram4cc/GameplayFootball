#include "onthepitch/stadiumsky.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace StadiumSky {

namespace {

// postprocess.frag's own constants
const float kDefaultZenith[3] = {0.32f, 0.52f, 0.78f};
const float kDefaultHorizon[3] = {0.78f, 0.85f, 0.93f};
const float kDefaultFog[3] = {0.85f, 0.85f, 0.9f};

float Clamp01(float value) { return std::max(0.0f, std::min(value, 1.0f)); }

bool ReadTriple(std::istringstream& line, float out[3]) {
  for (int i = 0; i < 3; i++) {
    if (!(line >> out[i])) return false;
    out[i] = Clamp01(out[i]);
  }
  return true;
}

}  // namespace

std::string SidecarPath(const std::string& stadiumObjectPath) {
  if (stadiumObjectPath.empty()) return "";
  const std::string::size_type slash = stadiumObjectPath.find_last_of("/\\");
  if (slash == std::string::npos) return "sky.txt";
  return stadiumObjectPath.substr(0, slash + 1) + "sky.txt";
}

Colours Parse(const std::string& text) {
  Colours colours;
  for (int i = 0; i < 3; i++) {
    colours.zenith[i] = kDefaultZenith[i];
    colours.horizon[i] = kDefaultHorizon[i];
  }
  bool haveZenith = false, haveHorizon = false;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string what;
    if (!(fields >> what)) continue;
    if (what == "zenith")
      haveZenith = ReadTriple(fields, colours.zenith);
    else if (what == "horizon")
      haveHorizon = ReadTriple(fields, colours.horizon);
  }
  // Half a sidecar would blend one stadium's sky into the engine's; keep the
  // designed one instead.
  colours.valid = haveZenith && haveHorizon;
  if (!colours.valid) {
    for (int i = 0; i < 3; i++) {
      colours.zenith[i] = kDefaultZenith[i];
      colours.horizon[i] = kDefaultHorizon[i];
    }
  }
  return colours;
}

const float* FogColour(const Colours& colours) {
  return colours.valid ? colours.horizon : kDefaultFog;
}

}  // namespace StadiumSky

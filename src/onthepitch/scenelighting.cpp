#include "scenelighting.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace SceneLighting {

std::string SidecarPath(const std::string& stadiumObjectPath) {
  if (stadiumObjectPath.empty()) return "";
  const size_t slash = stadiumObjectPath.find_last_of("/\\");
  if (slash == std::string::npos) return "lighting.txt";
  return stadiumObjectPath.substr(0, slash + 1) + "lighting.txt";
}

Sun Parse(const std::string& text) {
  Sun sun;
  std::istringstream lines(text);
  std::string line;
  bool haveDirection = false;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string key;
    if (!(fields >> key) || key.empty() || key[0] == '#') continue;
    if (key == "sun") {
      float x = 0, y = 0, z = 0;
      if (!(fields >> x >> y >> z)) continue;
      const float length = std::sqrt(x * x + y * y + z * z);
      // Nothing to point at, or pointing up from under the pitch: a light there
      // shadows everything in the ground.
      if (length < 1e-4f || z <= 0.0f) continue;
      sun.direction[0] = x / length;
      sun.direction[1] = y / length;
      sun.direction[2] = z / length;
      haveDirection = true;
    } else if (key == "sun_lux") {
      float lux = 0;
      if (fields >> lux) sun.lux = lux;
    } else if (key == "fog") {
      float fog = 1.0f;
      if (fields >> fog) sun.fog = std::max(0.0f, std::min(1.0f, fog));
    }
  }
  sun.valid = haveDirection;
  return sun;
}

Sun DefaultSun(float timeOfDay) {
  // Where PES puts a sun when it is not saying otherwise: across the ground
  // rather than down it, and at the elevation its own atmospheres sit around -
  // Namek's works out at 46 degrees, and the grounds that came out right in the
  // capture sheets were the ones lit like that. Bearing is kept off the main
  // camera's axis so players are lit from the front three-quarters, which is what
  // the old dice roll was reaching for when it flipped the sun to the camera side.
  const float day = std::max(0.0f, std::min(1.0f, timeOfDay));
  const float kDayElevation = 44.0f;
  const float kNightElevation = 6.0f;  // low, dim, and the floodlights take over
  const float elevation = (kDayElevation + (kNightElevation - kDayElevation) * day) *
                          3.14159265358979f / 180.0f;
  const float bearing = -130.0f * 3.14159265358979f / 180.0f;

  Sun sun;
  const float horizontal = std::cos(elevation);
  sun.direction[0] = horizontal * std::sin(bearing);
  sun.direction[1] = horizontal * std::cos(bearing);
  sun.direction[2] = std::sin(elevation);
  sun.valid = true;
  return sun;
}

}  // namespace SceneLighting

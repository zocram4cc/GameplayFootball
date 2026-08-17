#include "scenelighting.hpp"

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
    }
  }
  sun.valid = haveDirection;
  return sun;
}

}  // namespace SceneLighting

#include "onthepitch/stadiumfar.hpp"

#include <algorithm>
#include <cstdlib>

namespace StadiumFar {

const float kMaxFarCap = 2000.0f;

std::string SidecarPath(const std::string& stadiumObjectPath) {
  if (stadiumObjectPath.empty()) return "";
  const std::string::size_type slash = stadiumObjectPath.find_last_of("/\\");
  if (slash == std::string::npos) return "farplane.txt";
  return stadiumObjectPath.substr(0, slash + 1) + "farplane.txt";
}

float ParseDistance(const std::string& text) {
  if (text.empty()) return 0.0f;
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str()) return 0.0f;  // nothing numeric at all
  if (value <= 0.0) return 0.0f;
  return static_cast<float>(value);
}

float ChooseFarCap(float configuredCap, float stadiumNeeds) {
  if (stadiumNeeds <= 0.0f) return configuredCap;
  return std::max(configuredCap, std::min(stadiumNeeds, kMaxFarCap));
}

}  // namespace StadiumFar

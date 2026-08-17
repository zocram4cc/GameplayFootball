#include "onthepitch/pitchturf.hpp"

namespace PitchTurf {

const char* const kStockGrassTexture = "media/textures/pitch/seamlessgrass08.png";

std::string TurfCandidate(const std::string& stadiumObjectPath) {
  if (stadiumObjectPath.empty()) return "";
  const std::string::size_type slash = stadiumObjectPath.find_last_of("/\\");
  if (slash == std::string::npos) return "turf.png";
  return stadiumObjectPath.substr(0, slash + 1) + "turf.png";
}

std::string GrassTexturePath(const std::string& stadiumObjectPath, bool stadiumTurfExists) {
  const std::string candidate = TurfCandidate(stadiumObjectPath);
  if (candidate.empty() || !stadiumTurfExists) return kStockGrassTexture;
  return candidate;
}

Colour BaseColour(bool haveStadiumTurf, float turfR, float turfG, float turfB,
                  float redToBlueRatio) {
  Colour colour;
  if (haveStadiumTurf) {
    // the turf's own colour, at its own level - the ratio is for GF's grass
    colour.r = turfR;
    colour.g = turfG;
    colour.b = turfB;
    return colour;
  }
  // GF's built-in green, exactly as GetPitchDiffuseColor computed it
  const float contrast = 0.4f;  // g <=> rb contrast; higher is greener
  const float brightness = 2.0f;
  const float rToB = redToBlueRatio * 2.0f;  // 0..2, higher is more red
  colour.r = ((35 - contrast * 10) * rToB) * brightness;
  colour.g = 46 * brightness;
  colour.b = ((25 - contrast * 10) * (2.0f - rToB)) * brightness;
  return colour;
}

}  // namespace PitchTurf

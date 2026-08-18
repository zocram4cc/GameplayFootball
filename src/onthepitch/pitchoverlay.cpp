#include "onthepitch/pitchoverlay.hpp"

namespace PitchOverlay {

const char* kSharedOverlay = "media/textures/pitch/overlay.png";

std::string SidecarPath(const std::string& stadiumObjectPath) {
  if (stadiumObjectPath.empty()) return "";
  const std::string::size_type slash = stadiumObjectPath.find_last_of("/\\");
  if (slash == std::string::npos) return "pitch_overlay.png";
  return stadiumObjectPath.substr(0, slash + 1) + "pitch_overlay.png";
}

std::string Choose(const std::string& sidecarPath, bool sidecarExists) {
  if (sidecarPath.empty() || !sidecarExists) return kSharedOverlay;
  return sidecarPath;
}

}  // namespace PitchOverlay

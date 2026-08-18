#include "prematchshotpair.hpp"

namespace PrematchShotPair {

std::string StagingForCamera(const std::string& cameraName) {
  std::string name = cameraName;
  const size_t slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);

  // "_cam" ends the shot's own name; anything after it numbers the angle
  // (ent_020_st002_cam_1), and every angle films the same players.
  const size_t cam = name.rfind("_cam");
  if (cam == std::string::npos) return "";
  return name.substr(0, cam) + "_pl";
}

}  // namespace PrematchShotPair

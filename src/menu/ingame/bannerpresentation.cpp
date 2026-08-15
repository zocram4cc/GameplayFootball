#include "bannerpresentation.hpp"

#include <algorithm>

namespace BannerPresentation {

Slot SlotForTeam(int teamID) {
  if (teamID == 0) return Slot::Left;
  if (teamID == 1) return Slot::Right;
  return Slot::Center;
}

blunted::Vector3 AccentColor(int teamID, const blunted::Vector3& team0Color,
                             const blunted::Vector3& team1Color) {
  if (teamID == 0) return team0Color;
  if (teamID == 1) return team1Color;
  return blunted::Vector3(210, 175, 60);  // neutral gold, matches referee/commentary cues
}

float FadeAlpha(long elapsedSinceShown_ms, long remainingUntilHide_ms, unsigned long fadeIn_ms,
                unsigned long fadeOut_ms) {
  if (remainingUntilHide_ms < 0 || elapsedSinceShown_ms < 0) return 0.0f;
  float a = 1.0f;
  if (fadeIn_ms > 0 && (unsigned long)elapsedSinceShown_ms < fadeIn_ms)
    a = std::min(a, elapsedSinceShown_ms / (float)fadeIn_ms);
  if (fadeOut_ms > 0 && (unsigned long)remainingUntilHide_ms < fadeOut_ms)
    a = std::min(a, remainingUntilHide_ms / (float)fadeOut_ms);
  return std::max(0.0f, std::min(1.0f, a));
}

}  // namespace BannerPresentation

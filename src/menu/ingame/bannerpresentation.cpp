#include "bannerpresentation.hpp"

#include <algorithm>

namespace BannerPresentation {

Rect NotificationRect(float scoreboardX, float scoreboardY, float scoreboardHeight,
                      float widestLineWidth, int lineCount, float lineHeight) {
  constexpr float kGapBelowScoreboard = 0.9f;
  constexpr float kVerticalPadding = 0.5f;  // above and below the text block

  Rect rect;
  rect.x = scoreboardX;
  rect.y = scoreboardY + scoreboardHeight + kGapBelowScoreboard;
  rect.width = std::max(kNotificationMinWidth,
                        std::min(kNotificationMaxWidth,
                                 std::max(0.0f, widestLineWidth) + kNotificationChromeWidth));
  rect.height = std::max(0, lineCount) * lineHeight + 2.0f * kVerticalPadding;
  return rect;
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

// Pure-logic helpers for the in-match lower-third banner (see
// docs/PRESENTATION_SPEC.md section 4: tactical/instruction banners,
// substitution announcements, referee decisions). Kept as free functions
// over plain data so slot/colour selection is unit-testable; the widget
// (banner.hpp/.cpp) owns the actual GUI2 views and timers.

#ifndef _HPP_MENU_INGAME_BANNERPRESENTATION
#define _HPP_MENU_INGAME_BANNERPRESENTATION

#include "../../base/math/vector3.hpp"

namespace BannerPresentation {

// Every message - team-tagged or not - goes to one notification strip under the
// scoreboard, top-left. The bottom of the screen is taken: the player indicator
// bottom-left, its opposite number bottom-right, the radar bottom-centre. The
// three lower-thirds that used to live there sat on top of all of it, and a
// substitution drew two of them at once.
struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// Room for the accent tab and the padding either side of the text.
constexpr float kNotificationChromeWidth = 2.2f;
// A one-word message still needs to read as a panel rather than a blob...
constexpr float kNotificationMinWidth = 14.0f;
// ...and a squad file full of long names must not push it across the pitch.
constexpr float kNotificationMaxWidth = 44.0f;

// Left-aligned with the scoreboard and just below it, sized to its content:
// `widestLineWidth` is the measured width of the longest line of text, in
// percent of screen width, and the panel is that plus its chrome (clamped).
Rect NotificationRect(float scoreboardX, float scoreboardY, float scoreboardHeight,
                      float widestLineWidth, int lineCount, float lineHeight);

// team0Color/team1Color are used verbatim for teamID 0/1; teamID -1 (no
// team) gets a fixed neutral gold, matching a referee/commentary cue rather
// than either side's identity.
blunted::Vector3 AccentColor(int teamID, const blunted::Vector3& team0Color,
                             const blunted::Vector3& team1Color);

// Cross-fade alpha (0..1) for a banner that has been visible for
// `elapsedSinceShown_ms` and is due to hide in `remainingUntilHide_ms`: ramps
// up over the first `fadeIn_ms`, ramps down over the last `fadeOut_ms`, and
// is clamped to [0, 1]. `remainingUntilHide_ms < 0` (already past its hide
// time) returns 0.
float FadeAlpha(long elapsedSinceShown_ms, long remainingUntilHide_ms, unsigned long fadeIn_ms,
                unsigned long fadeOut_ms);

}  // namespace BannerPresentation

#endif

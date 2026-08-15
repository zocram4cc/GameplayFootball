// Pure-logic helpers for the in-match lower-third banner (see
// docs/PRESENTATION_SPEC.md section 4: tactical/instruction banners,
// substitution announcements, referee decisions). Kept as free functions
// over plain data so slot/colour selection is unit-testable; the widget
// (banner.hpp/.cpp) owns the actual GUI2 views and timers.

#ifndef _HPP_MENU_INGAME_BANNERPRESENTATION
#define _HPP_MENU_INGAME_BANNERPRESENTATION

#include "../../base/math/vector3.hpp"

namespace BannerPresentation {

// Mirrors the persistent player-HUD convention observed in the reference
// broadcast (team A bottom-left, team B bottom-right); a team-less message
// (goal commentary, offside, advantage) takes the bottom-centre slot so it
// never collides with either team's banner.
enum class Slot { Left, Center, Right };

Slot SlotForTeam(int teamID);  // -1 => Center, 0 => Left, 1 => Right

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

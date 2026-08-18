// Whose badge the crowd's stand flags fly.
//
// PES's stand flags - mob_prop_teamflag_home01..05 and away01, held up one seat in
// sixty (tools/pes21_import/stadium_crowd.py) - carry a texture called
// sys_zero_bsm. That is not a zero: it is a placeholder PES swaps at run time, and
// every model ships a different picture under the same filename. The flag bearers'
// is the flag of the United States; the tunnel arch's and the stand flags' are both
// the FC Barcelona crest. Imported verbatim, every crowd in every converted ground
// flew Barcelona.
//
// The importer leaves the placeholder behind and points the material at the
// engine's own neutral cloth instead - media/textures/stadium/teamflag_home.png or
// teamflag_away.png. This says which side a piece of cloth belongs to, so the match
// can paint the right team's badge over it, which is what PES does with it too.

#ifndef _HPP_ONTHEPITCH_TEAMFLAG
#define _HPP_ONTHEPITCH_TEAMFLAG

#include <string>

namespace TeamFlag {

enum Side {
  e_NotAFlag = 0,
  e_Home,
  e_Away,
};

// Which side a texture path belongs to, by the name the importer gave it. "away"
// wins where both words appear: the file is named for what it is, not for where it
// happens to be kept.
Side SideOf(const std::string& texturePath);

// The badge to paint on it: a team's own logo, or empty for a team that has none -
// in which case the cloth is left as it is rather than flying somebody else's crest.
std::string BadgeFor(const std::string& teamLogoPath);

}  // namespace TeamFlag

#endif

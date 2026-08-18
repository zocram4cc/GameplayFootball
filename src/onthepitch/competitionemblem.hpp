// Which competition's emblem rings the centre circle.
//
// PES sets a ring of pennant bearers on the centre circle for a competition tie -
// circleflag_afc_cl_01, four flag faces and four bearers - and the 4cc mod does the
// same to its own UEFA slot. The face carries the competition's badge, and these
// packs ship both as plain PNGs in common/render/symbol/emblemLc: emb_0004 is the
// 4chan Stupor Cup, the four-leaf clover, and emb_0008 the /vg/ Football League
// crest.
//
// Which one a tie flies follows from who is playing. The 4chan cup's teams are
// boards - /a/, /b/, /int/ - and the /vg/ league's are games: LCG, 2HUG. So two
// boards fly the clover and anything else flies the league. "competition_emblem"
// overrides it by name.

#ifndef _HPP_ONTHEPITCH_COMPETITIONEMBLEM
#define _HPP_ONTHEPITCH_COMPETITIONEMBLEM

#include <string>

namespace CompetitionEmblem {

// Whether a team name is a board tag: slashes round a short word.
bool IsBoard(const std::string& teamName);

// "4cc" for two boards, "vgl" for anything else.
std::string ForTeams(const std::string& homeName, const std::string& awayName);

// entrance/pennant_<emblem>.object beside the given stadium; "" when there is no
// stadium, or when the emblem is not a plain name (it only ever names a file).
std::string ObjectPath(const std::string& stadiumObjectPath, const std::string& emblem);

}  // namespace CompetitionEmblem

#endif

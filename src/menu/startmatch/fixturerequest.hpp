// Which fixture an unattended run plays.
//
// With menu_smoke_test_full_match set, the team-select page drives itself and
// would otherwise kick off whatever the selectors default to. A run that is
// recording something in particular - a stadium showcase, a reference
// comparison - names the two sides by database id instead:
//
//     "showcase_team1" "10"
//     "showcase_team2" "9"
//
// An id that is not in the selected league is ignored rather than guessed at,
// so a stale config plays the default fixture instead of the wrong one.

#ifndef _HPP_MENU_STARTMATCH_FIXTUREREQUEST
#define _HPP_MENU_STARTMATCH_FIXTUREREQUEST

#include <string>
#include <vector>

namespace FixtureRequest {

// The index of `wantedID` among the selector's entry ids, or -1 to leave the
// selector on whatever it had. An empty request never matches, including
// against the "no teams found" placeholder's empty id.
int EntryIndexForTeam(const std::vector<std::string>& entryIDs, const std::string& wantedID);

}  // namespace FixtureRequest

#endif

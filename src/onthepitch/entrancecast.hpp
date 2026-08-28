// Who is on the pitch during the entrance, and who is parked.
//
// The cast of a staging is posed by its choreography; the rest of the two squads has
// no business being visible until they walk on. That used to be done by calling
// HumanoidBase::Hide() on them every frame, which only parks the model at
// (1000, 1000, -1000) - and the player's own UpdateFullbodyNodes puts it straight
// back, since that follows the humanoid node. The two run on different schedules, so
// whichever won the frame decided whether the player was on screen: in the recorded
// match the squads blinked in and out of the centre circle every frame or two.
//
// Being parked is therefore a state the humanoid holds (SetBenched), and this is the
// decision that sets it: one answer per player per frame, asked and applied whether
// the answer is yes or no, so it cannot get stuck and there is nothing to race.

#ifndef _HPP_ONTHEPITCH_ENTRANCECAST
#define _HPP_ONTHEPITCH_ENTRANCECAST

#include <string>

namespace EntranceCast {

// isStaged: this player is one of the choreography's actors.
// holdingOpeningFrame: the establishing beat, which looks out over an empty pitch.
bool ShouldBench(bool inEntrance, bool holdingOpeningFrame, bool isStaged);

// The stadium's code out of its object path: "…/pes_st017/pes_st017.object" -> st017.
//
// PES names its entrance camera tracks after the ground they were authored for, so
// this is what decides whether a shot belongs to the stadium being played. Reading
// it as "the first st in the path" found the one in "stadiums" and produced "stadi",
// which matches no track ever written - so every ground was filmed with whatever
// shot happened to sort first, and on Planet Namek that put the lens in among the
// walking players.
std::string StadiumToken(const std::string& stadiumObjectPath);
}  // namespace EntranceCast

#endif

// Law 11 helpers, kept as free functions over plain data so they can be tested
// headlessly (docs/RULESET_AUDIT.md section 3). The Referee owns the whistle;
// these own the rule.

#ifndef _HPP_OFFSIDE_RULE
#define _HPP_OFFSIDE_RULE

#include "../gametypes.hpp"

namespace OffsideRule {

// Law 11: there is no offside offence when receiving the ball directly from a
// goal kick, a throw-in or a corner kick.
bool CanCreateOffside(e_SetPiece restart);

// Whether the referee should record offside positions at this touch. The touch
// of an exempt restart's delivery must not arm the flag; out of play nothing
// is recorded at all. `restartQueued` is whether a restart is currently staged
// (RefereeBuffer::active), `queuedRestart` which one.
bool ShouldSnapshot(bool inPlay, bool restartQueued, e_SetPiece queuedRestart);

// Law 11: a deliberate play is a controlled action on the ball. GF's touch
// vocabulary makes a controlled kick the only touch that clearly qualifies: a
// collision deflection is never deliberate, and a non-kicked touch cannot be
// told apart from a save, which must never reset the phase.
bool IsDeliberatePlay(e_TouchType touchType);

// Whether a touch ends the current offside phase, releasing the players
// flagged at the previous teammate touch. A touch by the flagged players' own
// team always starts a new phase (a fresh judgement is taken at that touch);
// an opponent's touch only resets when it is a deliberate play — a deflection
// or a save keeps the previously flagged attackers offside.
// `opponentOfFlagged` is whether the toucher opposes the flagged players; pass
// false when nobody is flagged.
bool TouchResetsPhase(bool opponentOfFlagged, e_TouchType touchType);

}  // namespace OffsideRule

#endif

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

}  // namespace OffsideRule

#endif

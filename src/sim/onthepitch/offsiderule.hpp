// Law 11 helpers, kept as free functions over plain data so they can be tested
// headlessly (docs/RULESET_AUDIT.md section 3). The Referee owns the whistle;
// these own the rule.

#ifndef _HPP_OFFSIDE_RULE
#define _HPP_OFFSIDE_RULE

#include <vector>

#include "../base/math/vector3.hpp"
#include "../gametypes.hpp"
#include "pesjudge.hpp"

namespace OffsideRule {

// PES 2021's offside detection (docs/re/pes-re-offside.md), kept as pure
// functions over plain data so they can be tested headlessly. The line runs in
// the defending team's own attack frame; the test and the category run in the
// attackers' frame, and every number comes from the install's PES2021.exe
// through `k`.

// PES 2021's offside line (report 3): in the defending team's own attack
// frame, L = min(0, the second-rearmost defender's rear surface, ball.x);
// fewer than two eligible defenders puts the second at the goal line, and the
// result is clamped to the goal line. `defenderCentres` are the eligible
// defenders' centre positions, `ballX` the current ball centre, `halfLength_m`
// the goal line's distance from the centre (PES's `pitch+0`, this engine's
// pitchHalfW + lineHalfW).
float OffsideLineX(int defendingSide, const std::vector<blunted::Vector3>& defenderCentres,
                   float ballX, float halfLength_m, const pes::JudgeConstants& k);

// The attacker test and category (report 4): beyond when the leading body
// point's surface passes the line by more than the tolerance, and the category
// from d = |P.x - L|: 2 when d < 1 m, 1 when d <= 4 m, 0 beyond that, and 3
// for a near-level attacker who is not beyond. Returns -1 when PES sets no bit.
int OffsideCategory(float attackerCentreX, float lineX, int attackingSide,
                    const pes::JudgeConstants& k);

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

// Law 11: the free kick is taken where the offence occurred - where the
// flagged player became involved in active play - not where he stood when the
// pass was struck. That holds even when he has dropped back into his own half
// before playing the ball (IFAB 2016 clarification).
blunted::Vector3 RestartPosition(const blunted::Vector3& positionAtPass,
                                 const blunted::Vector3& positionAtInvolvement);

}  // namespace OffsideRule

#endif

// DOGSO, denying an obvious goal-scoring opportunity: PES 2021's own test
// (docs/re/pes-re-duels-fouls.md 2.6). The foul itself is scored by
// pes::JudgeLevel (onthepitch/pesjudge.hpp).

#ifndef _HPP_FOUL_SEVERITY
#define _HPP_FOUL_SEVERITY

namespace FoulSeverity {

// The fouled player was ahead of the defensive reference line (the fouling
// team's last-defender line) in the attack direction, and within the width of
// the penalty area. `attackDirSign` is the sign of the attacked goal's x.
bool DeniesObviousChance(float attackDirSign, float fouledX, float fouledY,
                         float defensiveLineX, float penaltyAreaHalfWidth);

}  // namespace FoulSeverity

#endif

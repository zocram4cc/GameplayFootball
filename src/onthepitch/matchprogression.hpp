// Match phase progression: what follows the whistle at the end of a period.
// Extra time is only played when the match is level at full time, both extra
// periods are always completed, and penalties only follow a draw after them.
// See TECHNICAL_ROADMAP.md checklist item 1.

#ifndef _HPP_MATCH_PROGRESSION
#define _HPP_MATCH_PROGRESSION

#include "../gametypes.hpp"

namespace MatchProgression {

struct Outcome {
  bool gameOver = false;
  e_MatchPhase nextPhase = e_MatchPhase_1stHalf;
};

Outcome GetNext(e_MatchPhase currentPhase, bool scoresLevel);

// How much stoppage time a period can run over before the referee blows up
// regardless of where the ball is.
const unsigned long maxStoppageTime_ms = 3UL * 60UL * 1000UL;
// Half of the width of the central band the referee prefers to whistle in.
const float neutralBandHalfWidth = 10.0f;

// Scheduled end of the given period, on the running match clock.
unsigned long GetPeriodEndTime_ms(e_MatchPhase phase);

// Whether the whistle should go now. `ballX` is the ball's position along the
// length of the pitch.
bool ShouldEndPeriod(unsigned long matchTime_ms, unsigned long periodEnd_ms, float ballX);

}  // namespace MatchProgression

#endif

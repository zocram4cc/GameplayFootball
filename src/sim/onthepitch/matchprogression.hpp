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

// Whether the whistle should go now. `ballX` is the ball's position along the
// length of the pitch.
bool ShouldEndPeriod(unsigned long matchTime_ms, unsigned long periodEnd_ms, float ballX);

// Does this period change ends? Team::GetSide() flips for the second half and the
// second period of extra time and for nothing else, so these are the two moments
// both teams must be set out again on the ends they have just swapped to. Half time
// never did it: the referee prepares the kickoff and only then fires the phase
// change, so the marks belonged to the ends the teams were leaving.
inline bool SwapsEnds(e_MatchPhase phase) {
  return phase == e_MatchPhase_2ndHalf || phase == e_MatchPhase_2ndExtraTime;
}

}  // namespace MatchProgression

#endif

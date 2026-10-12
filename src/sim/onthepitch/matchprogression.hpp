// Match phase progression: what follows the whistle at the end of a period.
// Extra time is only played when the match is level at full time, both extra
// periods are always completed, and penalties only follow a draw after them.
// See TECHNICAL_ROADMAP.md checklist item 1.

#ifndef _HPP_MATCH_PROGRESSION
#define _HPP_MATCH_PROGRESSION

#include <string>

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

// --- Added time (Law 7: allowance for time lost) ---
//
// GF's clock stops during every restart, so time lost is accrued per discrete
// event instead of measured. PES 2021 keeps the same idea as a `lossTime` byte
// in its match state block. The accumulator is owned by Match and reset at
// every phase change.

enum e_StoppageReason {
  e_Stoppage_Goal,
  e_Stoppage_Substitution,
  e_Stoppage_Card,
  e_Stoppage_Injury,
};

struct Stoppage {
  unsigned long accrued_ms = 0;
};

// Law 7's own list: a goal celebration is worth about a minute, the rest about
// half of one.
const unsigned long stoppagePerGoal_ms = 60000;
const unsigned long stoppagePerSubstitution_ms = 30000;
const unsigned long stoppagePerCard_ms = 30000;
const unsigned long stoppagePerInjury_ms = 30000;

// Accrues time lost; the total never exceeds maxStoppageTime_ms, so together
// with the neutral-moment search a period runs at most twice that over.
void AddStoppage(Stoppage& stoppage, e_StoppageReason reason);

// Scheduled end of the period plus the allowance.
unsigned long GetPeriodEndTime_ms(e_MatchPhase phase, const Stoppage& stoppage);

// The fourth official's board: whole minutes, rounded up, never zero once any
// time at all was lost.
int GetAnnouncedAddedMinutes(const Stoppage& stoppage);

// The scoreboard clock: "44:59" in regulation, "45:00 +0:12" past the period's
// scheduled end. A scheduled end of 0 (pre-match, penalties) ticks plainly.
std::string FormatClock(unsigned long matchTime_ms, unsigned long scheduledEnd_ms);

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

#include "matchprogression.hpp"

#include <cmath>

namespace MatchProgression {

unsigned long GetPeriodEndTime_ms(e_MatchPhase phase) {
  switch (phase) {
    case e_MatchPhase_1stHalf:
      return 2700000;  // 45'
    case e_MatchPhase_2ndHalf:
      return 5400000;  // 90'
    case e_MatchPhase_1stExtraTime:
      return 6300000;  // 105'
    case e_MatchPhase_2ndExtraTime:
      return 7200000;  // 120'
    default:
      return 0;
  }
}

bool ShouldEndPeriod(unsigned long matchTime_ms, unsigned long periodEnd_ms, float ballX) {
  if (matchTime_ms <= periodEnd_ms)
    return false;

  // Past the allowance the whistle goes wherever the ball is, so a period can
  // never run on for a quarter of an hour waiting for a neutral moment.
  if (matchTime_ms > periodEnd_ms + maxStoppageTime_ms)
    return true;

  return std::fabs(ballX) < neutralBandHalfWidth;
}

Outcome GetNext(e_MatchPhase currentPhase, bool scoresLevel) {
  Outcome outcome;
  outcome.nextPhase = currentPhase;

  switch (currentPhase) {
    case e_MatchPhase_1stHalf:
      outcome.nextPhase = e_MatchPhase_2ndHalf;
      break;

    case e_MatchPhase_2ndHalf:
      // Extra time is only played when there is still nothing between them.
      if (scoresLevel)
        outcome.nextPhase = e_MatchPhase_1stExtraTime;
      else
        outcome.gameOver = true;
      break;

    case e_MatchPhase_1stExtraTime:
      // Just the turn-around in extra time; both periods are always played.
      outcome.nextPhase = e_MatchPhase_2ndExtraTime;
      break;

    case e_MatchPhase_2ndExtraTime:
      if (scoresLevel)
        outcome.nextPhase = e_MatchPhase_Penalties;
      else
        outcome.gameOver = true;
      break;

    default:
      // The shootout controller decides when the penalties phase is over.
      break;
  }

  return outcome;
}

}  // namespace MatchProgression

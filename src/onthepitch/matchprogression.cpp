#include "matchprogression.hpp"

#include <algorithm>
#include <cmath>

namespace MatchProgression {

namespace {

// Two digits for the scoreboard.
std::string PadTwo(unsigned long value) {
  std::string result = std::to_string(value);
  if (result.size() < 2)
    result.insert(result.begin(), '0');
  return result;
}

}  // namespace

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

void AddStoppage(Stoppage& stoppage, e_StoppageReason reason) {
  unsigned long loss_ms = 0;
  switch (reason) {
    case e_Stoppage_Goal:
      loss_ms = stoppagePerGoal_ms;
      break;
    case e_Stoppage_Substitution:
      loss_ms = stoppagePerSubstitution_ms;
      break;
    case e_Stoppage_Card:
      loss_ms = stoppagePerCard_ms;
      break;
    case e_Stoppage_Injury:
      loss_ms = stoppagePerInjury_ms;
      break;
  }
  stoppage.accrued_ms = std::min(stoppage.accrued_ms + loss_ms, maxStoppageTime_ms);
}

unsigned long GetPeriodEndTime_ms(e_MatchPhase phase, const Stoppage& stoppage) {
  const unsigned long scheduled = GetPeriodEndTime_ms(phase);
  if (scheduled == 0)
    return 0;
  return scheduled + stoppage.accrued_ms;
}

int GetAnnouncedAddedMinutes(const Stoppage& stoppage) {
  if (stoppage.accrued_ms == 0)
    return 0;
  return static_cast<int>((stoppage.accrued_ms + 59999) / 60000);
}

std::string FormatClock(unsigned long matchTime_ms, unsigned long scheduledEnd_ms) {
  if (scheduledEnd_ms == 0 || matchTime_ms <= scheduledEnd_ms) {
    const unsigned long minutes = matchTime_ms / 60000;
    const unsigned long seconds = (matchTime_ms / 1000) % 60;
    return PadTwo(minutes) + ":" + PadTwo(seconds);
  }

  // Past the scheduled end the clock holds there and counts the overtime
  // separately, the way broadcast clocks do.
  const unsigned long overtime_ms = matchTime_ms - scheduledEnd_ms;
  return PadTwo(scheduledEnd_ms / 60000) + ":" + PadTwo((scheduledEnd_ms / 1000) % 60) + " +" +
         std::to_string(overtime_ms / 60000) + ":" + PadTwo((overtime_ms / 1000) % 60);
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

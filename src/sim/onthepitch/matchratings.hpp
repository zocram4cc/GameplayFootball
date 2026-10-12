// The post-match Player Ratings, from what this match actually recorded.
//
// PES's result page rates every player 5.0-10.0 (VGL 26 day 12, 5:15:13:
// docs/VGL26_DAY12_REFERENCE.md §3.2). The engine keeps per-player goals
// (Match::GetGoalsToday) and cards (Player::GetCards) and per-team everything
// else, so a rating here is built from those three facts and the result and
// nothing invented: a baseline, a goal each, a card against, the win or the
// loss, a clean sheet for the back line. Pure, so it is testable; the source
// of each term is named beside it.

#ifndef _HPP_ONTHEPITCH_MATCHRATINGS
#define _HPP_ONTHEPITCH_MATCHRATINGS

#include <algorithm>

namespace MatchRatings {

struct Facts {
  int goals = 0;          // his goals in this match
  int cards = 0;          // Player::GetCards encoding: 1 yellow, 3 red
  int goalDifference = 0; // his side's, at the end
  bool defensiveLine = false;  // GK or back line
  bool cleanSheet = false;     // his side conceded nothing
};

constexpr float kBaseline = 6.0f;
constexpr float kPerGoal = 1.0f;
constexpr float kYellow = -0.5f;
constexpr float kRed = -1.5f;
constexpr float kWin = 0.5f;
constexpr float kLoss = -0.5f;
constexpr float kCleanSheet = 0.5f;
constexpr float kFloor = 5.0f;
constexpr float kCeiling = 10.0f;

inline float Rate(const Facts& f) {
  float rating = kBaseline + f.goals * kPerGoal;
  if (f.cards >= 3) rating += kRed;
  else if (f.cards > 0) rating += kYellow * f.cards;
  if (f.goalDifference > 0) rating += kWin;
  if (f.goalDifference < 0) rating += kLoss;
  if (f.defensiveLine && f.cleanSheet) rating += kCleanSheet;
  return std::clamp(rating, kFloor, kCeiling);
}

}  // namespace MatchRatings

#endif

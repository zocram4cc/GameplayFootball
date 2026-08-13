#include "matchpressure.hpp"

#include <algorithm>
#include <cmath>

namespace MatchPressure {

namespace {

// Base risk once two opponents are on top of the player, before temperament.
const float basePanicChance = 0.06f;
// Each opponent beyond the threshold adds this much pressure.
const float perExtraOpponent = 0.04f;
const float youthPanicPenalty = 0.04f;

}  // namespace

bool IsCloseGame(int goalDifference) {
  return std::abs(goalDifference) <= 1;
}

bool IsFinalTenMinutes(unsigned long matchTime_ms) {
  return matchTime_ms >= finalTenMinutesStart_ms;
}

float GetClutchTechnicalMultiplier(float resilience, int goalDifference,
                                   unsigned long matchTime_ms) {
  if (resilience < clutchResilienceThreshold)
    return 1.0f;
  if (!IsFinalTenMinutes(matchTime_ms) || !IsCloseGame(goalDifference))
    return 1.0f;
  return 1.0f + clutchTechnicalBonus;
}

float GetStumbleChance(float calmness, int age, int pressuringOpponents) {
  if (pressuringOpponents < panicOpponentThreshold)
    return 0.0f;

  const float nerves = 1.0f - std::max(0.0f, std::min(calmness, 1.0f));
  const int extraOpponents = pressuringOpponents - panicOpponentThreshold;

  float chance = (basePanicChance + perExtraOpponent * extraOpponents) * nerves;
  // Youngsters are still learning to handle a crowd; unknown age counts as
  // experienced so that plain match play is unaffected.
  if (age != unknownAge && age < youngPlayerAge)
    chance += youthPanicPenalty;

  return std::max(0.0f, std::min(chance, maxStumbleChance));
}

bool ShouldStumble(float stumbleChance, float randomSample) {
  return randomSample < stumbleChance;
}

}  // namespace MatchPressure

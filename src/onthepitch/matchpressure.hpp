// Mental fatigue and pressure: players who raise their game when it matters,
// and players who lose their footing when the game closes in on them.
// See SIMULATION_IMPROVEMENT_PROPOSAL.md section 3B.

#ifndef _HPP_MATCH_PRESSURE
#define _HPP_MATCH_PRESSURE

namespace MatchPressure {

// Age is not always known on the pitch (only career mode tracks it).
const int unknownAge = -1;
const int youngPlayerAge = 21;

const unsigned long finalTenMinutesStart_ms = 80UL * 60000UL;
const float clutchResilienceThreshold = 0.8f;
const float clutchTechnicalBonus = 0.05f;

// Two opponents closing in is the trigger for panic.
const int panicOpponentThreshold = 2;
const float maxStumbleChance = 0.25f;

bool IsCloseGame(int goalDifference);
bool IsFinalTenMinutes(unsigned long matchTime_ms);

// Multiplier applied to technical stats.
float GetClutchTechnicalMultiplier(float resilience, int goalDifference,
                                   unsigned long matchTime_ms);

// Probability that the player stumbles while under pressure. `age` may be
// `unknownAge`, in which case no youth penalty applies.
float GetStumbleChance(float calmness, int age, int pressuringOpponents);

// Deterministic roll: `randomSample` is expected in [0, 1) and supplied by the
// caller.
bool ShouldStumble(float stumbleChance, float randomSample);

}  // namespace MatchPressure

#endif

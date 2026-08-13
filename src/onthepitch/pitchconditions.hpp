// Surface and environment effects: a pitch that cuts up as the match wears on,
// and players losing their footing on it. Builds on the wind/wetness terms that
// already exist in BallPhysicsConfig.
// See SIMULATION_IMPROVEMENT_PROPOSAL.md section 4A.

#ifndef _HPP_PITCH_CONDITIONS
#define _HPP_PITCH_CONDITIONS

struct BallPhysicsConfig;

namespace PitchConditions {

// The pitch is considered fully chewed up by the end of regulation time.
const unsigned long fullWearTime_ms = 90UL * 60000UL;
// Extra ground friction on a completely worn pitch.
const float maxWearFrictionBonus = 0.3f;
// Extra air resistance in heavy rain.
const float maxWetnessDragBonus = 0.25f;
const float maxSlipChance = 0.2f;

// Fraction of the match's wear accumulated so far, in [0, 1].
float GetWear(unsigned long matchTime_ms);

float GetWearFrictionMultiplier(float wear);
float GetWetnessDragMultiplier(float wetness);

// Folds the current conditions into the ball physics configuration. A fresh dry
// pitch (wear 0, wetness 0) leaves the configuration untouched.
void Apply(BallPhysicsConfig& config, float wear, float wetness);

// Probability that a player loses his footing. `turnSharpness` is 0 for running
// straight and 1 for a full change of direction; `balance` is the player's
// physical balance stat in [0, 1].
float GetSlipChance(float wetness, float wear, float turnSharpness, float balance);

// Deterministic roll: `randomSample` is expected in [0, 1) and supplied by the
// caller.
bool ShouldSlip(float slipChance, float randomSample);

// How long a player needs to regain full pace after slipping.
const unsigned long slipRecoveryTime_ms = 900;
// Pace left immediately after going down.
const float slipVelocityFloor = 0.4f;

// Turn sharpness from the dot product between the old and new direction: 0 when
// running straight on, 1 when turning back on himself.
float GetTurnSharpness(float directionDotProduct);

// Velocity multiplier `elapsed_ms` after a slip, recovering to 1.
float GetSlipVelocityMultiplier(unsigned long elapsedSinceSlip_ms);

}  // namespace PitchConditions

#endif

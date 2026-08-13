#include "pitchconditions.hpp"

#include <algorithm>

#include "ballphysics.hpp"

namespace PitchConditions {

namespace {

float Clamp01(float value) {
  return std::max(0.0f, std::min(value, 1.0f));
}

// Risk of losing your footing in a full turn on a soaked pitch, before the
// player's own balance is taken into account.
const float wetSlipRisk = 0.14f;
// A cut-up surface is treacherous even when dry.
const float wearSlipRisk = 0.05f;
// How much of the risk a perfectly balanced player can absorb.
const float balanceSlipRelief = 0.7f;

}  // namespace

float GetWear(unsigned long matchTime_ms) {
  return Clamp01(static_cast<float>(matchTime_ms) / static_cast<float>(fullWearTime_ms));
}

float GetWearFrictionMultiplier(float wear) {
  return 1.0f + maxWearFrictionBonus * Clamp01(wear);
}

float GetWetnessDragMultiplier(float wetness) {
  return 1.0f + maxWetnessDragBonus * Clamp01(wetness);
}

void Apply(BallPhysicsConfig& config, float wear, float wetness) {
  const float wearFriction = GetWearFrictionMultiplier(wear);

  // Long grass and divots slow the ball down along the ground.
  config.friction *= wearFriction;
  config.linearFriction *= wearFriction;
  // A wet ball flying through rain meets more resistance...
  config.drag *= GetWetnessDragMultiplier(wetness);
  // ...while the turf itself gets slicker; that part lives in the ball physics.
  config.wetness = Clamp01(wetness);
}

float GetSlipChance(float wetness, float wear, float turnSharpness, float balance) {
  const float sharpness = Clamp01(turnSharpness);
  if (sharpness <= 0.0f)
    return 0.0f;

  const float surfaceRisk = wetSlipRisk * Clamp01(wetness) + wearSlipRisk * Clamp01(wear);
  const float relief = 1.0f - balanceSlipRelief * Clamp01(balance);
  return std::min(surfaceRisk * sharpness * relief, maxSlipChance);
}

bool ShouldSlip(float slipChance, float randomSample) {
  return randomSample < slipChance;
}

float GetTurnSharpness(float directionDotProduct) {
  const float dot = std::max(-1.0f, std::min(directionDotProduct, 1.0f));
  return (1.0f - dot) * 0.5f;
}

float GetSlipVelocityMultiplier(unsigned long elapsedSinceSlip_ms) {
  if (elapsedSinceSlip_ms >= slipRecoveryTime_ms)
    return 1.0f;
  const float recovered =
      static_cast<float>(elapsedSinceSlip_ms) / static_cast<float>(slipRecoveryTime_ms);
  return slipVelocityFloor + (1.0f - slipVelocityFloor) * recovered;
}

}  // namespace PitchConditions

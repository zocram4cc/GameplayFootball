#include "ballphysics.hpp"

#include <algorithm>
#include <cmath>

GoalNettingResult ApplyGoalNettingCollision(BallPhysicsState& state, bool ballIsInGoal,
                                            const GoalNettingConfig& config, float timeStep_s) {
  GoalNettingResult result;
  if (!ballIsInGoal) return result;

  const float powFactor = 2.6f;
  const float powerFac = 1.8f;
  const float netAbsorbInv = std::pow(0.95f, timeStep_s * 100.0f);

  blunted::Vector3& pos = state.position;
  blunted::Vector3& mom = state.momentum;

  const bool behindBackline = std::fabs(pos.coords[0]) > config.pitchHalfW + 0.11f;
  if (!behindBackline) return result;

  const float backX = config.pitchHalfW + config.goalDepth;
  const bool beforeGoalBack = std::fabs(pos.coords[0]) < backX - 0.11f;
  const bool betweenGoalWidth = std::fabs(pos.coords[1]) < config.goalHalfWidth - 0.11f;
  const bool belowGoalHeight = pos.coords[2] < config.goalHeight + 0.11f;

  // side netting
  if (!betweenGoalWidth) {
    const float netDist = blunted::clamp(std::fabs(std::fabs(pos.coords[1]) - config.goalHalfWidth), 0.0f, 1.0f);
    const float power = std::pow(netDist, powFactor) * -blunted::signSide(pos.coords[1]);
    const float woodworkTensionBiasInv =
        blunted::clamp((std::fabs(mom.coords[0]) - config.pitchHalfW) * 2.0f, 0.0f, 1.0f);
    const float adaptedPowerFac = powerFac + (1.0f - woodworkTensionBiasInv) * 3.0f;
    mom.coords[1] = mom.coords[1] * netAbsorbInv + power * adaptedPowerFac * (100.0f * timeStep_s);

    // Hard stop: whatever the spring above did to its momentum this step, the
    // ball's own surface cannot sit past the mesh. Without this a shot fast
    // enough sheds only a sliver of speed per step and keeps going regardless.
    const float limit = config.goalHalfWidth + config.ballRadius;
    pos.coords[1] = blunted::clamp(pos.coords[1], -limit, limit);

    result.touchedNet = true;
  }

  if (!beforeGoalBack) {
    const float netDist = blunted::clamp(std::fabs(std::fabs(pos.coords[0]) - backX), 0.0f, 1.0f);
    const float power = std::pow(netDist, powFactor) * -blunted::signSide(pos.coords[0]);
    mom.coords[0] = mom.coords[0] * netAbsorbInv + power * powerFac * (100.0f * timeStep_s);

    const float limit = backX + config.ballRadius;
    pos.coords[0] = blunted::clamp(pos.coords[0], -limit, limit);

    result.touchedNet = true;
  }

  if (!belowGoalHeight) {
    const float netDist = blunted::clamp(std::fabs(std::fabs(pos.coords[2]) - config.goalHeight), 0.0f, 1.0f);
    const float power = std::pow(netDist, powFactor) * -1.0f;
    const float woodworkTensionBiasInv =
        blunted::clamp((std::fabs(mom.coords[0]) - config.pitchHalfW) * 2.0f, 0.0f, 1.0f);
    const float adaptedPowerFac = powerFac + (1.0f - woodworkTensionBiasInv) * 3.0f;
    mom.coords[2] = mom.coords[2] * netAbsorbInv + power * adaptedPowerFac * (100.0f * timeStep_s);

    const float limit = config.goalHeight + config.ballRadius;
    pos.coords[2] = std::min(pos.coords[2], limit);

    result.touchedNet = true;
  }

  return result;
}

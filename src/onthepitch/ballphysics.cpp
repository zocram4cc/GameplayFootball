#include "ballphysics.hpp"

#include <algorithm>
#include <cmath>

#include "base/math/bluntmath.hpp"

BallGroundInteraction ApplyBallMotionForces(BallPhysicsState& state, const BallPhysicsConfig& config,
                                            float timeStep_s) {
  BallGroundInteraction interaction;

  state.momentum.coords[2] = state.momentum.coords[2] + config.gravity * timeStep_s;

  const float momentumVelo = state.momentum.GetLength();
  const float momentumVeloDragged =
      momentumVelo - config.drag * std::pow(momentumVelo, 2.0f) * timeStep_s;
  state.momentum = state.momentum.GetNormalized(0) * momentumVeloDragged;

  const float ballBottom = state.position.coords[2] - config.ballRadius;
  interaction.grassInfluenceBias =
      blunted::clamp(1.0f - (ballBottom / config.grassHeight), 0.0f, 1.0f);
  interaction.grassInfluenceBias = std::pow(interaction.grassInfluenceBias, 0.7f);

  // Wind only meaningfully affects the ball in flight; a ball settled in the
  // grass is shielded and dominated by ground friction. Scale the wind by how
  // airborne the ball is so a calm/grounded ball is unchanged.
  const float airborneFactor = 1.0f - interaction.grassInfluenceBias;
  state.momentum += config.wind * (airborneFactor * timeStep_s);

  if (state.position.coords[2] < config.ballRadius) {
    if (state.momentum.coords[2] < 0.0f) {
      interaction.frictionFactor =
          blunted::NormalizedClamp(-state.momentum.coords[2] - 0.5f, 0.0f, 12.0f);
      state.momentum.coords[2] = -state.momentum.coords[2] * config.bounce;
      state.momentum.coords[2] =
          std::max(state.momentum.coords[2] - config.linearBounce, 0.0f);
    }

    state.position.coords[2] = config.ballRadius;
  }

  if (state.position.coords[2] < config.ballRadius + config.grassHeight) {
    // A wet pitch is slicker: cut ground friction by up to half at full
    // wetness so the ball skids and keeps more pace along the turf.
    const float wetnessFrictionScale = 1.0f - blunted::clamp(config.wetness, 0.0f, 1.0f) * 0.5f;
    const float adaptedFriction =
        config.friction * interaction.grassInfluenceBias * wetnessFrictionScale;
    blunted::Vector3 xy = state.momentum.Get2D();
    const float velo = xy.GetLength();

    float newVelo = velo - adaptedFriction * std::pow(velo, 2.0f) * timeStep_s;
    newVelo = blunted::clamp(newVelo - (config.linearFriction * interaction.grassInfluenceBias *
                                        wetnessFrictionScale * timeStep_s),
                             0.0f, 100000.0f);

    xy.Normalize(blunted::Vector3(0));
    xy *= newVelo;
    state.momentum.coords[0] = xy.coords[0];
    state.momentum.coords[1] = xy.coords[1];
  }

  return interaction;
}

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

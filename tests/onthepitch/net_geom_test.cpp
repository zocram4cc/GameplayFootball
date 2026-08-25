// What keeps a ball that has crossed the goal line inside the net.
//
// The net is not a wall: it is a spring that decelerates the ball a little each
// step. That is enough for a normal shot, which loses most of its pace to grass
// and drag before it ever reaches goal, but a full-power penalty arrives with far
// more speed to shed, and the spring alone was not stopping it before it crossed
// the net's own ~2.5m depth and came out behind the mesh. These tests pin the
// hard clamp that has to sit alongside the spring so that cannot happen, whatever
// the shot's speed.

#include <cmath>

#include <gtest/gtest.h>

#include "onthepitch/ballphysics.hpp"

using blunted::Vector3;

namespace {

constexpr float kTimeStep_s = 0.01f;

GoalNettingConfig DefaultConfig() {
  return GoalNettingConfig();
}

}  // namespace

TEST(GoalNettingCollision, DoesNothingWhenTheBallIsNotInGoal) {
  BallPhysicsState state{Vector3(58.0f, 0.0f, 1.0f), Vector3(44.0f, 0.0f, 0.0f)};
  const GoalNettingResult result =
      ApplyGoalNettingCollision(state, false, DefaultConfig(), kTimeStep_s);

  EXPECT_FALSE(result.touchedNet);
  EXPECT_FLOAT_EQ(state.position.coords[0], 58.0f);
  EXPECT_FLOAT_EQ(state.momentum.coords[0], 44.0f);
}

TEST(GoalNettingCollision, ItLeavesTheBallAloneInFrontOfTheGoalLine) {
  // Well inside the pitch: none of the three panels apply here, regardless of
  // ballIsInGoal (which a shot this far out could not have earned anyway).
  BallPhysicsState state{Vector3(40.0f, 0.0f, 1.0f), Vector3(20.0f, 0.0f, 0.0f)};
  const GoalNettingResult result =
      ApplyGoalNettingCollision(state, true, DefaultConfig(), kTimeStep_s);

  EXPECT_FALSE(result.touchedNet);
  EXPECT_FLOAT_EQ(state.position.coords[0], 40.0f);
  EXPECT_FLOAT_EQ(state.momentum.coords[0], 20.0f);
}

TEST(GoalNettingCollision, ASlowShotIsCaughtByTheRearNetWithoutBeingClamped) {
  const GoalNettingConfig config = DefaultConfig();
  // Just into the rear panel's band (backX - 0.11 = 57.44), well short of the
  // physical mesh (backX + ballRadius = 57.66): a normal finish's speed, which
  // the spring alone is expected to handle.
  BallPhysicsState state{Vector3(57.5f, 0.0f, 0.2f), Vector3(18.0f, 0.0f, 0.0f)};
  const GoalNettingResult result = ApplyGoalNettingCollision(state, true, config, kTimeStep_s);

  EXPECT_TRUE(result.touchedNet);
  EXPECT_LT(state.momentum.coords[0], 18.0f);
  // No overshoot to correct at this position, so the clamp leaves it be.
  EXPECT_FLOAT_EQ(state.position.coords[0], 57.5f);
}

TEST(GoalNettingCollision, AFullPowerPenaltyCannotTunnelPastTheRearNet) {
  const GoalNettingConfig config = DefaultConfig();
  const float limit = config.pitchHalfW + config.goalDepth + config.ballRadius;
  // A tick that already overshot the mesh by a metre - what an unclamped, weak
  // spring leaves behind when a 44 m/s penalty (the measured full-power shot;
  // see the shootout's own power log) crosses the ~0.11m detection band faster
  // than one 10ms step.
  BallPhysicsState state{Vector3(limit + 1.0f, 0.0f, 0.2f), Vector3(44.0f, 0.0f, 0.0f)};
  const GoalNettingResult result = ApplyGoalNettingCollision(state, true, config, kTimeStep_s);

  EXPECT_TRUE(result.touchedNet);
  EXPECT_LE(state.position.coords[0], limit + 1e-4f);
}

TEST(GoalNettingCollision, ASideNettingOvershootIsClampedAtThePost) {
  const GoalNettingConfig config = DefaultConfig();
  const float limit = config.goalHalfWidth + config.ballRadius;
  BallPhysicsState state{Vector3(57.0f, limit + 1.0f, 0.2f), Vector3(5.0f, 30.0f, 0.0f)};
  const GoalNettingResult result = ApplyGoalNettingCollision(state, true, config, kTimeStep_s);

  EXPECT_TRUE(result.touchedNet);
  EXPECT_LE(state.position.coords[1], limit + 1e-4f);
}

TEST(GoalNettingCollision, ANegativeSideNettingOvershootIsClampedAtThePost) {
  const GoalNettingConfig config = DefaultConfig();
  const float limit = config.goalHalfWidth + config.ballRadius;
  BallPhysicsState state{Vector3(57.0f, -(limit + 1.0f), 0.2f), Vector3(5.0f, -30.0f, 0.0f)};
  const GoalNettingResult result = ApplyGoalNettingCollision(state, true, config, kTimeStep_s);

  EXPECT_TRUE(result.touchedNet);
  EXPECT_GE(state.position.coords[1], -(limit + 1e-4f));
}

TEST(GoalNettingCollision, ATopNettingOvershootIsClampedAtTheHeight) {
  const GoalNettingConfig config = DefaultConfig();
  const float limit = config.goalHeight + config.ballRadius;
  BallPhysicsState state{Vector3(57.0f, 0.0f, limit + 1.0f), Vector3(5.0f, 0.0f, 30.0f)};
  const GoalNettingResult result = ApplyGoalNettingCollision(state, true, config, kTimeStep_s);

  EXPECT_TRUE(result.touchedNet);
  EXPECT_LE(state.position.coords[2], limit + 1e-4f);
}

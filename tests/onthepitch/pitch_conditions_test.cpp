// Tests for pitch degradation and wet-weather slipping described in
// SIMULATION_IMPROVEMENT_PROPOSAL.md section 4A. The wind/wetness ball physics
// they build on already exist in ballphysics.cpp.

#include "onthepitch/pitchconditions.hpp"

#include <gtest/gtest.h>

#include "onthepitch/ballphysics.hpp"

namespace {

constexpr unsigned long Minutes(float minutes) {
  return static_cast<unsigned long>(minutes * 60000.0f);
}

}  // namespace

TEST(PitchWearTest, ThePitchStartsPristineAndChewsUpOverTheMatch) {
  EXPECT_FLOAT_EQ(PitchConditions::GetWear(0), 0.0f);
  EXPECT_NEAR(PitchConditions::GetWear(Minutes(45)), 0.5f, 1e-4f);
  EXPECT_FLOAT_EQ(PitchConditions::GetWear(Minutes(90)), 1.0f);
}

TEST(PitchWearTest, WearNeverExceedsOneInExtraTime) {
  EXPECT_FLOAT_EQ(PitchConditions::GetWear(Minutes(120)), 1.0f);
}

TEST(PitchWearTest, WornGrassGripsTheBallMore) {
  EXPECT_FLOAT_EQ(PitchConditions::GetWearFrictionMultiplier(0.0f), 1.0f);
  EXPECT_GT(PitchConditions::GetWearFrictionMultiplier(1.0f),
            PitchConditions::GetWearFrictionMultiplier(0.5f));
  EXPECT_GT(PitchConditions::GetWearFrictionMultiplier(0.5f), 1.0f);
  EXPECT_LE(PitchConditions::GetWearFrictionMultiplier(1.0f), 1.5f);
}

TEST(PitchWetnessTest, RainThickensTheAirTheBallFliesThrough) {
  EXPECT_FLOAT_EQ(PitchConditions::GetWetnessDragMultiplier(0.0f), 1.0f);
  EXPECT_GT(PitchConditions::GetWetnessDragMultiplier(1.0f), 1.0f);
  EXPECT_LE(PitchConditions::GetWetnessDragMultiplier(1.0f), 1.5f);
}

TEST(PitchConditionsApplyTest, AFreshDryPitchLeavesThePhysicsUntouched) {
  const BallPhysicsConfig reference;
  BallPhysicsConfig config;
  PitchConditions::Apply(config, 0.0f, 0.0f);

  EXPECT_FLOAT_EQ(config.friction, reference.friction);
  EXPECT_FLOAT_EQ(config.linearFriction, reference.linearFriction);
  EXPECT_FLOAT_EQ(config.drag, reference.drag);
  EXPECT_FLOAT_EQ(config.wetness, reference.wetness);
}

TEST(PitchConditionsApplyTest, WearRaisesBothFrictionTerms) {
  const BallPhysicsConfig reference;
  BallPhysicsConfig config;
  PitchConditions::Apply(config, 1.0f, 0.0f);

  EXPECT_GT(config.friction, reference.friction);
  EXPECT_GT(config.linearFriction, reference.linearFriction);
}

TEST(PitchConditionsApplyTest, WetnessReachesTheBallPhysicsAndRaisesDrag) {
  const BallPhysicsConfig reference;
  BallPhysicsConfig config;
  PitchConditions::Apply(config, 0.0f, 0.8f);

  EXPECT_FLOAT_EQ(config.wetness, 0.8f);
  EXPECT_GT(config.drag, reference.drag);
}

TEST(PitchConditionsApplyTest, LeavesUnrelatedPhysicsAlone) {
  const BallPhysicsConfig reference;
  BallPhysicsConfig config;
  PitchConditions::Apply(config, 1.0f, 1.0f);

  EXPECT_FLOAT_EQ(config.gravity, reference.gravity);
  EXPECT_FLOAT_EQ(config.bounce, reference.bounce);
  EXPECT_FLOAT_EQ(config.ballRadius, reference.ballRadius);
  EXPECT_FLOAT_EQ(config.grassHeight, reference.grassHeight);
}

TEST(PitchConditionsApplyTest, ClampsOutOfRangeInputs) {
  BallPhysicsConfig config;
  PitchConditions::Apply(config, 5.0f, 5.0f);
  EXPECT_FLOAT_EQ(config.wetness, 1.0f);

  BallPhysicsConfig negative;
  const BallPhysicsConfig reference;
  PitchConditions::Apply(negative, -1.0f, -1.0f);
  EXPECT_FLOAT_EQ(negative.friction, reference.friction);
  EXPECT_FLOAT_EQ(negative.wetness, 0.0f);
}

TEST(PlayerSlipTest, NobodySlipsJoggingInAStraightLineOnADryPitch) {
  EXPECT_FLOAT_EQ(PitchConditions::GetSlipChance(0.0f, 0.0f, 0.0f, 0.5f), 0.0f);
  EXPECT_FLOAT_EQ(PitchConditions::GetSlipChance(1.0f, 1.0f, 0.0f, 0.5f), 0.0f);
}

TEST(PlayerSlipTest, SharpTurnsOnAWetPitchAreRisky) {
  const float dry = PitchConditions::GetSlipChance(0.0f, 0.0f, 1.0f, 0.5f);
  const float wet = PitchConditions::GetSlipChance(1.0f, 0.0f, 1.0f, 0.5f);
  EXPECT_GT(wet, dry);
  EXPECT_GT(wet, 0.0f);
}

TEST(PlayerSlipTest, AChewedUpPitchAddsRiskOfItsOwn) {
  EXPECT_GT(PitchConditions::GetSlipChance(0.2f, 1.0f, 1.0f, 0.5f),
            PitchConditions::GetSlipChance(0.2f, 0.0f, 1.0f, 0.5f));
}

TEST(PlayerSlipTest, WellBalancedPlayersStayOnTheirFeet) {
  EXPECT_LT(PitchConditions::GetSlipChance(1.0f, 1.0f, 1.0f, 1.0f),
            PitchConditions::GetSlipChance(1.0f, 1.0f, 1.0f, 0.0f));
}

TEST(PlayerSlipTest, SlipChanceStaysAProbability) {
  for (float wetness = 0.0f; wetness <= 1.0f; wetness += 0.25f) {
    for (float sharpness = 0.0f; sharpness <= 1.0f; sharpness += 0.25f) {
      const float chance = PitchConditions::GetSlipChance(wetness, 1.0f, sharpness, 0.0f);
      EXPECT_GE(chance, 0.0f);
      EXPECT_LE(chance, PitchConditions::maxSlipChance);
    }
  }
}

TEST(PlayerSlipTest, TheSlipRollComparesTheSampleAgainstTheChance) {
  EXPECT_TRUE(PitchConditions::ShouldSlip(0.2f, 0.19f));
  EXPECT_FALSE(PitchConditions::ShouldSlip(0.2f, 0.2f));
  EXPECT_FALSE(PitchConditions::ShouldSlip(0.0f, 0.0f));
}

// --- Turn sharpness and the loss of pace a slip causes ---

TEST(PlayerSlipTest, TurnSharpnessComesFromTheChangeOfDirection) {
  // Running straight on: no sharpness at all.
  EXPECT_FLOAT_EQ(PitchConditions::GetTurnSharpness(1.0f), 0.0f);
  // A right-angle turn is halfway.
  EXPECT_NEAR(PitchConditions::GetTurnSharpness(0.0f), 0.5f, 1e-5f);
  // Turning back on himself is as sharp as it gets.
  EXPECT_FLOAT_EQ(PitchConditions::GetTurnSharpness(-1.0f), 1.0f);
}

TEST(PlayerSlipTest, TurnSharpnessIsClampedForOutOfRangeDotProducts) {
  EXPECT_FLOAT_EQ(PitchConditions::GetTurnSharpness(2.0f), 0.0f);
  EXPECT_FLOAT_EQ(PitchConditions::GetTurnSharpness(-2.0f), 1.0f);
}

TEST(PlayerSlipTest, ASlipCostsPaceAndThenWearsOff) {
  // Right after slipping the player has lost most of his pace...
  EXPECT_LT(PitchConditions::GetSlipVelocityMultiplier(0), 0.7f);
  EXPECT_GT(PitchConditions::GetSlipVelocityMultiplier(0), 0.0f);
  // ...recovers gradually...
  EXPECT_GT(PitchConditions::GetSlipVelocityMultiplier(PitchConditions::slipRecoveryTime_ms / 2),
            PitchConditions::GetSlipVelocityMultiplier(0));
  // ...and is back to full speed once the recovery time has passed.
  EXPECT_FLOAT_EQ(PitchConditions::GetSlipVelocityMultiplier(PitchConditions::slipRecoveryTime_ms),
                  1.0f);
  EXPECT_FLOAT_EQ(
      PitchConditions::GetSlipVelocityMultiplier(PitchConditions::slipRecoveryTime_ms + 5000), 1.0f);
}

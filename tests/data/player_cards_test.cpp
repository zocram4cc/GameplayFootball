// Tests for the PES-style "cards" (conditional modifiers and logic overrides)
// described in TECHNICAL_ROADMAP.md section 4D.

#include <gtest/gtest.h>

#include "data/playertraits.hpp"

TEST(OneTouchPassTest, RemovesTheAccuracyPenaltyOnAQuickRelease) {
  const PlayerTraits::TraitMask mask = PlayerTraits::e_Trait_OneTouchPass;
  const float basePenalty = 0.3f;

  EXPECT_FLOAT_EQ(PlayerTraits::GetQuickReleaseAccuracyPenalty(mask, 100, basePenalty), 0.0f);
  EXPECT_FLOAT_EQ(PlayerTraits::GetQuickReleaseAccuracyPenalty(
                      mask, PlayerTraits::oneTouchWindow_ms, basePenalty),
                  0.0f);
}

TEST(OneTouchPassTest, KeepsThePenaltyOnceTheBallHasBeenHeld) {
  const PlayerTraits::TraitMask mask = PlayerTraits::e_Trait_OneTouchPass;
  EXPECT_FLOAT_EQ(
      PlayerTraits::GetQuickReleaseAccuracyPenalty(mask, PlayerTraits::oneTouchWindow_ms + 1, 0.3f),
      0.3f);
  EXPECT_FLOAT_EQ(PlayerTraits::GetQuickReleaseAccuracyPenalty(mask, 2000, 0.3f), 0.3f);
}

TEST(OneTouchPassTest, PlayersWithoutTheCardAlwaysPayThePenalty) {
  EXPECT_FLOAT_EQ(
      PlayerTraits::GetQuickReleaseAccuracyPenalty(PlayerTraits::traitMaskNone, 50, 0.3f), 0.3f);
  EXPECT_FLOAT_EQ(
      PlayerTraits::GetQuickReleaseAccuracyPenalty(PlayerTraits::e_Trait_TargetMan, 50, 0.3f),
      0.3f);
}

TEST(FirstTimeShotTest, BoostsPowerOnlyWhenStrikingAMovingBallFirstTime) {
  const PlayerTraits::TraitMask mask = PlayerTraits::e_Trait_FirstTimeShot;

  EXPECT_GT(PlayerTraits::GetFirstTimeShotPowerMultiplier(mask, true, 6.0f), 1.0f);
  // Ball already under control: no bonus.
  EXPECT_FLOAT_EQ(PlayerTraits::GetFirstTimeShotPowerMultiplier(mask, false, 6.0f), 1.0f);
  // Dead ball: nothing to time.
  EXPECT_FLOAT_EQ(PlayerTraits::GetFirstTimeShotPowerMultiplier(mask, true, 0.0f), 1.0f);
}

TEST(FirstTimeShotTest, BonusGrowsWithBallSpeedButStaysBounded) {
  const PlayerTraits::TraitMask mask = PlayerTraits::e_Trait_FirstTimeShot;
  const float slow = PlayerTraits::GetFirstTimeShotPowerMultiplier(mask, true, 3.0f);
  const float fast = PlayerTraits::GetFirstTimeShotPowerMultiplier(mask, true, 15.0f);

  EXPECT_GT(fast, slow);
  EXPECT_LE(fast, 1.2f);
}

TEST(FirstTimeShotTest, PlayersWithoutTheCardGetNothing) {
  EXPECT_FLOAT_EQ(
      PlayerTraits::GetFirstTimeShotPowerMultiplier(PlayerTraits::traitMaskNone, true, 8.0f), 1.0f);
}

// Own goal is at side * pitchHalfW, so "forward" is the -side direction and a
// poacher wants to sit just on the own-goal side of the opponent offside line.
TEST(GoalPoacherTest, GluesTheForwardToTheOpponentOffsideLine) {
  const float offsideLineX = -20.0f;
  const float target =
      PlayerTraits::GetPoacherTargetX(PlayerTraits::e_Trait_GoalPoacher, -5.0f, offsideLineX, 1);

  EXPECT_GT(target, offsideLineX);  // stays onside
  EXPECT_NEAR(target, offsideLineX + PlayerTraits::poacherOffsideCushion, 1e-5f);
}

TEST(GoalPoacherTest, PullsBackAForwardThatWouldStrayOffside) {
  const float offsideLineX = -20.0f;
  // Default position is beyond the line (further forward for side +1).
  const float target =
      PlayerTraits::GetPoacherTargetX(PlayerTraits::e_Trait_GoalPoacher, -30.0f, offsideLineX, 1);
  EXPECT_GT(target, offsideLineX);
}

TEST(GoalPoacherTest, WorksMirroredForTheOtherTeam) {
  const float offsideLineX = 20.0f;
  const float target =
      PlayerTraits::GetPoacherTargetX(PlayerTraits::e_Trait_GoalPoacher, 5.0f, offsideLineX, -1);
  EXPECT_LT(target, offsideLineX);  // onside is the -x side for side -1
  EXPECT_NEAR(target, offsideLineX - PlayerTraits::poacherOffsideCushion, 1e-5f);
}

TEST(GoalPoacherTest, PlayersWithoutTheCardKeepTheirFormationPosition) {
  EXPECT_FLOAT_EQ(PlayerTraits::GetPoacherTargetX(PlayerTraits::traitMaskNone, -5.0f, -20.0f, 1),
                  -5.0f);
  EXPECT_FLOAT_EQ(
      PlayerTraits::GetPoacherTargetX(PlayerTraits::e_Trait_TargetMan, -5.0f, -20.0f, 1), -5.0f);
}

TEST(CreativePlaymakerTest, WeightsSpaceMoreHeavilyWhenRatingSpots) {
  const float baseWeight = 0.4f;
  EXPECT_GT(PlayerTraits::GetSpaceRatingWeight(PlayerTraits::e_Trait_CreativePlaymaker, baseWeight),
            baseWeight);
  EXPECT_FLOAT_EQ(PlayerTraits::GetSpaceRatingWeight(PlayerTraits::traitMaskNone, baseWeight),
                  baseWeight);
  EXPECT_FLOAT_EQ(PlayerTraits::GetSpaceRatingWeight(PlayerTraits::e_Trait_GoalPoacher, baseWeight),
                  baseWeight);
}

TEST(CreativePlaymakerTest, SpaceWeightStaysNormalized) {
  EXPECT_LE(PlayerTraits::GetSpaceRatingWeight(PlayerTraits::e_Trait_CreativePlaymaker, 0.9f),
            1.0f);
  EXPECT_GE(PlayerTraits::GetSpaceRatingWeight(PlayerTraits::e_Trait_CreativePlaymaker, 0.0f),
            0.0f);
}

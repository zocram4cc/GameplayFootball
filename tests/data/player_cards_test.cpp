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

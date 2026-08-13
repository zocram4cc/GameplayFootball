// Tests for the knobs that decide how open a match feels: how far out players
// shoot, how readily they shoot, and how often a keeper gets across.

#include <gtest/gtest.h>

#include "base/properties.hpp"
#include "onthepitch/gameplaytuning.hpp"

using blunted::Properties;

TEST(GameplayTuningTest, DefaultsOpenTheGameUpComparedToTheStockEngine) {
  const Properties config;
  // The stock engine only shot from inside a 16 metre window.
  EXPECT_GT(GameplayTuning::GetShootingRange(config), 16.0f);
  EXPECT_GT(GameplayTuning::GetShotAppetite(config), 1.0f);
}

TEST(GameplayTuningTest, TheKnobsAreConfigurableAndClamped) {
  Properties config;
  config.Set("gameplay_shooting_range", 22.0f);
  config.Set("gameplay_shot_appetite", 1.8f);
  EXPECT_FLOAT_EQ(GameplayTuning::GetShootingRange(config), 22.0f);
  EXPECT_FLOAT_EQ(GameplayTuning::GetShotAppetite(config), 1.8f);

  Properties silly;
  silly.Set("gameplay_shooting_range", 500.0f);
  silly.Set("gameplay_shot_appetite", -3.0f);
  EXPECT_LE(GameplayTuning::GetShootingRange(silly), 45.0f);
  EXPECT_GE(GameplayTuning::GetShotAppetite(silly), 0.5f);
}

TEST(GameplayTuningKeeperTest, SharperKeepersGetAcrossMoreOften) {
  const Properties config;
  EXPECT_GT(GameplayTuning::GetKeeperSaveChance(config, 1.0f),
            GameplayTuning::GetKeeperSaveChance(config, 0.0f));
}

TEST(GameplayTuningKeeperTest, EvenTheBestKeeperCanBeBeaten) {
  const Properties config;
  EXPECT_LT(GameplayTuning::GetKeeperSaveChance(config, 1.0f), 1.0f);
  EXPECT_GT(GameplayTuning::GetKeeperSaveChance(config, 0.0f), 0.0f);
}

TEST(GameplayTuningKeeperTest, TheKeeperKnobScalesTheWholeRange) {
  Properties generous;
  generous.Set("gameplay_keeper_sharpness", 0.4f);
  Properties stingy;
  stingy.Set("gameplay_keeper_sharpness", 1.0f);

  EXPECT_LT(GameplayTuning::GetKeeperSaveChance(generous, 0.7f),
            GameplayTuning::GetKeeperSaveChance(stingy, 0.7f));
}

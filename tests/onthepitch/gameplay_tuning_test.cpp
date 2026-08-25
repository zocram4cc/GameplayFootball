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

// A keeper who only tries for one shot in four does not look like a keeper: he
// stands and watches, which is what "very disinclined to dive" was. Whether he
// *reaches* the ball is for the save animation to decide - it only picks one
// that can get there - so the attempt itself should be close to universal.

TEST(GameplayTuningKeeperTest, EvenAPoorKeeperTriesForMostShots) {
  const Properties config;
  EXPECT_GT(GameplayTuning::GetKeeperSaveChance(config, 0.2f), 0.7f);
}

TEST(GameplayTuningKeeperTest, AGoodKeeperTriesForNearlyEverything) {
  const Properties config;
  EXPECT_GT(GameplayTuning::GetKeeperSaveChance(config, 0.9f), 0.8f);
}

// The reaction stat still separates keepers, just not by whether they bother.
TEST(GameplayTuningKeeperTest, ReactionStillSeparatesKeepersWithoutFreezingThem) {
  const Properties config;
  const float poor = GameplayTuning::GetKeeperSaveChance(config, 0.1f);
  const float great = GameplayTuning::GetKeeperSaveChance(config, 1.0f);
  EXPECT_GT(great, poor);
  EXPECT_LT(great - poor, 0.3f) << "the gap should be a shade, not a wall";
}
TEST(GameplayTuningTrapTest, SupportWebImprovesTrapPrediction) {
  EXPECT_LT(GameplayTuning::GetTrapPredictionAssist(0.20f), 0.95f);
  EXPECT_LT(GameplayTuning::GetTrapPredictionAssist(0.20f),
            GameplayTuning::GetTrapPredictionAssist(1.0f));
}

// The assist used to be multiplied in before the 0..1 difficulty clamps, so
// whenever a receiver's difficulty factors were saturated (fast ball, far
// offset - exactly the tight-web case) the clamp swallowed it whole. It must
// survive saturation: applied last, it always bites.

TEST(GameplayTuningTrapTest, AssistSurvivesSaturatedDifficultyFactors) {
  float distanceFactor = 1.0f;
  float heightFactor = 1.0f;
  float ballMovementFactor = 0.9f;
  GameplayTuning::ApplyTrapPredictionAssist(distanceFactor, heightFactor,
                                            ballMovementFactor, 0.20f);
  EXPECT_LT(distanceFactor, 1.0f);
  EXPECT_LT(heightFactor, 1.0f);
  EXPECT_LT(ballMovementFactor, 0.9f);
}

TEST(GameplayTuningTrapTest, AssistNeverAmplifiesOrNegates) {
  float distanceFactor = 0.0f;
  float heightFactor = 0.7f;
  float ballMovementFactor = 0.5f;
  GameplayTuning::ApplyTrapPredictionAssist(distanceFactor, heightFactor,
                                            ballMovementFactor, 1.0f);
  EXPECT_FLOAT_EQ(distanceFactor, 0.0f);  // nothing to ease stays eased to nothing
  EXPECT_FLOAT_EQ(heightFactor, 0.7f);    // wide web: identity
  EXPECT_FLOAT_EQ(ballMovementFactor, 0.5f);

  GameplayTuning::ApplyTrapPredictionAssist(distanceFactor, heightFactor,
                                            ballMovementFactor, 0.20f);
  EXPECT_GT(heightFactor, 0.0f);
  EXPECT_LT(heightFactor, 0.7f);
}

TEST(GameplayTuningTrapTest, TighterWebEasesMore) {
  float tightD = 1.0f, tightH = 1.0f, tightM = 0.9f;
  float wideD = 1.0f, wideH = 1.0f, wideM = 0.9f;
  GameplayTuning::ApplyTrapPredictionAssist(tightD, tightH, tightM, 0.20f);
  GameplayTuning::ApplyTrapPredictionAssist(wideD, wideH, wideM, 1.0f);
  EXPECT_LT(tightD, wideD);
  EXPECT_LT(tightH, wideH);
}

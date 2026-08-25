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

// Trap failures dominate the pass breakdown (trap 9-24 per side vs intercept
// 4-6), and most of them never reach a trap anim at all: a ball landing more
// than the stock 0.4 m from the anim's touch point is simply untouchable. A
// configurable catch radius lets receivers chest/body-trap balls that would
// otherwise sail through - the same generosity PES receivers get. General:
// every philosophy and every skill tier profits, positioning not tiers.

TEST(GameplayTuningTrapTest, TrapCatchRadiusDefaultIsMoreGenerousThanStock) {
  const blunted::Properties config;
  EXPECT_GT(GameplayTuning::GetTrapTouchableDistance(config), 0.4f);
}

TEST(GameplayTuningTrapTest, TrapCatchRadiusIsConfigurableAndClamped) {
  blunted::Properties config;
  config.Set("gameplay_trap_touchable_distance", blunted::real(0.5f));
  EXPECT_FLOAT_EQ(GameplayTuning::GetTrapTouchableDistance(config), 0.5f);

  config.Set("gameplay_trap_touchable_distance", blunted::real(0.05f));
  EXPECT_FLOAT_EQ(GameplayTuning::GetTrapTouchableDistance(config), 0.2f);

  config.Set("gameplay_trap_touchable_distance", blunted::real(9.0f));
  EXPECT_FLOAT_EQ(GameplayTuning::GetTrapTouchableDistance(config), 1.0f);
}

// The touch check that follows the catch radius is still binary: miss the
// window by a hair and the ball gets no touch event at all, it simply runs
// through. That gate is the dominant, untouched sink in the pass-failure
// breakdown (trap 8-24 per side). Headless probing of a live match found the
// gate rejecting touches that missed the 0.8 m window by only centimetres
// even on a slow ball (dist 0.84 m, speed 4.45 m/s) - animation-blend slop
// that has nothing to do with ball speed - so the gate needs a modest
// baseline on top of the speed-proportional widening: a fast ball is still
// harder to line up exactly and earns extra slack.

TEST(GameplayTuningTrapTest, AcceptGateHasAModestBaselineEvenForASlowBall) {
  EXPECT_NEAR(GameplayTuning::GetTrapAcceptGateScale(2.0f), 1.15f, 0.001f);
}

TEST(GameplayTuningTrapTest, AcceptGateWidensForFastBallsButNeverExplodes) {
  float slow = GameplayTuning::GetTrapAcceptGateScale(2.0f);
  float fast = GameplayTuning::GetTrapAcceptGateScale(16.0f);
  EXPECT_GT(fast, slow);
  EXPECT_LE(GameplayTuning::GetTrapAcceptGateScale(200.0f), 1.5f);
}

// The exact near-miss a headless probe caught live: a slow ball (4.45 m/s)
// landed 0.84 m from the touch point with a 0.11 m height delta, just
// outside the stock 0.8 m window, so the stock gate discarded it with no
// touch at all even though speed was not the problem. The softened gate
// must still accept it.
TEST(GameplayTuningTrapTest, ProbeObservedNearMissThatUsedToVanish) {
  const float touchableDistance = 0.8f;
  const float heightThreshold = 1.0f;
  const float ballSpeed = 4.45f;
  const float scale = GameplayTuning::GetTrapAcceptGateScale(ballSpeed);

  const float fullBallDistance = 0.8427f;
  const float heightDelta = 0.112f;

  EXPECT_FALSE(fullBallDistance < touchableDistance);  // stock gate: no touch
  EXPECT_TRUE(fullBallDistance < touchableDistance * scale);   // softened: touch
  EXPECT_TRUE(heightDelta < heightThreshold * scale);
}

// A genuinely bad miss (over a metre off) must stay a miss regardless of
// speed: the gate eases near-misses, it does not start catching balls that
// sail well past the receiver.
TEST(GameplayTuningTrapTest, AcceptGateNeverRescuesAGenuinelyBadMiss) {
  const float touchableDistance = 0.8f;
  const float scale = GameplayTuning::GetTrapAcceptGateScale(7.72f);
  EXPECT_FALSE(1.219f < touchableDistance * scale);
}

// A short pass is a ground pass. The stock touch code added up to 5 m/s of
// vertical velocity on a bad roll (difficultyFactor * 5.0 * random(0.2, 1)),
// lofting exactly the passes whose receivers gate on |height delta| < 1.0 m
// and a sub-metre touch radius - a self-inflicted trap failure. A misplayed
// short pass should stay on the deck (wrong direction, wrong weight), while
// aerial balls keep the full loft error.

TEST(GameplayTuningPassTest, MisplayedShortPassesStayOnTheDeck) {
  const float difficulty = 0.6f;
  EXPECT_LT(GameplayTuning::GetPassErrorLoft(difficulty, true),
            GameplayTuning::GetPassErrorLoft(difficulty, false));
  // Even a fully botched ground pass must not clear the receiver's 1 m
  // height gate on its own.
  EXPECT_LE(GameplayTuning::GetPassErrorLoft(1.0f, true), 1.5f);
}

TEST(GameplayTuningPassTest, PassErrorLoftScalesWithDifficulty) {
  EXPECT_LT(GameplayTuning::GetPassErrorLoft(0.1f, true),
            GameplayTuning::GetPassErrorLoft(0.9f, true));
  EXPECT_FLOAT_EQ(GameplayTuning::GetPassErrorLoft(0.0f, false), 0.0f);
}

// The passing odds only priced the lane (interception); a target with a
// marker on his shoulder scored the same as a free man, so the passer kept
// picking marked men and the receiving-end failure never entered the choice.

TEST(GameplayTuningPassTest, MarkedReceiversAreWorseTargets) {
  EXPECT_GT(GameplayTuning::GetReceiverPressureDanger(0.5f),
            GameplayTuning::GetReceiverPressureDanger(4.0f));
  EXPECT_FLOAT_EQ(GameplayTuning::GetReceiverPressureDanger(10.0f), 0.0f);
}

TEST(GameplayTuningPassTest, ReceiverPressureNeverDominatesTheLane) {
  EXPECT_LE(GameplayTuning::GetReceiverPressureDanger(0.0f), 0.5f);
  EXPECT_GE(GameplayTuning::GetReceiverPressureDanger(3.0f), 0.0f);
}

// A panic pass used to be a blind hoof in one fixed direction - a gift to the
// nearest interceptor at exactly the moments the carrier is most vulnerable.
// The controller now probes three lanes and kicks into the safest; the pick
// itself is pure: best lane wins, but if every lane is hopeless the original
// desperate clearance stands (a defender's hoefunction must survive).

TEST(GameplayTuningPanicTest, PanicPickTakesTheSafestLane) {
  const float odds[3] = {0.2f, 0.8f, 0.5f};
  EXPECT_EQ(GameplayTuning::GetSafestPanicLane(odds), 1);
  const float left[3] = {0.9f, 0.1f, 0.4f};
  EXPECT_EQ(GameplayTuning::GetSafestPanicLane(left), 0);
}

TEST(GameplayTuningPanicTest, DesperateClearanceSurvivesHopelessLanes) {
  const float blocked[3] = {0.05f, 0.14f, 0.0f};
  EXPECT_EQ(GameplayTuning::GetSafestPanicLane(blocked), 1);
}

// The receive blend preserves incoming momentum when the touch is mistimed
// (bumpyRideBias -> 1), which is fair for a jogged pass but absurd for a
// rocket: a slightly late touch on a 20 m/s ball kept nearly the full 20 m/s
// and rolled away - the dominant bad-trap failure. Hard incoming balls must
// be damped; soft ones keep the stock feel.

TEST(GameplayTuningTrapTest, HardIncomingPassesAreKilledNotPreserved) {
  EXPECT_FLOAT_EQ(GameplayTuning::GetTrapKillStrength(6.0f), 0.0f);
  EXPECT_GT(GameplayTuning::GetTrapKillStrength(20.0f),
            GameplayTuning::GetTrapKillStrength(8.0f));
}

TEST(GameplayTuningTrapTest, ALateTouchOnARocketKeepsAtMost60Percent) {
  const float bias = 0.8f;  // a badly mistimed touch
  const float preserved = bias * (1.0f - GameplayTuning::GetTrapKillStrength(20.0f));
  EXPECT_LE(preserved, 0.6f);
  // A soft pass is untouched by the kill term.
  EXPECT_FLOAT_EQ(0.8f * (1.0f - GameplayTuning::GetTrapKillStrength(5.0f)), 0.8f);
}

// The pass lead model saturated at 0.7 s for any pass beyond ~14 m, while a
// 15 m ground pass takes ~2.5 s to arrive: balls were led for a third of
// their flight and landed in space the receiver had already left. The lead
// must keep growing with distance - no saturation.

TEST(GameplayTuningPassTest, PassLeadKeepsGrowingWithDistance) {
  EXPECT_FLOAT_EQ(GameplayTuning::GetPassLeadTime(0.0f), 0.3f);
  // Constant effective speed means linear growth; the property that matters
  // is no saturation: the second 15 m must add at least as much lead as the
  // first.
  EXPECT_GE(GameplayTuning::GetPassLeadTime(30.0f) - GameplayTuning::GetPassLeadTime(15.0f),
            GameplayTuning::GetPassLeadTime(15.0f) - GameplayTuning::GetPassLeadTime(0.0f));
  // At long range the lead must far exceed the old 0.7 s saturation.
  EXPECT_GT(GameplayTuning::GetPassLeadTime(30.0f), 2.0f);
}

// A pass in flight toward me is a run-to-meet instruction: the passer leads
// by flight time, so holding the strategy route means meeting the ball in
// the wrong spot (batch 8: lead without convergence scored 47.8%, the worst
// of the series). The convergence decision is a contract in itself: only in
// open play, only for the designated receiver, only while the ball is loose,
// and only when my team played the pass.

TEST(GameplayTuningPassTest, ConvergeOnlyOnOwnTeamsLoosePassInOpenPlay) {
  EXPECT_TRUE(GameplayTuning::ShouldMeetInFlightPass(true, false, true, false, true));
  // Opponent's pass: that is defending, handled elsewhere.
  EXPECT_FALSE(GameplayTuning::ShouldMeetInFlightPass(true, false, true, false, false));
  // Ball held by anyone: no in-flight pass to meet.
  EXPECT_FALSE(GameplayTuning::ShouldMeetInFlightPass(true, false, true, true, true));
  // Not the designated receiver: keep the strategy route.
  EXPECT_FALSE(GameplayTuning::ShouldMeetInFlightPass(true, false, false, false, true));
  // Set piece or dead ball: choreography owns the run.
  EXPECT_FALSE(GameplayTuning::ShouldMeetInFlightPass(false, false, true, false, true));
  EXPECT_FALSE(GameplayTuning::ShouldMeetInFlightPass(true, true, true, false, true));
}

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

// A keeper's save is geometry, not a coin flip. The probability model that used
// to live here (GetKeeperSaveChance, one random() per shot against 0.53-0.66)
// left a third to a half of all shots unattempted whatever his quality, which
// on screen is a keeper who stands and watches while the ball goes past him
// (owner, 15-09). These tests pin the replacement's behaviour at the two ends
// every viewer recognises.

TEST(GameplayTuningShootingTest, MedalsPlaceTheirShots) {
  // The same unlucky draw, three players: a 4cc gold medal (every attribute
  // 0.99), a bronze (0.88) and a journeyman. The weight is how much of the
  // MISHIT ends up in the shot, so lower is better placed. The stock curve gave
  // the gold player 0.76 on this draw - more mishit than intention - which is
  // why medals did not read on the pitch.
  const float draw = 0.5f;
  const float gold = GameplayTuning::GetShotWorstCaseWeight(draw, 0.99f);
  const float bronze = GameplayTuning::GetShotWorstCaseWeight(draw, 0.88f);
  const float journeyman = GameplayTuning::GetShotWorstCaseWeight(draw, 0.50f);
  EXPECT_LT(gold, bronze);
  EXPECT_LT(bronze, journeyman);
  EXPECT_LT(gold, 0.05f) << "a gold medal shot goes where he aimed";
  EXPECT_GT(journeyman, 0.2f) << "an ordinary player still sprays them";
}

TEST(GameplayTuningShootingTest, NobodyIsPerfectAndNobodyIsHopeless) {
  // Bounds, over the whole draw: even the best has a bad one in him, and the
  // worst can still hit what he aimed at.
  EXPECT_GT(GameplayTuning::GetShotWorstCaseWeight(0.999f, 1.0f), 0.0f);
  EXPECT_LT(GameplayTuning::GetShotWorstCaseWeight(0.001f, 0.0f), 0.01f);
  EXPECT_FLOAT_EQ(GameplayTuning::GetShotWorstCaseWeight(0.0f, 0.5f), 0.0f);
}

TEST(GameplayTuningKeeperTest, ATameShotStraightAtHimIsAlwaysSaved) {
  // In his hands' path, 0.6 s of flight: even a keeper with no reach at all
  // covers that standing still.
  EXPECT_TRUE(GameplayTuning::KeeperReachesShot(
      0.2f, 0.6f, GameplayTuning::GetKeeperReactionTime_s(0.0f),
      GameplayTuning::GetKeeperDiveReach_m(0.0f),
      GameplayTuning::GetKeeperDiveSpeed_ms(0.0f)));
}

TEST(GameplayTuningKeeperTest, ARocketIntoTheFarCornerBeatsAnybody) {
  // 6 m away, 0.25 s of flight: the best reach in the game plus a sprinter's
  // speed is not enough, and it should not be.
  EXPECT_FALSE(GameplayTuning::KeeperReachesShot(
      6.0f, 0.25f, GameplayTuning::GetKeeperReactionTime_s(1.0f),
      GameplayTuning::GetKeeperDiveReach_m(1.0f),
      GameplayTuning::GetKeeperDiveSpeed_ms(1.0f)));
}

TEST(GameplayTuningKeeperTest, TheAttributesDecideTheShotsInBetween) {
  // A 20 m drive at 25 m/s - 0.8 s of flight - into a corner 3.5 m from where
  // he stands. The best keeper covers 2.4 m of reach plus 3.3 m of dive and
  // gets there; the worst has 0.9 m plus 1.6 m and does not.
  const float gap = 3.5f, flight = 0.8f;
  const bool great = GameplayTuning::KeeperReachesShot(
      gap, flight, GameplayTuning::GetKeeperReactionTime_s(1.0f),
      GameplayTuning::GetKeeperDiveReach_m(1.0f),
      GameplayTuning::GetKeeperDiveSpeed_ms(1.0f));
  const bool poor = GameplayTuning::KeeperReachesShot(
      gap, flight, GameplayTuning::GetKeeperReactionTime_s(0.0f),
      GameplayTuning::GetKeeperDiveReach_m(0.0f),
      GameplayTuning::GetKeeperDiveSpeed_ms(0.0f));
  EXPECT_TRUE(great);
  EXPECT_FALSE(poor);
}

TEST(GameplayTuningKeeperTest, ZeroFlightLeavesTheStandingReachNotAnEmptyGoal) {
  // The defect this pins: the reach behind the ball used to be the whole reach
  // scaled by min(1, timeToLine/latency), so a point-blank strike left the
  // keeper with almost nothing and every corner-placed shot from inside the box
  // scored (measured at 1x: 12 of 16 on-target became goals). Zero flight means
  // no DIVE, not no keeper: his standing frame is still behind the ball.
  const float flight = 0.0f, latency = GameplayTuning::GetKeeperReactionTime_s(0.9f);
  EXPECT_FLOAT_EQ(GameplayTuning::KeeperEffectiveReach_m(2.25f, flight, latency),
                  GameplayTuning::kKeeperStandingReach_m);
  // And it is a floor, not a starting point the dive can shrink below: the
  // worst keeper's dive reach is smaller than his standing frame, so he simply
  // never extends.
  EXPECT_GE(GameplayTuning::KeeperEffectiveReach_m(GameplayTuning::GetKeeperDiveReach_m(0.0f),
                                                   flight, latency),
            GameplayTuning::kKeeperStandingReach_m);
  // Monotone in the flight, ending at the untouched reach (the old
  // full-flight assertion, kept).
  const float partial = GameplayTuning::KeeperEffectiveReach_m(2.25f, 0.1f, latency);
  EXPECT_GT(partial, GameplayTuning::kKeeperStandingReach_m);
  EXPECT_LT(partial, 2.25f);
  EXPECT_FLOAT_EQ(GameplayTuning::KeeperEffectiveReach_m(2.25f, 1.0f, latency), 2.25f);
}

TEST(GameplayTuningKeeperTest, ShortFlightShrinksEffectiveReachByStat) {
  // The dive share still follows the keeper's own reaction, so a 0.9 keeper
  // gets across to a 2 m gap and a 0.6 keeper does not. No dice anywhere: same
  // inputs, same answer, every time.
  const float gap = 2.0f, flight = 0.27f;
  EXPECT_TRUE(GameplayTuning::KeeperReachesShot(
      gap, flight, GameplayTuning::GetKeeperReactionTime_s(0.9f),
      GameplayTuning::KeeperEffectiveReach_m(GameplayTuning::GetKeeperDiveReach_m(0.9f), flight,
                                             GameplayTuning::GetKeeperReactionTime_s(0.9f)),
      GameplayTuning::GetKeeperDiveSpeed_ms(0.9f)));
  EXPECT_FALSE(GameplayTuning::KeeperReachesShot(
      gap, flight, GameplayTuning::GetKeeperReactionTime_s(0.6f),
      GameplayTuning::KeeperEffectiveReach_m(GameplayTuning::GetKeeperDiveReach_m(0.6f), flight,
                                             GameplayTuning::GetKeeperReactionTime_s(0.6f)),
      GameplayTuning::GetKeeperDiveSpeed_ms(0.6f)));
  // A shot straight at him with time is saved by anyone.
  EXPECT_TRUE(GameplayTuning::KeeperReachesShot(
      0.2f, 0.6f, GameplayTuning::GetKeeperReactionTime_s(0.0f),
      GameplayTuning::KeeperEffectiveReach_m(GameplayTuning::GetKeeperDiveReach_m(0.0f), 0.6f,
                                             GameplayTuning::GetKeeperReactionTime_s(0.0f)),
      GameplayTuning::GetKeeperDiveSpeed_ms(0.0f)));
}

TEST(GameplayTuningKeeperTest, ReachAndCatchingRiseWithTheAttribute) {
  EXPECT_GT(GameplayTuning::GetKeeperDiveReach_m(1.0f),
            GameplayTuning::GetKeeperDiveReach_m(0.0f));
  EXPECT_GT(GameplayTuning::GetKeeperCatchSpeed_ms(1.0f),
            GameplayTuning::GetKeeperCatchSpeed_ms(0.0f));
}

TEST(GameplayTuningKeeperTest, EvenTheBestKeeperCannotHoldEverything) {
  // A 30 m/s shot is parried by anyone; a 10 m/s one is held by anyone.
  EXPECT_LT(GameplayTuning::GetKeeperCatchSpeed_ms(1.0f), 30.0f);
  EXPECT_GT(GameplayTuning::GetKeeperCatchSpeed_ms(0.0f), 10.0f);
}

TEST(GameplayTuningShootingTest, MedalsFlyTruer) {
  // The same technical_shot that places the strike also steadies the ball:
  // gold barely wobbles, a journeyman bends it halfway across the maximum,
  // and the ordering is strict between them.
  EXPECT_LT(GameplayTuning::GetShotCurveNoise(0.99f), 0.1f);
  EXPECT_GT(GameplayTuning::GetShotCurveNoise(0.0f), 0.9f);
  EXPECT_LT(GameplayTuning::GetShotCurveNoise(0.99f),
            GameplayTuning::GetShotCurveNoise(0.88f));
  EXPECT_LT(GameplayTuning::GetShotCurveNoise(0.88f),
            GameplayTuning::GetShotCurveNoise(0.50f));
  EXPECT_FLOAT_EQ(GameplayTuning::GetShotCurveNoise(0.99f), 0.05f + 0.01f * 0.95f);
}

TEST(GameplayTuningShootingTest, TechniqueKeepsTheBootFaceSquare) {
  // A cross-body strike swings off the boot; how much of that swing survives
  // is the finisher's. Gold keeps nearly none of it, a defender all of it,
  // and nobody squares the face completely.
  EXPECT_FLOAT_EQ(GameplayTuning::GetShotBodySliceShare(0.0f), 1.0f);
  EXPECT_FLOAT_EQ(GameplayTuning::GetShotBodySliceShare(1.0f),
                  GameplayTuning::kShotBodySliceFloor);
  EXPECT_LT(GameplayTuning::GetShotBodySliceShare(0.99f), 0.2f);
  EXPECT_GT(GameplayTuning::GetShotBodySliceShare(0.99f), 0.0f);
  EXPECT_LT(GameplayTuning::GetShotBodySliceShare(0.88f),
            GameplayTuning::GetShotBodySliceShare(0.50f));
}

TEST(GameplayTuningShootingTest, TheShotBarIsConfiguredAndClamped) {
  // One bar, read from the config, clamped to something a match can survive:
  // at 0 every hopeless ball is a shot, and above 0.30 only tap-ins qualify.
  blunted::Properties stock;
  EXPECT_FLOAT_EQ(GameplayTuning::GetMinShotXg(stock), GameplayTuning::kMinShotXg);
  blunted::Properties raised;
  raised.Set("gameplay_min_shot_xg", 0.20f);
  EXPECT_FLOAT_EQ(GameplayTuning::GetMinShotXg(raised), 0.20f);
  blunted::Properties silly;
  silly.Set("gameplay_min_shot_xg", 9.0f);
  EXPECT_LE(GameplayTuning::GetMinShotXg(silly), 0.30f);
  silly.Set("gameplay_min_shot_xg", -1.0f);
  EXPECT_GE(GameplayTuning::GetMinShotXg(silly), 0.01f);
}

TEST(GameplayTuningShootingTest, TheArrivalBandPutsTheBallBetweenThePosts) {
  // A flat strike from the 27.7 m median distance at 20 m/s: the band's floor
  // must lift it enough to arrive. Flight 1.385 s, so unclamped it would land
  // 9.4 m short of the line.
  const float flight = 27.7f / 20.0f;
  const float lifted = GameplayTuning::ClampShotArrivalVelocityZ(0.0f, 0.11f, flight);
  EXPECT_GT(lifted, 0.0f) << "a flat strike is raised, not left to drop";
  EXPECT_NEAR(lifted,
              GameplayTuning::ShotArrivalVelocityZ(GameplayTuning::kShotArrivalMin_m, 0.11f, flight),
              1e-4f);

  // A shot already lofted through the roof is pulled back to the top of the band.
  const float tooHigh = GameplayTuning::ShotArrivalVelocityZ(2.1f, 0.11f, flight) + 5.0f;
  const float clamped = GameplayTuning::ClampShotArrivalVelocityZ(tooHigh, 0.11f, flight);
  EXPECT_NEAR(clamped, GameplayTuning::ShotArrivalVelocityZ(2.1f, 0.11f, flight), 1e-4f);

  // A shot already inside the band is left exactly alone.
  const float inside = GameplayTuning::ShotArrivalVelocityZ(1.0f, 0.11f, flight);
  EXPECT_FLOAT_EQ(GameplayTuning::ClampShotArrivalVelocityZ(inside, 0.11f, flight), inside);
}

TEST(GameplayTuningShootingTest, TheBandIsOrderedAndHasNoFlightForNoShot) {
  const float flight = 0.6f;
  EXPECT_LT(GameplayTuning::ShotArrivalVelocityZ(GameplayTuning::kShotArrivalMin_m, 0.11f, flight),
            GameplayTuning::ShotArrivalVelocityZ(GameplayTuning::kShotArrivalMax_m, 0.11f, flight));
  EXPECT_LT(GameplayTuning::kShotArrivalMin_m, GameplayTuning::kShotArrivalMax_m);
  // No flight time: nothing to aim ballistically, so the strike is untouched.
  EXPECT_FLOAT_EQ(GameplayTuning::ClampShotArrivalVelocityZ(3.0f, 0.11f, 0.0f), 3.0f);
}

TEST(GameplayTuningKeeperTest, ReflexesShortenTheLatency) {
  EXPECT_LT(GameplayTuning::GetReactionTime_ms(1.0f), GameplayTuning::GetReactionTime_ms(0.0f));
  EXPECT_EQ(GameplayTuning::GetReactionTime_ms(0.5f), 60);
}

TEST(GameplayTuningKeeperTest, AwarenessReadsTheBallEarlierAndPanicsLess) {
  EXPECT_GT(GameplayTuning::GetKeeperAnticipation_ms(1.0f),
            GameplayTuning::GetKeeperAnticipation_ms(0.0f));
  EXPECT_EQ(GameplayTuning::GetKeeperAnticipation_ms(0.6f), 600u);
  EXPECT_LT(GameplayTuning::GetKeeperGoalMouthPanic(1.0f),
            GameplayTuning::GetKeeperGoalMouthPanic(0.0f));
  EXPECT_GE(GameplayTuning::GetKeeperGoalMouthPanic(1.0f), 1.0f)
      << "he never treats the goal as smaller than it is";
}

TEST(GameplayTuningKeeperTest, CoverageBringsHimOffHisLine) {
  EXPECT_GT(GameplayTuning::GetKeeperComeOutBias(1.0f), GameplayTuning::GetKeeperComeOutBias(0.0f));
  EXPECT_NEAR(GameplayTuning::GetKeeperComeOutBias(0.6f), 0.3f, 1e-5f);
  EXPECT_LT(GameplayTuning::GetKeeperComeOutMargin_m(1.0f),
            GameplayTuning::GetKeeperComeOutMargin_m(0.0f));
  EXPECT_GT(GameplayTuning::GetKeeperComeOutMargin_m(1.0f), 0.0f)
      << "even a sweeper keeper wants some head start";
}

TEST(GameplayTuningKeeperTest, CatchingHoldsHarderBallsAndClearingParriesFurther) {
  EXPECT_LT(GameplayTuning::GetKeeperCatchSpeed_ms(0.0f),
            GameplayTuning::GetKeeperCatchSpeed_ms(1.0f));
  EXPECT_NEAR(GameplayTuning::GetKeeperCatchSpeed_ms(0.6f), 21.6f, 1e-5f);
  EXPECT_GT(GameplayTuning::GetKeeperParryPush(1.0f), GameplayTuning::GetKeeperParryPush(0.0f));
  EXPECT_NEAR(GameplayTuning::GetKeeperParryPush(0.6f), 4.0f, 1e-5f);
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

// A pass can fail without anyone touching it: struck too hard, struck off
// line, or simply impossible to kill on arrival. The odds model priced only
// interception, so an empty forty-metre channel scored the same as an empty
// five-metre one and the AI kept choosing the long ball.

TEST(GameplayTuningPassExecutionTest, ShortPassesAreNotPenalised) {
  EXPECT_FLOAT_EQ(GameplayTuning::GetPassExecutionOdds(0.0f), 1.0f);
  EXPECT_GT(GameplayTuning::GetPassExecutionOdds(6.0f), 0.99f);
}

TEST(GameplayTuningPassExecutionTest, OddsFallWithDistance) {
  EXPECT_GT(GameplayTuning::GetPassExecutionOdds(10.0f),
            GameplayTuning::GetPassExecutionOdds(25.0f));
  EXPECT_GT(GameplayTuning::GetPassExecutionOdds(25.0f),
            GameplayTuning::GetPassExecutionOdds(45.0f));
}

// A long ball is worse than a short one, never impossible: the term must not
// zero out an option the rest of the rating might still justify.
TEST(GameplayTuningPassExecutionTest, ALongBallStaysPossible) {
  // The curve saturates at 1 - 0.85 = 0.15: worth a sixth of a free ball, never
  // literally impossible, so a rating can still justify one.
  // Saturates at 1 - 0.85 = 0.15 (a hair under, in float): worth a sixth of a
  // free ball, never literally impossible, so a rating can still justify one.
  EXPECT_NEAR(GameplayTuning::GetPassExecutionOdds(60.0f), 0.15f, 0.01f);
}

// The aim used to saturate at 0.7 s of receiver movement whatever the distance,
// so long passes were struck several metres behind a man who was still running
// and reached nobody at all. Lead has to track flight time.

TEST(GameplayTuningLeadTest, LeadGrowsWithDistance) {
  EXPECT_GT(GameplayTuning::GetReceiverLeadTime_sec(40.0f),
            GameplayTuning::GetReceiverLeadTime_sec(10.0f));
}

TEST(GameplayTuningLeadTest, ALongBallIsLedPastTheOldCeiling) {
  // The old ceiling was 0.7 s; a forty-metre ball must beat it comfortably.
  EXPECT_GT(GameplayTuning::GetReceiverLeadTime_sec(40.0f), 0.7f);
}

// Leading by the whole flight at full pace overshoots a receiver who checks his
// run - the mistake that got the first attempt reverted.
TEST(GameplayTuningLeadTest, LeadIsShorterThanTheFlightItself) {
  const float d = 30.0f;
  EXPECT_LT(GameplayTuning::GetReceiverLeadTime_sec(d),
            GameplayTuning::GetPassFlightTime_sec(d));
}

TEST(GameplayTuningTest, AGoalLineCrossingIsFoundFromTwoPhysicsSteps) {
  // 52.4 -> 52.6 straddles the line: the ball is half a metre wide of centre
  // and 1.2 m up at the moment it meets the plane.
  const GameplayTuning::GoalLineCrossing cross =
      GameplayTuning::FindGoalLineCrossing({52.4f, 0.5f, 1.0f}, {52.6f, 0.7f, 1.4f}, 52.5f);
  EXPECT_TRUE(cross.crossed);
  EXPECT_NEAR(cross.lateral_m, 0.6f, 1e-4f);
  EXPECT_NEAR(cross.height_m, 1.2f, 1e-4f);
}

TEST(GameplayTuningTest, ACrossingIsCountedOnceAndOnlyOnTheStepThrough) {
  // Both steps outside: this is the ball settling in the net, not arriving.
  EXPECT_FALSE(GameplayTuning::FindGoalLineCrossing({52.6f, 0.0f, 0.2f}, {52.9f, 0.0f, 0.2f}, 52.5f)
                   .crossed);
  // Both inside: still on its way.
  EXPECT_FALSE(GameplayTuning::FindGoalLineCrossing({40.0f, 0.0f, 0.2f}, {45.0f, 0.0f, 0.2f}, 52.5f)
                   .crossed);
}

TEST(GameplayTuningTest, TheFrameIsJudgedOnWhereTheBallGotToNotWhereItWasAimed) {
  // Over the bar and wide of the post are the two ways a shot that reached the
  // plane still is not on target. A ball that dies short never reaches it, so
  // it appears as no crossing at all rather than as a low one.
  const GameplayTuning::GoalLineCrossing over =
      GameplayTuning::FindGoalLineCrossing({52.4f, 0.0f, 2.5f}, {52.6f, 0.0f, 2.7f}, 52.5f);
  EXPECT_FALSE(GameplayTuning::CrossedInsideGoalFrame(over, 3.7f, 2.5f));
  const GameplayTuning::GoalLineCrossing wide =
      GameplayTuning::FindGoalLineCrossing({52.4f, 3.8f, 1.0f}, {52.6f, 4.0f, 1.0f}, 52.5f);
  EXPECT_TRUE(wide.crossed);
  EXPECT_FALSE(GameplayTuning::CrossedInsideGoalFrame(wide, 3.7f, 2.5f));
  const GameplayTuning::GoalLineCrossing in =
      GameplayTuning::FindGoalLineCrossing({52.4f, 3.0f, 1.0f}, {52.6f, 3.2f, 1.0f}, 52.5f);
  EXPECT_TRUE(GameplayTuning::CrossedInsideGoalFrame(in, 3.7f, 2.5f));
}

TEST(GameplayTuningTest, AStoppedBallIsJudgedOnWhereItWasGoing) {
  // The keeper's touch: the ball is at 40 m, moving toward the goal at 25 m/s,
  // half a metre wide of centre and 1 m up. It would meet the plane in 0.5 s.
  const GameplayTuning::GoalLineCrossing heading =
      GameplayTuning::PredictGoalLineCrossing({-40.0f, 0.5f, 1.0f}, {-25.0f, 0.2f, 0.4f}, 52.5f);
  EXPECT_TRUE(heading.crossed);
  EXPECT_NEAR(heading.lateral_m, 0.6f, 0.01f);
  EXPECT_NEAR(heading.height_m, 1.0f + 0.4f * 0.5f - 0.5f * 9.81f * 0.25f, 0.01f);
  EXPECT_TRUE(GameplayTuning::CrossedInsideGoalFrame(heading, 3.7f, 2.5f));
}

TEST(GameplayTuningTest, ABallGoingNowhereIsNotASave) {
  // Moving away from the goal, or barely moving: a keeper collecting either is
  // not stopping a shot on target, and this is the gate that says so.
  EXPECT_FALSE(GameplayTuning::PredictGoalLineCrossing({-40.0f, 0.0f, 0.2f}, {10.0f, 0.0f, 0.0f}, 52.5f)
                   .crossed);
  EXPECT_FALSE(GameplayTuning::PredictGoalLineCrossing({-40.0f, 0.0f, 0.2f}, {0.0f, 0.0f, 0.0f}, 52.5f)
                   .crossed);
  // A ball already level with the line has no crossing left to predict.
  EXPECT_FALSE(GameplayTuning::PredictGoalLineCrossing({-52.5f, 0.0f, 0.2f}, {-10.0f, 0.0f, 0.0f}, 52.5f)
                   .crossed);
}

TEST(GameplayTuningTest, ABallAimedOutsideTheFrameIsNotOnTarget) {
  // Heading for the line, but well wide of the post: a save it is not.
  const GameplayTuning::GoalLineCrossing wide =
      GameplayTuning::PredictGoalLineCrossing({-30.0f, 8.0f, 1.0f}, {-20.0f, 2.0f, 0.0f}, 52.5f);
  EXPECT_TRUE(wide.crossed);
  EXPECT_GT(std::fabs(wide.lateral_m), 3.7f);
  EXPECT_FALSE(GameplayTuning::CrossedInsideGoalFrame(wide, 3.7f, 2.5f));
  // And one driven over the bar is not either. This case carries gravity: from
  // 22.5 m at 20 m/s the ball is 1.1 s in the air, which costs it 6.2 m of
  // height, so 6 m/s of lift actually arrives LOW (1.54 m) and 8 m/s is what
  // clears the bar. Getting that backwards is how a "lob" test passes while
  // asserting the opposite of what the function does.
  const GameplayTuning::GoalLineCrossing driven =
      GameplayTuning::PredictGoalLineCrossing({-30.0f, 0.0f, 1.0f}, {-20.0f, 0.0f, 6.0f}, 52.5f);
  EXPECT_LT(driven.height_m, 2.5f);
  EXPECT_TRUE(GameplayTuning::CrossedInsideGoalFrame(driven, 3.7f, 2.5f));
  const GameplayTuning::GoalLineCrossing over =
      GameplayTuning::PredictGoalLineCrossing({-30.0f, 0.0f, 1.0f}, {-20.0f, 0.0f, 8.0f}, 52.5f);
  EXPECT_GT(over.height_m, 2.5f);
  EXPECT_FALSE(GameplayTuning::CrossedInsideGoalFrame(over, 3.7f, 2.5f));
}

TEST(GameplayTuningTest, AStopIsAShotOnlyFromWithinRangeOfTheGoalItIsHeadingFor) {
  // Which goal the ball is heading for comes from its velocity. Within shooting
  // range that agrees with the ball's own half by construction - a ball aimed
  // at the goal on the far side of the pitch is more than 45 m away from it, so
  // the range cap rejects it whichever way the side is derived. This pins the
  // reachable boundary, which is the one the save gate actually sees:
  //
  //   40 m out, heading for that goal -> a shot, and it was on target
  const GameplayTuning::GoalLineCrossing inRange =
      GameplayTuning::PredictGoalLineCrossing({-12.5f, 0.5f, 0.5f}, {-25.0f, 0.0f, 0.0f}, 52.5f);
  EXPECT_TRUE(inRange.crossed);
  EXPECT_NEAR(inRange.lateral_m, 0.5f, 0.01f);
  EXPECT_TRUE(GameplayTuning::CrossedInsideGoalFrame(inRange, 3.7f, 2.5f));
  //   57.5 m out at the far goal -> not a shot at all, however hard it is hit
  EXPECT_FALSE(GameplayTuning::PredictGoalLineCrossing({-5.0f, 0.0f, 0.5f}, {30.0f, 0.0f, 0.0f}, 52.5f)
                   .crossed);
  //   past the line it travels toward -> nothing left to predict
  EXPECT_FALSE(GameplayTuning::PredictGoalLineCrossing({-54.0f, 0.0f, 0.5f}, {-10.0f, 0.0f, 0.0f}, 52.5f)
                   .crossed);
}

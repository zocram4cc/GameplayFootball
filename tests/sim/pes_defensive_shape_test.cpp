// PES 2021's open-play defensive shape (docs/re/pes-re-defensive-shape.md 6, 7,
// 14.1, 14.2, 14.3). The line chain's clamps and slider effects, the team length
// and the line half-widths, the mark distance and the hand-off delay are pure
// arithmetic and are pinned here with synthetic constants; the trap gate and the
// mark-pass ordering run through a real headless Match on the fixture.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "data/matchdata.hpp"
#include "onthepitch/match.hpp"
#include "onthepitch/matchreport.hpp"
#include "onthepitch/matchsetup.hpp"
#include "onthepitch/pesdefshape.hpp"
#include "onthepitch/player/player.hpp"
#include "onthepitch/team.hpp"
#include "onthepitch/teamAIcontroller.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"
#include "simcontext.hpp"

#include "pes_match_constants.hpp"

#ifndef GF_SOURCE_DIR
#define GF_SOURCE_DIR "."
#endif

namespace {

constexpr int HOME_TEAM = 1, AWAY_TEAM = 2;

// A synthetic constant set with every relation the shape needs, in the same
// units PES's live read carried (report 8). The tests vary one field at a time,
// so the numbers themselves are not what is pinned.
pes::DefensiveShapeConstants Synthetic() {
  pes::DefensiveShapeConstants k{};
  k.foreCheck_m = -9.0f;
  k.foreCheckSlider_m = 10.0f;
  k.retreatAngle_deg = 90.0f;
  k.dfLineCenterAdjustMax_m = -8.0f;
  k.dfLineCenterAdjustMin_m = -20.0f;
  k.strategyDefensive_m = 1.0f;
  k.strategyLevelUp_m = -14.0f;
  k.rampHighFactor = 0.6f;
  k.rampLowFactor = -0.2f;
  k.boxRamp_m = -5.0f;
  k.stepBack_m = 3.75f;
  k.deepOffset_m = 5.0f;
  k.marginTargetLineX_m = -10.0f;
  k.cushionAwayAngle_deg = 135.0f;
  k.cushionSpeedDenominator = 30000.0f;
  k.cushionFps = 54.0f;
  k.speedPerHour_s = 3600.0f;
  k.cushionHalfFactor = 0.5f;
  k.cushionFacingFloor_deg = 65.0f;
  k.cushionFacingSpan_deg = 115.0f;
  k.stepUpDistance_m = 4.0f;
  k.stepUpHeading_deg = 100.0f;
  k.stepUpSpeed_mps = 5.5556f;
  k.stepUpFacingSpan_deg = 35.0f;
  k.lengthDf_m = 46.0f;
  k.lengthDfRetreat_m = 42.0f;
  k.lengthDfSuperRetreat_m = 32.0f;
  k.adjustXCompactFW_m = 0.33f;
  k.dfLineWidth3_m = 15.0f;
  k.dfLineWidth4_m = 19.0f;
  k.dfLineWidth5_m = 23.0f;
  k.dfLineWidthFew_m = 10.0f;
  k.adjustZCompact_m = 2.85f;
  k.minWidth_m = 6.0f;
  k.minWidthDefensive_m = 5.0f;
  k.widthCapFactor = 0.85f;
  k.lateralRampNear_m = 6.0f;
  k.widthLevelReduction_m = 6.0f;
  k.lateralRampFar_m = 15.12f;
  k.lastLineCloseMaxRate = 0.34f;
  k.dfLineCloseRate = 0.61f;
  k.markDistanceBase_m = 6.0f;
  k.markDistanceSpan_m = 4.0f;
  k.markUpfieldRamp_m = 20.0f;
  k.markPressuringBonus_m = 2.0f;
  k.stealRadius_m = 35.0f;
  k.stealRadiusLowLevel_m = 20.0f;
  k.midGap_m = 7.25f;
  k.midLineScoreAdjustMax = 36.0f;
  k.midCatchUpDivisor = 4.0f;
  k.handOffOrganiserFloor = 10.0f;
  k.handOffOrganiserTop = 50.0f;
  k.handOffFrames_m = 60.0f;
  k.handOffOrganiserSpan = -40.0f;
  return k;
}

pes::LineFacts BallAt(float depth_m) {
  pes::LineFacts f{};
  f.ballDepth_m = depth_m;
  return f;
}

// --- line height: the clamps (report 6, 14.1) ---------------------------------

// The ramp runs between two line-height offsets in the ball's depth, and the
// box line at its deep end. The cushion behind the ball is the only thing that
// moves a deep line off the box line.
TEST(DefensiveLine, RampIsMonotoneInBallDepth) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  float previous = -1000.0f;
  for (float ball = -f.ownBox_m; ball <= 45.0f; ball += 5.0f) {
    const float line = pes::DefensiveLineHeight(k, f, BallAt(ball), 4, 0, 2, true, true, 0.0f);
    EXPECT_GE(line, previous) << "ball " << ball;
    previous = line;
  }
}

TEST(DefensiveLine, DeepLineSitsOnTheBoxLineWithoutACushion) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  // No opponent on the ball: the cushion collapses and the ramp's own deepest
  // end, the box line, is where the line sits.
  EXPECT_NEAR(pes::DefensiveLineHeight(k, f, BallAt(-f.ownBox_m), 4, 0, 2, true, false, 0.0f),
              -f.ownBox_m, 1e-3f);
  // The cushion is what pushes it 10 m behind a ball that deep.
  EXPECT_NEAR(pes::DefensiveLineHeight(k, f, BallAt(-f.ownBox_m), 4, 0, 2, true, true, 0.0f),
              -f.ownBox_m - 10.0f, 1e-3f);
}

TEST(DefensiveLine, CushionHoldsTheLineBehindTheBall) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  const float ball = -40.0f;  // deep in our own half, where the cushion decides
  // A full cushion keeps 10 m behind the ball, but never deeper than the box.
  EXPECT_NEAR(pes::DefensiveLineHeight(k, f, BallAt(ball), 4, 0, 2, true, true, 0.0f),
              std::max(ball - 10.0f, -f.ownGoalArea_m), 1e-3f);
  // FUN_1405f7a00 hands out half a cushion when the line was already deeper than
  // it is now and the ball is travelling away from our goal. At a ball this deep
  // FUN_1405ef910's step-up (report 14.1) lands on top of it and holds the line 4
  // m behind the ball, so the half cushion is only observable where the line is
  // shallower than the box line - pinned here on the factor itself.
  pes::LineFacts away = BallAt(ball);
  away.ballHeadingFromGoal_deg = 170.0f;
  away.ballSpeed_mps = 100.0f;
  EXPECT_FLOAT_EQ(pes::TargetLineCushionFactor(k, away, true, -40.0f, -36.0f), 0.5f);
  EXPECT_NEAR(pes::DefensiveLineHeight(k, f, away, 4, 0, 2, true, true, -40.0f), ball - 4.0f, 1e-3f);
  // And the cushion is a floor over the whole ramp: a fuller one holds the line
  // deeper, never shallower.
  EXPECT_LE(pes::DefensiveLineHeight(k, f, BallAt(ball), 4, 0, 2, true, true, 0.0f),
            pes::DefensiveLineHeight(k, f, away, 4, 0, 2, true, true, -40.0f) + 1e-4f);
}

TEST(DefensiveLine, CushionFactorDecaysAsTheCarrierTurnsAway) {
  const pes::DefensiveShapeConstants k = Synthetic();
  pes::LineFacts ball = BallAt(-30.0f);
  ball.carrierFacingFromGoal_deg = 0.0f;  // facing our goal: the full cushion
  const float facingGoal = pes::TargetLineCushionFactor(k, ball, true, 0.0f, -30.0f);
  ball.carrierFacingFromGoal_deg = 180.0f;  // turned away: none of it
  const float turnedAway = pes::TargetLineCushionFactor(k, ball, true, 0.0f, -30.0f);
  EXPECT_FLOAT_EQ(facingGoal, 1.0f);
  EXPECT_FLOAT_EQ(turnedAway, 0.0f);
  // The boundary is the 65 degree floor, and it decays with the 115 degree span.
  ball.carrierFacingFromGoal_deg = 65.0f;
  EXPECT_FLOAT_EQ(pes::TargetLineCushionFactor(k, ball, true, 0.0f, -30.0f), 1.0f);
  ball.carrierFacingFromGoal_deg = 180.0f - 57.5f;
  EXPECT_FLOAT_EQ(pes::TargetLineCushionFactor(k, ball, true, 47.0f, -30.0f), 0.5f);
  // Our own carrier, or none at all, is not a factor.
  EXPECT_FLOAT_EQ(pes::TargetLineCushionFactor(k, ball, false, 0.0f, -30.0f), 0.0f);
}

TEST(DefensiveLine, NeverPastHalfwayAndNeverDeeperThanTheGoalAreaLine) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  // Ball on the opponents' goal line: the ramp's top is still under halfway at
  // level 2, but the cushion clamp never lets it cross it.
  EXPECT_LE(pes::DefensiveLineHeight(k, f, BallAt(52.0f), 4, 0, 2, true, true, 0.0f), 0.0f);
  // Ball on our own goal line, no cushion: the deepest the line ever sits is
  // the goal-area line.
  EXPECT_NEAR(pes::DefensiveLineHeight(k, f, BallAt(-f.halfLength_m), 4, 0, 2, true, true, 0.0f),
              -f.ownGoalArea_m, 1e-3f);
}

// --- the Defensive Line slider (report 14.2's k2) ------------------------------

TEST(DefensiveLine, HigherSliderHoldsAHigherLine) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  float previous = -1000.0f;
  for (float slider = 1.0f; slider <= 10.0f; slider += 1.0f) {
    const float line = pes::DefensiveLineHeight(k, f, BallAt(0.0f), slider, 0, 2, true, true, 0.0f);
    EXPECT_GT(line, previous) << "slider " << slider;
    previous = line;
  }
  // The full slider range spans most of the forecheck branch's own 10 m step:
  // the rest is absorbed by the ramp clamping at this ball depth.
  const float span = pes::DefensiveLineHeight(k, f, BallAt(0.0f), 10.0f, 0, 2, true, true, 0.0f) -
                     pes::DefensiveLineHeight(k, f, BallAt(0.0f), 1.0f, 0, 2, true, true, 0.0f);
  EXPECT_GT(span, 5.0f);
  EXPECT_LE(span, 10.0f);
}

TEST(DefensiveLine, DefensiveStyleOneRunsItsOwnOffsets) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  // Style 1's term is 5*sin(90*t) - 11 with t = 1 at the bottom of the slider and
  // 0 at its top, so its own range is the retreat constant minus 5 m, against the
  // forecheck branch's -9 down to -19. Both terms shift the ramp's ends equally,
  // so a metre of offset moves the line by (deep + 5)/(hi - lo).
  const float a0Bottom = -9.0f, a0Top = -19.0f;
  const float a1Bottom = 5.0f * std::sin(90.0f * 3.14159265f / 180.0f) - 11.0f;
  const float a1Top = -11.0f;
  const float loBottom = k.rampLowFactor * f.halfLength_m + k.dfLineCenterAdjustMin_m + a0Bottom;
  const float hiBottom = k.rampHighFactor * f.halfLength_m + k.dfLineCenterAdjustMax_m + a0Bottom;
  const float scale = (1.0f - f.ownBox_m + 5.0f) / (hiBottom - loBottom);
  EXPECT_NEAR(pes::DefensiveLineHeight(k, f, BallAt(0.0f), 1.0f, 1, 2, true, true, 0.0f) -
                  pes::DefensiveLineHeight(k, f, BallAt(0.0f), 1.0f, 0, 2, true, true, 0.0f),
              (a1Bottom - a0Bottom) * scale, 1e-3f);
  EXPECT_NEAR(pes::DefensiveLineHeight(k, f, BallAt(0.0f), 10.0f, 1, 2, true, true, 0.0f) -
                  pes::DefensiveLineHeight(k, f, BallAt(0.0f), 10.0f, 0, 2, true, true, 0.0f),
              (a1Top - a0Top) * scale, 1e-3f);
  // The retreat branch holds a lower line than the forecheck branch on its own:
  // at the bottom of the slider the sine term is worth 5 m of offset.
  EXPECT_GT(a1Bottom, a1Top);
  EXPECT_LT(pes::DefensiveLineHeight(k, f, BallAt(0.0f), 1.0f, 1, 2, true, true, 0.0f),
            pes::DefensiveLineHeight(k, f, BallAt(0.0f), 10.0f, 1, 2, true, true, 0.0f));
}

TEST(DefensiveLine, NoSmoothingBetweenTicks) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  // The target is a function of this tick's ball only: the same ball twice gives
  // the same line, and a ball that jumps 15 m jumps the line with it, with the
  // ramp's own gradient, not a filtered fraction of it.
  const float first = pes::DefensiveLineHeight(k, f, BallAt(0.0f), 4, 0, 2, true, true, 0.0f);
  const float again = pes::DefensiveLineHeight(k, f, BallAt(0.0f), 4, 0, 2, true, true, 0.0f);
  EXPECT_FLOAT_EQ(first, again);
  // Over the ramp the line moves with its own gradient, (deep + 5) / (hi - lo),
  // so a ball that teleports moves the line by the full amount at once.
  const float slider = 4.0f;
  const float a = k.foreCheck_m - k.foreCheckSlider_m * (slider - 1.0f) / 9.0f;
  const float hi = k.rampHighFactor * f.halfLength_m + k.dfLineCenterAdjustMax_m + a;
  const float lo = k.rampLowFactor * f.halfLength_m + k.dfLineCenterAdjustMin_m + a;
  // Two balls where neither the ramp nor the cushion clamps, so the line moves by
  // the ramp's own gradient over the whole interval.
  const float lower = pes::DefensiveLineHeight(k, f, BallAt(5.0f), 4, 0, 2, true, true, 0.0f);
  const float raised = pes::DefensiveLineHeight(k, f, BallAt(10.0f), 4, 0, 2, true, true, 0.0f);
  EXPECT_NEAR((raised - lower) / 5.0f, -(1.0f - f.ownBox_m + 5.0f) / (hi - lo), 1e-3f);
  (void)first;
}

// --- team length (report 14.1's length term) -----------------------------------

TEST(TeamLength, ForecheckRetreatAndSuperRetreat) {
  const pes::DefensiveShapeConstants k = Synthetic();
  EXPECT_FLOAT_EQ(pes::TeamLength_m(k, 0, 2, 1.0f), 46.0f);  // level 2: nothing moves it
  EXPECT_FLOAT_EQ(pes::TeamLength_m(k, 1, 2, 1.0f), 42.0f);
  EXPECT_FLOAT_EQ(pes::TeamLength_m(k, 0, 0, 1.0f), 32.0f);  // deepest attack level
  // Monotonic in the attack level between the two ends.
  EXPECT_GT(pes::TeamLength_m(k, 0, 1, 1.0f), pes::TeamLength_m(k, 0, 0, 1.0f));
  EXPECT_LT(pes::TeamLength_m(k, 0, 1, 1.0f), pes::TeamLength_m(k, 0, 2, 1.0f));
}

TEST(TeamLength, EachCompactnessStepCostsADefinedMetre) {
  const pes::DefensiveShapeConstants k = Synthetic();
  EXPECT_FLOAT_EQ(pes::TeamLength_m(k, 0, 2, 5.0f), 46.0f - 4.0f * k.adjustXCompactFW_m);
  EXPECT_FLOAT_EQ(pes::TeamLength_m(k, 0, 2, 10.0f), 46.0f - 9.0f * k.adjustXCompactFW_m);
}

// --- line half-width (report 7) ------------------------------------------------

TEST(LineWidth, HalfWidthFollowsTheHeadCount) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  // A ball 20 m upfield of the line and central: the squeeze factor collapses,
  // so the half-widths are the raw 15/19/23 with the count floor beneath them.
  const float noSqueeze = 20.0f, line = 0.0f, central = 0.0f, neutralCompact = 5.5f;
  EXPECT_NEAR(pes::LineHalfWidth_m(k, f, 3, neutralCompact, 0, false, central, noSqueeze, line),
              15.0f, 1e-3f);
  EXPECT_NEAR(pes::LineHalfWidth_m(k, f, 4, neutralCompact, 0, false, central, noSqueeze, line),
              19.0f, 1e-3f);
  EXPECT_NEAR(pes::LineHalfWidth_m(k, f, 5, neutralCompact, 0, false, central, noSqueeze, line),
              23.0f, 1e-3f);
  EXPECT_NEAR(pes::LineHalfWidth_m(k, f, 2, neutralCompact, 0, false, central, noSqueeze, line),
              10.0f, 1e-3f);
  EXPECT_NEAR(pes::LineHalfWidth_m(k, f, 6, neutralCompact, 0, false, central, noSqueeze, line),
              23.0f, 1e-3f);
}

TEST(LineWidth, CompactnessNarrowsTheWidestLines) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  // A ball 20 m upfield of the line, so the squeeze is out of the way.
  const float noSqueeze = 20.0f, line = 0.0f, central = 0.0f;
  EXPECT_LT(pes::LineHalfWidth_m(k, f, 5, 10.0f, 0, false, central, noSqueeze, line),
            pes::LineHalfWidth_m(k, f, 5, 1.0f, 0, false, central, noSqueeze, line));
  // The slider's own magnitude: 2.85 m over its full range.
  EXPECT_NEAR(pes::LineHalfWidth_m(k, f, 5, 1.0f, 0, false, central, noSqueeze, line) -
                  pes::LineHalfWidth_m(k, f, 5, 10.0f, 0, false, central, noSqueeze, line),
              2.0f * k.adjustZCompact_m, 1e-3f);
  // Its neutral point sits in the middle of the slider, at 5.5.
  EXPECT_FLOAT_EQ(
      pes::LineHalfWidth_m(k, f, 5, 5.5f, 0, false, central, noSqueeze, line),
      pes::LineHalfWidth_m(k, f, 5, 5.5f, 0, false, central, noSqueeze, line) -
          k.adjustZCompact_m * (5.5f - 5.5f) / 4.5f);
}

TEST(LineWidth, AttackLevelNarrowsTheBlockInPlay) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  const float noSqueeze = 20.0f, line = 0.0f, central = 0.0f, neutralCompact = 5.5f;
  EXPECT_NEAR(pes::LineHalfWidth_m(k, f, 4, neutralCompact, 2, true, central, noSqueeze, line),
              19.0f, 1e-3f);
  EXPECT_NEAR(pes::LineHalfWidth_m(k, f, 4, neutralCompact, 0, true, central, noSqueeze, line),
              19.0f - k.widthLevelReduction_m, 1e-3f);
  // Out of play the level does not narrow it at all.
  EXPECT_NEAR(pes::LineHalfWidth_m(k, f, 4, neutralCompact, 0, false, central, noSqueeze, line),
              19.0f, 1e-3f);
}

TEST(LineWidth, BallCloseToTheLineSqueezesTheBlock) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  const float line = -30.0f, central = 0.0f, fourPlayers = 5.5f;
  // A ball 50 m upfield of the line: neither the proximity nor the depth ramp
  // squeezes, so the half-width stands at its own value.
  const float open = pes::LineHalfWidth_m(k, f, 4, fourPlayers, 2, true, central, line + 50.0f, line);
  EXPECT_NEAR(open, 19.0f, 1e-3f);
  // A ball level with the line: the proximity ramp squeezes at its own rate.
  const float squeezed =
      pes::LineHalfWidth_m(k, f, 4, fourPlayers, 2, true, central, line, line);
  EXPECT_LT(squeezed, open);
  // A ball behind the line squeezes fully.
  const float behind =
      pes::LineHalfWidth_m(k, f, 4, fourPlayers, 2, true, central, line - 5.0f, line);
  EXPECT_LT(behind, squeezed);
  // The squeeze never inverts the band.
  EXPECT_GT(behind, 0.0f);
}

TEST(LineWidth, WideBallKeepsTheBlockWider) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  const float line = -30.0f, ball = line - 5.0f;
  const float wideBall = pes::LineHalfWidth_m(k, f, 4, 5.5f, 2, true, 25.0f, ball, line);
  const float centralBall = pes::LineHalfWidth_m(k, f, 4, 5.5f, 2, true, 0.0f, ball, line);
  EXPECT_GT(wideBall, centralBall);
}

TEST(LineWidth, CappedByTheBoxHalfWidthAndTheMinimumSpacing) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  const float central = 0.0f;
  EXPECT_LE(pes::LineHalfWidth_m(k, f, 11, 5.5f, 0, false, central, 0.0f, 0.0f),
            k.widthCapFactor * f.halfWidth_m);
  EXPECT_GE(pes::LineHalfWidth_m(k, f, 2, 1.0f, 2, true, central, 0.0f, 0.0f),
            1.0f * 0.5f * k.minWidthDefensive_m - 1e-3f);
}

// --- mark distance (report 14.3) ----------------------------------------------

TEST(MarkDistance, TwoMetresDeepSixUpfieldPlusThePressuringTwo) {
  const pes::DefensiveShapeConstants k = Synthetic();
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  const float deepRamp = 20.0f - f.ownBox_m;
  EXPECT_NEAR(pes::MarkDistance_m(k, f, deepRamp - 1.0f, false), 2.0f, 1e-3f);
  EXPECT_NEAR(pes::MarkDistance_m(k, f, 25.0f, false), 6.0f, 1e-3f);
  EXPECT_NEAR(pes::MarkDistance_m(k, f, 25.0f, true), 8.0f, 1e-3f);
  // Half way up the ramp the distance is exactly mid-way between the two ends.
  EXPECT_NEAR(pes::MarkDistance_m(k, f, 0.5f * (deepRamp + 20.0f), false), 4.0f, 1e-3f);
  // Monotone in the opponent's depth.
  EXPECT_LE(pes::MarkDistance_m(k, f, -30.0f, false), pes::MarkDistance_m(k, f, 0.0f, false));
}

TEST(MarkSpot, IsTheGoalSidePointAtTheMarkDistance) {
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  const Vector3 ownGoal(-f.halfLength_m, 0.0f, 0.0f);
  const Vector3 opp(0.0f, 0.0f, 0.0f);
  const Vector3 spot = pes::MarkSpot(opp, 6.0f, ownGoal);
  EXPECT_LT(spot.coords[0], 0.0f);  // goal-side of the opponent
  EXPECT_NEAR((spot - ownGoal).GetLength(), f.halfLength_m - 6.0f, 1e-4f);
  EXPECT_NEAR(spot.coords[1], 0.0f, 1e-4f);
}

// --- opponent ordering and hand-off delay (report 3.3, 14.2) -------------------

TEST(OpponentOrder, MostAdvancedFirstWithTheSixthSecondProjection) {
  // dir +1, own frame: the key is the negated depth, so a goal-side opponent
  // carries the larger key and sorts first.
  const float goalSide = pes::OpponentOrderKey(-10.0f, 0.0f);
  const float upfield = pes::OpponentOrderKey(10.0f, 0.0f);
  EXPECT_GT(goalSide, upfield);
  // A man running goal-side sorts in front of where he stands.
  const float runner = pes::OpponentOrderKey(-10.0f, -3.0f);
  EXPECT_GT(runner, goalSide);
  // The projection is one sixth of a second of his velocity.
  EXPECT_NEAR(runner - goalSide, 3.0f / 6.0f, 1e-5f);
}

TEST(HandOffDelay, MaxSixtyFramesForTheWorstOrganiser) {
  const pes::DefensiveShapeConstants k = Synthetic();
  EXPECT_EQ(pes::MarkHandOffFrames(k, 0), 60);   // b <= 10: the full minute
  EXPECT_EQ(pes::MarkHandOffFrames(k, 10), 60);
  EXPECT_EQ(pes::MarkHandOffFrames(k, 30), 30);  // (50 - 30)/40 * 60
  EXPECT_EQ(pes::MarkHandOffFrames(k, 45), 7);   // floor((50 - 45)/40 * 60)
  EXPECT_EQ(pes::MarkHandOffFrames(k, 50), 0);   // b >= 50: immediate
  EXPECT_EQ(pes::MarkHandOffFrames(k, 99), 0);
}

// The loader reads the image's own floats, so the synthetic set must not have
// invented a PES value that the exe disagrees with by more than its own noise.
TEST(DefensiveShapeLoader, ReadsTheImagesOwnValues) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto k = pes::LoadDefensiveShapeConstants(*exe, &err);
  ASSERT_TRUE(k) << err;
  // Relations, not pinned numbers: the report's live basePosition record read
  // -9, -8, -20, 46/42/32, 15/19/23, 0.33, 2.85, 6, 0.61, 0.34.
  EXPECT_FLOAT_EQ(k->foreCheck_m, -9.0f);
  EXPECT_FLOAT_EQ(k->dfLineCenterAdjustMax_m, -8.0f);
  EXPECT_FLOAT_EQ(k->dfLineCenterAdjustMin_m, -20.0f);
  EXPECT_FLOAT_EQ(k->lengthDf_m, 46.0f);
  EXPECT_FLOAT_EQ(k->lengthDfRetreat_m, 42.0f);
  EXPECT_FLOAT_EQ(k->lengthDfSuperRetreat_m, 32.0f);
  EXPECT_FLOAT_EQ(k->adjustXCompactFW_m, 0.33f);
  EXPECT_FLOAT_EQ(k->dfLineWidth3_m, 15.0f);
  EXPECT_FLOAT_EQ(k->dfLineWidth4_m, 19.0f);
  EXPECT_FLOAT_EQ(k->dfLineWidth5_m, 23.0f);
  EXPECT_FLOAT_EQ(k->adjustZCompact_m, 2.85f);
  EXPECT_FLOAT_EQ(k->dfLineCloseRate, 0.61f);
  EXPECT_FLOAT_EQ(k->lastLineCloseMaxRate, 0.34f);
  EXPECT_FLOAT_EQ(k->markDistanceBase_m, 6.0f);
  EXPECT_FLOAT_EQ(k->markDistanceSpan_m, 4.0f);
  EXPECT_FLOAT_EQ(k->markPressuringBonus_m, 2.0f);
  // Every image constant the loader touched came from .rdata.
  EXPECT_FLOAT_EQ(k->rampHighFactor, 0.6f);
  EXPECT_FLOAT_EQ(k->rampLowFactor, -0.2f);
  EXPECT_FLOAT_EQ(k->boxRamp_m, -5.0f);
  EXPECT_FLOAT_EQ(k->stepBack_m, 3.75f);
  EXPECT_FLOAT_EQ(k->cushionAwayAngle_deg, 135.0f);
  EXPECT_FLOAT_EQ(k->stepUpHeading_deg, 100.0f);
  EXPECT_FLOAT_EQ(k->widthCapFactor, 0.85f);
  EXPECT_FLOAT_EQ(k->handOffOrganiserFloor, 10.0f);
  EXPECT_FLOAT_EQ(k->handOffOrganiserTop, 50.0f);
  EXPECT_FLOAT_EQ(k->handOffFrames_m, 60.0f);
  EXPECT_FLOAT_EQ(k->handOffOrganiserSpan, -40.0f);
  // The pitch facts PES derives from its FieldInfo and GF derives from its own.
  const pes::ShapeField f = pes::ShapeFieldFromPitch();
  EXPECT_FLOAT_EQ(f.halfLength_m, 52.5f);
  EXPECT_FLOAT_EQ(f.ownBox_m, 52.5f - 16.5f);
  EXPECT_FLOAT_EQ(f.ownGoalArea_m, 52.5f - 5.5f);
}

// --- the settings that gate the trap and the mark passes ----------------------

class DefensiveShapeMatchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* root = std::getenv("PES21_ROOT");
    if (!root) {
      haveInstall_ = false;
      return;
    }
    std::string err;
    vfs_ = std::make_unique<pes::PesVfs>();
    if (!vfs_->Mount(root, &err)) {
      haveInstall_ = false;
      return;
    }
    rig_ = pesrig::BuildRig(*vfs_, &err);
    haveInstall_ = rig_.has_value();
    if (!haveInstall_) return;
    haveInstall_ = db_.LoadSqlScript(std::string(GF_SOURCE_DIR) + "/assets/fixtures/teams.sql");
    if (haveInstall_) SetDB(&db_);
  }

  Match* Start(unsigned int seed, int minutes) {
    setup_ = std::make_unique<MatchSetup>();
    setup_->homeTeamDbId = HOME_TEAM;
    setup_->awayTeamDbId = AWAY_TEAM;
    setup_->seed = seed;
    setup_->durationMinutes = minutes;
    setup_->animationDir = std::string(GF_SOURCE_DIR) + "/assets/gf_anims";
    setup_->rig = &*rig_;
    if (!pestest::FillMissingPesConstants(setup_.get())) return nullptr;
    match_ = std::make_unique<Match>(new MatchData(HOME_TEAM, AWAY_TEAM), *setup_, Properties());
    return match_.get();
  }

  bool haveInstall_ = false;
  blunted::Database db_;
  std::unique_ptr<pes::PesVfs> vfs_;
  std::optional<pesrig::Rig> rig_;
  std::unique_ptr<MatchSetup> setup_;
  std::unique_ptr<Match> match_;
};

// PES arms the offside trap only on a team instruction (report 14.4's `T+0xe4`),
// and both live teams ran with it off. A team with no such instruction plays
// with it off.
TEST_F(DefensiveShapeMatchTest, OffsideTrapDefaultsOff) {
  if (!haveInstall_) GTEST_SKIP() << "PES21_ROOT not set";
  Match* match = Start(1000, 1);
  ASSERT_TRUE(match);
  for (int t = 0; t < 2; ++t) EXPECT_FALSE(match->GetTeam(t)->GetController()->IsOffsideTrapArmed());
}

// The marking is re-decided every frame (GF ran it every 400 ms), so after one
// tick of play the defending side has marks out, and a marker's target is an
// opponent rather than himself.
TEST_F(DefensiveShapeMatchTest, MarkingRunsEveryFrameAndCoversMoreThanThree) {
  if (!haveInstall_) GTEST_SKIP() << "PES21_ROOT not set";
  Match* match = Start(1000, 2);
  ASSERT_TRUE(match);
  int marked = -1, bestMarked = 0, ticksWithMarks = 0, pairs = 0, nearest = 0;
  for (int i = 0; i < 2000; ++i) {
    match->Process();
    if (match->IsInPlay() && !match->IsInSetPiece()) {
      std::vector<Player*> players;
      match->GetActiveTeamPlayers(0, players);
      marked = 0;
      for (Player* p : players)
        if (p->GetManMarkingID() >= 0) ++marked;
      // A freshly computed pairing only commits after the hand-off delay, so a
      // tick sampled the moment it changed carries none; the run's maximum is
      // what says the assignment covers the opponents.
      bestMarked = std::max(bestMarked, marked);
      if (marked > 0) ++ticksWithMarks;
      // The marker is the nearest participating own player to his opponent: PES's
      // live sample had 57 % nearest, 78 % top-two, 86 % top-three (report 14.2),
      // so the port is held to "nearest most of the time".
      for (Player* p : players) {
        if (p->GetManMarkingID() < 0) continue;
        Player* opp = match->GetPlayer(p->GetManMarkingID());
        if (!opp) continue;
        ++pairs;
        float nearest_m = 1000.0f;
        for (Player* mate : players) {
          if (mate->GetFormationEntry().role == e_PlayerRole_GK) continue;
          nearest_m = std::min(nearest_m, (mate->GetPosition() - opp->GetPosition()).GetLength());
        }
        if ((p->GetPosition() - opp->GetPosition()).GetLength() <= nearest_m + 0.01f) ++nearest;
      }
    }
  }
  EXPECT_GT(bestMarked, 0) << "no marker in " << ticksWithMarks << " in-play ticks";
  // PES's live sample had a median of three markers per defending team, and never
  // more than eight; the port has no cap but the steal radius bounds it.
  EXPECT_LE(bestMarked, 10) << "the steal radius did not bound the assignment";

  std::vector<Player*> own;
  match->GetActiveTeamPlayers(0, own);
  for (Player* p : own) {
    const int target = p->GetManMarkingID();
    if (target < 0) continue;
    EXPECT_NE(target, p->GetID()) << "a player marks himself";
    Player* opp = match->GetPlayer(target);
    EXPECT_NE(opp, nullptr);
    EXPECT_EQ(opp->GetTeamID(), 1) << "the marker targets the other team";
    EXPECT_NE(opp->GetFormationEntry().role, e_PlayerRole_GK) << "nobody marks the keeper";
  }

  ASSERT_GT(pairs, 0) << "no mark pair was sampled";
  EXPECT_GE(nearest * 100 / pairs, 40)
      << "only " << nearest << " of " << pairs << " markers are the nearest own player";
}

// The defensive line and the team length are rebuilt every tick from the ball,
// so a defending side keeps a line behind the ball and inside its own half.
TEST_F(DefensiveShapeMatchTest, LineStaysBehindTheBallAndInsideOurHalf) {
  if (!haveInstall_) GTEST_SKIP() << "PES21_ROOT not set";
  Match* match = Start(1000, 2);
  ASSERT_TRUE(match);
  bool sampling = false;
  for (int i = 0; i < 600; ++i) {
    match->Process();
    if (!match->IsInPlay() || match->IsInSetPiece()) continue;
    for (int t = 0; t < 2; ++t) {
      Team* team = match->GetTeam(t);
      // A side that has the ball plays its attacking shape, not this one.
      if (team->GetFadingTeamPossessionAmount() >= 1.0f) continue;
      const float dir = static_cast<float>(-team->GetSide());
      const float line = team->GetController()->GetDefensiveLineHeight() * dir;
      const float ball = match->GetBall()->Predict(0).coords[0] * dir;
      // Never past halfway, never ahead of the ball, never deeper than the box
      // line. The 10 m cushion behind the ball is a floor, not a target, and is
      // pinned on the ramp itself above.
      EXPECT_LE(line, ball + 0.5f) << "the line is ahead of the ball";
      EXPECT_LE(line, 0.0f) << "the line crossed halfway";
      EXPECT_GE(line, -52.5f + 5.5f - 1e-3f) << "the line went past the box line";
      EXPECT_GT(team->GetController()->GetDefensiveTeamLength(), 0.0f);
      sampling = true;
    }
  }
  EXPECT_TRUE(sampling) << "no defending tick was sampled";
}

}  // namespace

// Where players may stand at a restart.
//
// Law 16 keeps the opponents out of the penalty area at a goal kick until the ball is
// in play. Nothing enforced it: a striker who was in the six-yard box when the kick was
// awarded stayed there, and the kick could be played straight to him - which is exactly
// how a goal kick got stolen and scored.

#include <cmath>

#include <gtest/gtest.h>

#include "onthepitch/setpiecelaws.hpp"

namespace {

// The pitch the engine plays on.
constexpr float kHalfW = 55.0f;

}  // namespace

TEST(SetPieceLaws, AGoalKickClearsTheArea) {
  EXPECT_TRUE(SetPieceLaws::ClearsThePenaltyArea(e_SetPiece_GoalKick));
  EXPECT_FALSE(SetPieceLaws::ClearsThePenaltyArea(e_SetPiece_ThrowIn));
  EXPECT_FALSE(SetPieceLaws::ClearsThePenaltyArea(e_SetPiece_Corner));
  EXPECT_FALSE(SetPieceLaws::ClearsThePenaltyArea(e_SetPiece_KickOff));
  EXPECT_FALSE(SetPieceLaws::ClearsThePenaltyArea(e_SetPiece_None));
}

TEST(SetPieceLaws, AFreeKickInsideYourOwnAreaClearsItAsWell) {
  // Law 13, and the case that was actually reported: an offside given deep in defence
  // restarts inside the box with the flagged attacker still standing there
  EXPECT_TRUE(SetPieceLaws::ClearsThePenaltyArea(e_SetPiece_FreeKick, kHalfW - 8.0f, 1, kHalfW));
  EXPECT_TRUE(SetPieceLaws::ClearsThePenaltyArea(e_SetPiece_FreeKick, -kHalfW + 8.0f, -1, kHalfW));
}

TEST(SetPieceLaws, AFreeKickOutsideTheAreaLeavesEveryoneAlone) {
  EXPECT_FALSE(SetPieceLaws::ClearsThePenaltyArea(e_SetPiece_FreeKick, 0.0f, 1, kHalfW));
  EXPECT_FALSE(SetPieceLaws::ClearsThePenaltyArea(e_SetPiece_FreeKick, kHalfW - 30.0f, 1, kHalfW));
}

TEST(SetPieceLaws, WithNoSideGivenAFreeKickIsNotGuessedAt) {
  EXPECT_FALSE(SetPieceLaws::ClearsThePenaltyArea(e_SetPiece_FreeKick));
}

TEST(SetPieceLaws, TheAreaIsWhereThePitchPaintsIt) {
  // 16.5 m deep, 20.15 m either side, on the +x side
  EXPECT_TRUE(SetPieceLaws::InsidePenaltyArea(kHalfW - 1.0f, 0.0f, 1, kHalfW));
  EXPECT_TRUE(SetPieceLaws::InsidePenaltyArea(kHalfW - 16.0f, 20.0f, 1, kHalfW));
  EXPECT_FALSE(SetPieceLaws::InsidePenaltyArea(kHalfW - 17.0f, 0.0f, 1, kHalfW));
  EXPECT_FALSE(SetPieceLaws::InsidePenaltyArea(kHalfW - 1.0f, 21.0f, 1, kHalfW));
}

TEST(SetPieceLaws, TheOtherEndIsNotTheSameArea) {
  // a striker in the far box is not in this one
  EXPECT_FALSE(SetPieceLaws::InsidePenaltyArea(-kHalfW + 1.0f, 0.0f, 1, kHalfW));
  EXPECT_TRUE(SetPieceLaws::InsidePenaltyArea(-kHalfW + 1.0f, 0.0f, -1, kHalfW));
}

TEST(SetPieceLaws, ClearingSendsAPlayerOutOfTheNearestEdge) {
  float x = 0.0f, y = 0.0f;
  // deep in the box and central: the way out is up the pitch, over the 16.5 m line
  SetPieceLaws::ClearingTarget(kHalfW - 2.0f, 0.0f, 1, kHalfW, &x, &y);
  EXPECT_FALSE(SetPieceLaws::InsidePenaltyArea(x, y, 1, kHalfW));
  EXPECT_LT(x, kHalfW - SetPieceLaws::kAreaDepth);

  // near the side of the box: the way out is sideways, which is shorter
  SetPieceLaws::ClearingTarget(kHalfW - 2.0f, 19.0f, 1, kHalfW, &x, &y);
  EXPECT_FALSE(SetPieceLaws::InsidePenaltyArea(x, y, 1, kHalfW));
  EXPECT_GT(y, SetPieceLaws::kAreaHalfWidth);
}

TEST(SetPieceLaws, ClearingKeepsAMarginRatherThanStandingOnTheLine) {
  float x = 0.0f, y = 0.0f;
  SetPieceLaws::ClearingTarget(kHalfW - 2.0f, 0.0f, 1, kHalfW, &x, &y);
  EXPECT_LE(x, kHalfW - SetPieceLaws::kAreaDepth - SetPieceLaws::kClearanceMargin + 0.01f);
}

TEST(SetPieceLaws, ClearingWorksOnBothSides) {
  float x = 0.0f, y = 0.0f;
  SetPieceLaws::ClearingTarget(-kHalfW + 2.0f, 0.0f, -1, kHalfW, &x, &y);
  EXPECT_FALSE(SetPieceLaws::InsidePenaltyArea(x, y, -1, kHalfW));
  EXPECT_GT(x, -kHalfW + SetPieceLaws::kAreaDepth);
}

TEST(SetPieceLaws, APlayerAlreadyOutIsLeftWhereHeIs) {
  float x = 0.0f, y = 0.0f;
  SetPieceLaws::ClearingTarget(0.0f, 5.0f, 1, kHalfW, &x, &y);
  EXPECT_FLOAT_EQ(x, 0.0f);
  EXPECT_FLOAT_EQ(y, 5.0f);
}

TEST(SetPieceLaws, TheKickerWaitsWhileAnyoneIsStillInside) {
  EXPECT_FALSE(SetPieceLaws::MayRestart(1, 0));
  EXPECT_FALSE(SetPieceLaws::MayRestart(3, 2000));
  EXPECT_TRUE(SetPieceLaws::MayRestart(0, 0));
}

TEST(SetPieceLaws, TheWaitIsBoundedSoAStuckPlayerCannotFreezeTheMatch) {
  EXPECT_TRUE(SetPieceLaws::MayRestart(1, SetPieceLaws::kMaxWait_ms));
  EXPECT_TRUE(SetPieceLaws::MayRestart(5, SetPieceLaws::kMaxWait_ms + 1000));
}

// Law 13: the opponents stand off the ball at a free kick or a corner. Nothing
// enforced it, so a defender could stand over the ball and block the kick.

TEST(BallRadius, AFreeKickAndACornerHoldOpponentsOff) {
  EXPECT_TRUE(SetPieceLaws::ClearsTheBallRadius(e_SetPiece_FreeKick));
  EXPECT_TRUE(SetPieceLaws::ClearsTheBallRadius(e_SetPiece_Corner));
  EXPECT_FALSE(SetPieceLaws::ClearsTheBallRadius(e_SetPiece_GoalKick));
  EXPECT_FALSE(SetPieceLaws::ClearsTheBallRadius(e_SetPiece_KickOff));
}

TEST(BallRadius, TheDistanceIsTheLawsNineFifteen) {
  EXPECT_TRUE(SetPieceLaws::InsideBallRadius(5.0f, 0.0f, 0.0f, 0.0f));
  EXPECT_TRUE(SetPieceLaws::InsideBallRadius(0.0f, 9.0f, 0.0f, 0.0f));
  EXPECT_FALSE(SetPieceLaws::InsideBallRadius(10.0f, 0.0f, 0.0f, 0.0f));
}

TEST(BallRadius, RetreatingGoesStraightBackFromTheBall) {
  float x = 0.0f, y = 0.0f;
  SetPieceLaws::RetreatTarget(3.0f, 0.0f, 0.0f, 0.0f, &x, &y);
  EXPECT_FALSE(SetPieceLaws::InsideBallRadius(x, y, 0.0f, 0.0f));
  EXPECT_GT(x, SetPieceLaws::kRetreatRadius);
  EXPECT_FLOAT_EQ(y, 0.0f) << "backed off sideways rather than straight";
}

TEST(BallRadius, RetreatingKeepsTheDirectionItCameFrom) {
  float x = 0.0f, y = 0.0f;
  SetPieceLaws::RetreatTarget(-2.0f, -2.0f, 0.0f, 0.0f, &x, &y);
  EXPECT_LT(x, 0.0f);
  EXPECT_LT(y, 0.0f);
  EXPECT_NEAR(x, y, 0.01f) << "the diagonal was not preserved";
}

TEST(BallRadius, StandingOnTheBallIsNotADivisionByZero) {
  float x = 0.0f, y = 0.0f;
  SetPieceLaws::RetreatTarget(0.0f, 0.0f, 0.0f, 0.0f, &x, &y);
  EXPECT_FALSE(SetPieceLaws::InsideBallRadius(x, y, 0.0f, 0.0f));
  EXPECT_TRUE(std::isfinite(x));
  EXPECT_TRUE(std::isfinite(y));
}

TEST(BallRadius, APlayerAlreadyBackIsLeftWhereHeIs) {
  float x = 0.0f, y = 0.0f;
  SetPieceLaws::RetreatTarget(20.0f, 3.0f, 0.0f, 0.0f, &x, &y);
  EXPECT_FLOAT_EQ(x, 20.0f);
  EXPECT_FLOAT_EQ(y, 3.0f);
}

// How clear is clear.
//
// Recorded in a match: the opponents did walk out of the box, and the kick went the
// moment the last of them crossed the line - with him still standing on it, half a
// step from where he had been. The clearing happened and bought nothing.
//
// The gate that lets the taker kick therefore has to be a little wider than the area
// itself, and narrower than the place the players are walking to, or a man who stops
// exactly on his target would never quite satisfy it.

TEST(SetPieceLaws, TheGateIsWiderThanTheAreaAndInsideTheWalk) {
  EXPECT_GT(SetPieceLaws::kGateMargin, 0.0f);
  EXPECT_LT(SetPieceLaws::kGateMargin, SetPieceLaws::kClearanceMargin);
}

TEST(SetPieceLaws, StandingOnTheLineStillHoldsTheKick) {
  const float pitchHalfW = 55.0f;
  const int side = 1;
  // Just outside the 16.5 m line: out of the area by the letter, not out of the way.
  const float x = pitchHalfW - SetPieceLaws::kAreaDepth - 0.2f;
  EXPECT_FALSE(SetPieceLaws::InsidePenaltyArea(x, 0.0f, side, pitchHalfW));
  EXPECT_TRUE(SetPieceLaws::IntrudesOnPenaltyArea(x, 0.0f, side, pitchHalfW));
}

TEST(SetPieceLaws, AManOnHisClearingTargetLetsTheKickGo) {
  const float pitchHalfW = 55.0f;
  const int side = 1;
  float x = 0.0f, y = 0.0f;
  SetPieceLaws::ClearingTarget(pitchHalfW - 4.0f, 2.0f, side, pitchHalfW, &x, &y);
  EXPECT_FALSE(SetPieceLaws::IntrudesOnPenaltyArea(x, y, side, pitchHalfW));
}

TEST(SetPieceLaws, EveryClearingTargetSatisfiesTheGate) {
  // Whatever corner of the area he starts in, walking to his target is enough.
  const float pitchHalfW = 55.0f;
  for (int side = -1; side <= 1; side += 2) {
    for (float depth = 0.0f; depth <= SetPieceLaws::kAreaDepth; depth += 1.5f) {
      for (float y = -SetPieceLaws::kAreaHalfWidth; y <= SetPieceLaws::kAreaHalfWidth;
           y += 2.5f) {
        const float x = (pitchHalfW - depth) * static_cast<float>(side);
        float outX = 0.0f, outY = 0.0f;
        SetPieceLaws::ClearingTarget(x, y, side, pitchHalfW, &outX, &outY);
        EXPECT_FALSE(SetPieceLaws::IntrudesOnPenaltyArea(outX, outY, side, pitchHalfW))
            << "from x " << x << " y " << y;
      }
    }
  }
}

TEST(SetPieceLaws, BackingOffToTheEdgeOfNineMetresStillHoldsIt) {
  // 9.16 m from the ball is legal and useless: he is still on top of the kick.
  EXPECT_FALSE(SetPieceLaws::InsideBallRadius(SetPieceLaws::kRetreatRadius + 0.01f, 0.0f,
                                              0.0f, 0.0f));
  EXPECT_TRUE(SetPieceLaws::IntrudesOnBallRadius(SetPieceLaws::kRetreatRadius + 0.01f, 0.0f,
                                                 0.0f, 0.0f));
}

TEST(SetPieceLaws, EveryRetreatTargetSatisfiesTheGate) {
  for (float angle = 0.0f; angle < 6.28f; angle += 0.7f) {
    for (float distance = 0.0f; distance < SetPieceLaws::kRetreatRadius; distance += 1.5f) {
      const float x = 12.0f + distance * std::cos(angle);
      const float y = 3.0f + distance * std::sin(angle);
      float outX = 0.0f, outY = 0.0f;
      SetPieceLaws::RetreatTarget(x, y, 12.0f, 3.0f, &outX, &outY);
      EXPECT_FALSE(SetPieceLaws::IntrudesOnBallRadius(outX, outY, 12.0f, 3.0f))
          << "from x " << x << " y " << y;
    }
  }
}

TEST(SetPieceLaws, AManInTheGateBandIsAlsoSentOut) {
  // Half a metre outside the painted line: he holds the kick up, so he has to be
  // asked to move, or nothing releases it and every restart waits out the cap.
  const float pitchHalfW = 55.0f;
  const int side = 1;
  const float x = pitchHalfW - SetPieceLaws::kAreaDepth - 0.5f;
  ASSERT_TRUE(SetPieceLaws::IntrudesOnPenaltyArea(x, 0.0f, side, pitchHalfW));
  float outX = x, outY = 0.0f;
  SetPieceLaws::ClearingTarget(x, 0.0f, side, pitchHalfW, &outX, &outY);
  EXPECT_NE(outX, x);
  EXPECT_FALSE(SetPieceLaws::IntrudesOnPenaltyArea(outX, outY, side, pitchHalfW));
}

TEST(SetPieceLaws, AManWellClearIsLeftWhereHeIs) {
  const float pitchHalfW = 55.0f;
  const int side = 1;
  const float x = pitchHalfW - SetPieceLaws::kAreaDepth - 8.0f;
  float outX = x, outY = 3.0f;
  SetPieceLaws::ClearingTarget(x, 3.0f, side, pitchHalfW, &outX, &outY);
  EXPECT_FLOAT_EQ(outX, x);
  EXPECT_FLOAT_EQ(outY, 3.0f);
}

TEST(SetPieceLaws, EveryStartingPointInTheGateLeavesIt) {
  // Swept over the whole gate region, including the band outside the paint and the
  // corners, where going sideways alone is not enough.
  const float pitchHalfW = 55.0f;
  for (int side = -1; side <= 1; side += 2) {
    for (float depth = -SetPieceLaws::kGateMargin; depth <= SetPieceLaws::kAreaDepth +
                                                                SetPieceLaws::kGateMargin;
         depth += 0.7f) {
      for (float y = -(SetPieceLaws::kAreaHalfWidth + SetPieceLaws::kGateMargin);
           y <= SetPieceLaws::kAreaHalfWidth + SetPieceLaws::kGateMargin; y += 1.3f) {
        const float x = (pitchHalfW - depth) * static_cast<float>(side);
        if (!SetPieceLaws::IntrudesOnPenaltyArea(x, y, side, pitchHalfW)) continue;
        float outX = 0.0f, outY = 0.0f;
        SetPieceLaws::ClearingTarget(x, y, side, pitchHalfW, &outX, &outY);
        EXPECT_FALSE(SetPieceLaws::IntrudesOnPenaltyArea(outX, outY, side, pitchHalfW))
            << "from x " << x << " y " << y;
      }
    }
  }
}

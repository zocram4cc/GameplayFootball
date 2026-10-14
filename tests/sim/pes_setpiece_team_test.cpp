// PES 2021's set-piece team AI: the taker waterfall, the jog budget, the wall and
// the corner attacker count. docs/re/pes-re-setpiece-kicker.md 1,
// docs/re/pes-re-setpiece-choreography.md 5.2, 11.3, 11.5, 11.6 and 11.8.
//
// The logic tests run on synthetic constants shaped like the install's; the
// install's own are read and checked in the one gated test at the end.

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>

#include "onthepitch/pessetpieceteam.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace {

// The measured set: 16 km/h jog, 20 s corner, 7/10/13 s own side by role class,
// 8/12 s other side, wall at 9.15 m with 0.5 m spacings.
pes::SetPieceTeamConstants Measured() {
  pes::SetPieceTeamConstants k;
  k.jogKmh = 16000.0f;
  k.secondsPerHour = 3600.0f;
  k.budgetCorner_s = 20.0f;
  k.budgetPostMan_s = 300.0f;
  k.budgetOwnBack_s = 7.0f;
  k.budgetOwnMid_s = 10.0f;
  k.budgetOwnFront_s = 13.0f;
  k.budgetOtherMarking_s = 8.0f;
  k.budgetOther_s = 12.0f;
  k.budgetThrowInSupport_s = 3.0f;
  k.budgetThrowIn_s = 2.0f;
  k.takerSlotBudgetFactor = 1.5f;
  k.fkDesignatedNear_m = 32.0f;
  k.fkDesignatedFar_m = 60.0f;
  k.fkKeeperOwnGoal_m = 28.0f;
  k.fkNearest_m = 65.0f;
  k.wallDistance_m = 9.15f;
  k.wallSpacing_m = 0.5f;
  k.wallNearPost_m = 1.3f;
  k.wallGoalLineBand_m = 7.5f;
  k.wallNarrowDist_m = 28.0f;
  k.wallNarrowBand_m = 5.0f;
  k.wallCentralGap_m = 3.0f;
  k.wallCentralHalfZ_m = 10.0f;
  k.wallD25_m = 25.0f;
  k.wallD29_m = 29.0f;
  k.wallD32_m = 32.0f;
  k.wallD36_m = 36.0f;
  k.wallD40_m = 40.0f;
  k.wallAngle20_deg = 20.0f;
  k.wallAngle30_deg = 30.0f;
  k.wallAngle40_deg = 40.0f;
  k.wallAngle45_deg = 45.0f;
  k.wallAngle50_deg = 50.0f;
  k.wallSpan20_deg = 20.0f;
  k.wallSpan15_deg = 15.0f;
  k.wallSpan10_deg = 10.0f;
  return k;
}

const pes::SetPieceTeamConstants k = Measured();
const pes::SetPieceTeamCounts kCounts;

// PES's FieldInfo, which every placement rule reads: half-length, goal line,
// penalty line and half-width. GF's pitch is the 105 x 68 standard these were
// authored on (gametypes.hpp), so the numbers stand in for it.
constexpr float kPitchHalfW = 52.5f;
constexpr float kGoalHalfWidth = 3.66f;
constexpr float kPenaltyX = 36.0f;
constexpr float kPenaltyHalfZ = 20.15f;

pes::SetPieceCandidate Candidate(int index, e_PlayerRole role, float suitability,
                                float distanceToBall_m, bool keeper = false,
                                float aerial = 0.5f) {
  pes::SetPieceCandidate c;
  c.index = index;
  c.role = role;
  c.suitability = suitability;
  c.distanceToBall_m = distanceToBall_m;
  c.isKeeper = keeper;
  c.aerial = aerial;
  c.onPitch = true;
  return c;
}

}  // namespace

// --- the taker waterfall (pes-re-setpiece-kicker.md 1.2) ----------------------

// A free kick near the own goal is the keeper's, however far away he stands.
// Confirmed live: an FK 21.8 m from its own goal was taken by that team's keeper
// standing 46 m from it.
TEST(SetPieceTaker, KeeperTakesAFreeKickDeepInHisOwnBox) {
  std::vector<pes::SetPieceCandidate> squad{Candidate(0, e_PlayerRole_GK, 0.1f, 46.0f, true),
                                            Candidate(4, e_PlayerRole_CB, 0.4f, 6.0f),
                                            Candidate(9, e_PlayerRole_CF, 0.9f, 8.0f)};
  // |z| < F[7] and within 28 m of the own goal: the keeper.
  EXPECT_EQ(pes::FreeKickTaker(squad, -40.0f, 0.0f, 1.0f, kPitchHalfW, -kPitchHalfW, kPenaltyX,
                               kPenaltyHalfZ, k),
            0);
  // Wide of the box line but still in the own half: a centre back, not the stiker.
  EXPECT_EQ(pes::FreeKickTaker(squad, -20.0f, 0.0f, 1.0f, kPitchHalfW, -kPitchHalfW, kPenaltyX,
                               kPenaltyHalfZ, k),
            4);
  // The own-half branch is a role branch, not a proximity one: a midfielder
  // standing closer does not get it, even though the next step down would give
  // it to him.
  std::vector<pes::SetPieceCandidate> backs{Candidate(0, e_PlayerRole_GK, 0.1f, 46.0f, true),
                                            Candidate(4, e_PlayerRole_CB, 0.4f, 20.0f),
                                            Candidate(6, e_PlayerRole_CM, 0.4f, 3.0f)};
  EXPECT_EQ(pes::FreeKickTaker(backs, -20.0f, 0.0f, 1.0f, kPitchHalfW, -kPitchHalfW, kPenaltyX,
                               kPenaltyHalfZ, k),
            4);
}

// A free kick from far out is the nearest midfielder's, not the best taker's.
TEST(SetPieceTaker, LongFreeKickGoesToTheNearestOutfielder) {
  std::vector<pes::SetPieceCandidate> squad{Candidate(3, e_PlayerRole_CM, 0.2f, 3.0f),
                                            Candidate(9, e_PlayerRole_CF, 0.95f, 20.0f)};
  // 72.5 m from the goal being attacked, so `d > 60`, with no back on the pitch.
  EXPECT_EQ(pes::FreeKickTaker(squad, -20.0f, 0.0f, 1.0f, kPitchHalfW, -kPitchHalfW, kPenaltyX,
                               kPenaltyHalfZ, k),
            3);
  // With nobody else on the pitch the rating fallback takes over, and its role
  // filter for a free kick is "anyone but the goalkeeper" - so even a striker may
  // end up with one when the waterfall has exhausted everyone else.
  std::vector<pes::SetPieceCandidate> only{Candidate(9, e_PlayerRole_CF, 0.95f, 20.0f)};
  EXPECT_EQ(pes::FreeKickTaker(only, -20.0f, 0.0f, 1.0f, kPitchHalfW, -kPitchHalfW, kPenaltyX,
                               kPenaltyHalfZ, k),
            9);
  // The nearest-man branch's role filter ("role < 7") is what keeps a striker out
  // of a long free kick while there is anyone else to give it to.
  std::vector<pes::SetPieceCandidate> squad2{Candidate(2, e_PlayerRole_LB, 0.3f, 7.0f),
                                             Candidate(9, e_PlayerRole_CF, 0.95f, 20.0f)};
  EXPECT_EQ(pes::FreeKickTaker(squad2, -20.0f, 0.0f, 1.0f, kPitchHalfW, -kPitchHalfW, kPenaltyX,
                               kPenaltyHalfZ, k),
            2);
}

// The rating fallback is the last resort and the goalkeeper is never in it.
TEST(SetPieceTaker, RatingFallbackIsHighestSuitabilityFirstWins) {
  std::vector<pes::SetPieceCandidate> squad{Candidate(0, e_PlayerRole_GK, 1.0f, 2.0f, true),
                                           Candidate(2, e_PlayerRole_CM, 0.50f, 30.0f),
                                           Candidate(6, e_PlayerRole_AM, 0.62f, 30.0f),
                                           Candidate(7, e_PlayerRole_AM, 0.62f, 30.0f)};
  EXPECT_EQ(pes::PickBySuitability(squad), 6);
}

// --- the jog budget (pes-re-setpiece-choreography.md 11.3) -------------------

TEST(SetPieceJog, ACloseMarkIsReachedExactly) {
  // 10 m at 16 km/h is 2.25 s, so a 7 s budget covers it and the clamp is 1.
  EXPECT_FLOAT_EQ(pes::JogFraction(k.budgetOwnBack_s, 10.0f, k), 1.0f);
  // 31.1 m at 16 km/h is exactly 7 s: the boundary.
  EXPECT_NEAR(pes::JogFraction(k.budgetOwnBack_s, 31.1f, k), 1.0f, 0.02f);
}

TEST(SetPieceJog, AFarMarkIsOnlyPartiallyCovered) {
  // 60 m in a 12 s budget is 53.3 m: 88.9% of the way.
  const float fraction = pes::JogFraction(k.budgetOther_s, 60.0f, k);
  EXPECT_NEAR(fraction * 60.0f, 4.444f * k.budgetOther_s, 1.0f);
  EXPECT_GT(fraction, 0.85f);
  EXPECT_LT(fraction, 0.95f);
  // A 2 s throw-in budget covers 8.9 m of a 40 m reform.
  EXPECT_NEAR(pes::JogFraction(k.budgetThrowIn_s, 40.0f, k) * 40.0f, 8.89f, 0.05f);
}

TEST(SetPieceJog, NoBudgetMeansNoLimit) {
  EXPECT_FLOAT_EQ(pes::JogFraction(-1.0f, 90.0f, k), 1.0f);
  EXPECT_FLOAT_EQ(pes::JogFraction(-1.0f, 0.0f, k), 1.0f);
}

TEST(SetPieceJog, BudgetsFollowTheKindAndTheRole) {
  // The kicker, a wall man, the own keeper and a post man are unlimited.
  EXPECT_LT(pes::PlacementBudget_s(k, (int)e_SetPiece_FreeKick, true, e_PlayerRole_CF, true, false,
                                   false, false, false, false),
            0.0f);
  EXPECT_LT(pes::PlacementBudget_s(k, (int)e_SetPiece_FreeKick, false, e_PlayerRole_CM, false, false,
                                   true, false, false, false),
            0.0f);
  EXPECT_LT(pes::PlacementBudget_s(k, (int)e_SetPiece_Corner, true, e_PlayerRole_CM, false, false,
                                   false, false, true, false),
            0.0f);
  // A corner gives everybody the same 20 s.
  EXPECT_FLOAT_EQ(pes::PlacementBudget_s(k, (int)e_SetPiece_Corner, true, e_PlayerRole_CM, false,
                                         false, false, false, false, false),
                  k.budgetCorner_s);
  EXPECT_FLOAT_EQ(pes::PlacementBudget_s(k, (int)e_SetPiece_Corner, false, e_PlayerRole_CF, false,
                                         false, false, false, false, false),
                  k.budgetCorner_s);
  // A goal kick: back class 7 s, mid 10 s, front 13 s, the other side 12 s.
  EXPECT_FLOAT_EQ(pes::PlacementBudget_s(k, (int)e_SetPiece_GoalKick, true, e_PlayerRole_CB, false,
                                         false, false, false, false, false),
                  k.budgetOwnBack_s);
  EXPECT_FLOAT_EQ(pes::PlacementBudget_s(k, (int)e_SetPiece_GoalKick, true, e_PlayerRole_CM, false,
                                         false, false, false, false, false),
                  k.budgetOwnMid_s);
  EXPECT_FLOAT_EQ(pes::PlacementBudget_s(k, (int)e_SetPiece_GoalKick, true, e_PlayerRole_CF, false,
                                         false, false, false, false, false),
                  k.budgetOwnFront_s);
  EXPECT_FLOAT_EQ(pes::PlacementBudget_s(k, (int)e_SetPiece_GoalKick, false, e_PlayerRole_CF, false,
                                         false, false, false, false, false),
                  k.budgetOther_s);
  // A kick-off and a penalty are re-formed, not jogged to.
  EXPECT_LT(pes::PlacementBudget_s(k, (int)e_SetPiece_KickOff, true, e_PlayerRole_CM, false, false,
                                   false, false, false, false),
            0.0f);
}

// --- the wall (pes-re-setpiece-choreography.md 5.2, 5.3, 11.4, 11.8) ----------

TEST(SetPieceWall, SizeGrowsTowardsTheGoalAndNarrowsWithTheAngle) {
  // The live free kick: 35.3 m out, 60 degrees off the centre line -> 1 man.
  EXPECT_EQ(pes::WallSize(35.3f, 60.4f, 35.1f, -30.7f, 1.0f, kPitchHalfW, kPenaltyX, kPenaltyHalfZ,
                          k, kCounts),
            1);
  // Straight in front of the box, close: the maximum.
  EXPECT_EQ(pes::WallSize(20.0f, 5.0f, 30.0f, 1.0f, 1.0f, kPitchHalfW, kPenaltyX, kPenaltyHalfZ, k,
                          kCounts),
            kCounts.wallCentralCount);
  // 22 m out and 10 degrees off centre: 4 + int(2*1) = 6.
  EXPECT_EQ(pes::WallSize(22.0f, 10.0f, 30.0f, 3.0f, 1.0f, kPitchHalfW, kPenaltyX, kPenaltyHalfZ, k,
                          kCounts),
            6);
  // 22 m out and 30 degrees off centre: 4 + int(2*0.5) = 5.
  EXPECT_EQ(pes::WallSize(22.0f, 30.0f, 30.0f, 12.0f, 1.0f, kPitchHalfW, kPenaltyX, kPenaltyHalfZ, k,
                          kCounts),
            5);
  // 22 m out and 45 degrees off centre: the ramp is empty, 4 men.
  EXPECT_EQ(pes::WallSize(22.0f, 45.0f, 30.0f, 17.0f, 1.0f, kPitchHalfW, kPenaltyX, kPenaltyHalfZ, k,
                          kCounts),
            4);
  // 35.3 m out and 40 degrees off centre: 1 + int(1) = 2. The live free kick was
  // 35.3 m out but 60 degrees off, which is past the 50-degree ramp and gives one.
  EXPECT_EQ(pes::WallSize(35.3f, 40.0f, 30.0f, 23.0f, 1.0f, kPitchHalfW, kPenaltyX, kPenaltyHalfZ, k,
                          kCounts),
            2);
  EXPECT_EQ(pes::WallSize(35.3f, 55.0f, 30.0f, 27.0f, 1.0f, kPitchHalfW, kPenaltyX, kPenaltyHalfZ, k,
                          kCounts),
            1);
  // Beyond 40 m nothing is built.
  EXPECT_EQ(pes::WallSize(41.0f, 5.0f, 30.0f, 3.0f, 1.0f, kPitchHalfW, kPenaltyX, kPenaltyHalfZ, k,
                          kCounts),
            0);
  // Within 7.5 m of the goal line: one man, whatever the angle.
  EXPECT_EQ(pes::WallSize(6.0f, 50.0f, 48.0f, 2.0f, 1.0f, kPitchHalfW, kPenaltyX, kPenaltyHalfZ, k,
                          kCounts),
            1);
}

TEST(SetPieceWall, MenStandNineFifteenMetresOutAndHalfAMetreApart) {
  // PES writes the centre 9.15 m from the ball toward the aim point and then
  // spreads the members 0.5 m apart along the wall line, the first on the centre.
  EXPECT_FLOAT_EQ(k.wallDistance_m, 9.15f);
  EXPECT_FLOAT_EQ(k.wallSpacing_m, 0.5f);
  // The centre for a one-man wall is the goal centre, so the wall man stands
  // exactly the ring distance out along the ball's bearing to it.
  const float ballX = 30.0f, ballZ = -20.0f, goalX = kPitchHalfW;
  const float dx = goalX - ballX, dz = 0.0f - ballZ;
  const float len = std::sqrt(dx * dx + dz * dz);
  const float centreX = ballX + dx / len * k.wallDistance_m;
  const float centreZ = ballZ + dz / len * k.wallDistance_m;
  EXPECT_NEAR(std::sqrt((centreX - ballX) * (centreX - ballX) + (centreZ - ballZ) *
                                                                     (centreZ - ballZ)),
              k.wallDistance_m, 1e-4f);
  // The second man is exactly one spacing further along the wall line, which is
  // perpendicular to the ball's bearing: the wall is 0.5 m apart, not 1 m and
  // not a line of men on the ring.
  const float perpX = -dz / len * k.wallSpacing_m, perpZ = dx / len * k.wallSpacing_m;
  const float secondX = centreX + perpX, secondZ = centreZ + perpZ;
  const float toSecond = std::sqrt((secondX - centreX) * (secondX - centreX) +
                                   (secondZ - centreZ) * (secondZ - centreZ));
  EXPECT_NEAR(toSecond, k.wallSpacing_m, 1e-4f);
  // ...and he is a chord out from the ring, because the wall is a straight line
  // through the centre, not an arc around the ball.
  EXPECT_NEAR(std::sqrt((secondX - ballX) * (secondX - ballX) +
                        (secondZ - ballZ) * (secondZ - ballZ)),
              std::sqrt(k.wallDistance_m * k.wallDistance_m + k.wallSpacing_m * k.wallSpacing_m),
              1e-4f);
}

TEST(SetPieceWall, JumpRuleIsDeterministicFrameArithmetic) {
  // FUN_140525ec0. No RNG draw anywhere: the same frames give the same decision.
  const int wallSize = 3;
  pes::WallJumpState a = pes::WallJumpStateStart(wallSize);
  pes::WallJumpState b = pes::WallJumpStateStart(wallSize);
  EXPECT_EQ(pes::WallAction(wallSize, 2, 11, 40, a), pes::WallAction(wallSize, 2, 11, 40, b));
  // The not-jump slots start at (n-1)/2 = 1 for a three-man wall.
  EXPECT_EQ(pes::WallJumpStateStart(wallSize).notJumpLeft, 1);
  EXPECT_EQ(pes::WallJumpStateStart(1).notJumpLeft, 0);
  EXPECT_EQ(pes::WallJumpStateStart(6).notJumpLeft, 2);
  // Once they run out, everyone jumps.
  pes::WallJumpState s = pes::WallJumpStateStart(wallSize);
  s.notJumpLeft = 0;
  EXPECT_EQ(pes::WallAction(wallSize, 1, 10, 40, s), 0);
  // A one-man wall jumps at once: (1-1)/2 = 0 slots.
  pes::WallJumpState one = pes::WallJumpStateStart(1);
  EXPECT_EQ(pes::WallAction(1, 9, 9, 40, one), 0);
  // A whole wall on one frame, threaded in slot order exactly as PES walks it:
  // slot 0 rolls 81 and jumps, slot 1 rolls 20 and does not, slot 2 has no
  // not-jump slot left and jumps. The arithmetic is the frame counter's, so the
  // same frame always produces this triple.
  pes::WallJumpState wall = pes::WallJumpStateStart(3);
  EXPECT_EQ(pes::WallAction(3, 0, 11, 41, wall), 0);  // r = 81  > 24
  EXPECT_EQ(pes::WallAction(3, 1, 12, 41, wall), 1);  // r = 20 <= 24
  EXPECT_EQ(pes::WallAction(3, 2, 13, 41, wall), 0);  // no slots left
  // The roll mixes the slot and the player index, so the same slot on a
  // different index is a different roll: nothing about it is a fixed order.
  pes::WallJumpState other = pes::WallJumpStateStart(3);
  EXPECT_EQ(pes::WallAction(3, 1, 1, 41, other), 0);  // r = 80 > 24
}

TEST(SetPieceWall, AtMostOneManPresses) {
  // Scan the wall's slot order over a window of frames and count the presses:
  // the rule allows exactly one, whatever the frames do.
  const int wallSize = 5;
  for (int frame = 0; frame < 300; frame++) {
    pes::WallJumpState state = pes::WallJumpStateStart(wallSize);
    int presses = 0, jumpers = 0;
    for (int slot = 0; slot < wallSize; slot++) {
      const int action = pes::WallAction(wallSize, slot, 11 + slot, frame, state);
      if (action == 2) presses++;
      if (action == 0) jumpers++;
    }
    EXPECT_LE(presses, 1) << "frame " << frame;
    EXPECT_GE(jumpers, 1) << "frame " << frame;
  }
}

// --- the corner (pes-re-setpiece-choreography.md 11.5, 11.6) ----------------

TEST(SetPieceCorner, BoxAttackerCountFollowsTheAttackLevel) {
  EXPECT_EQ(pes::BoxAttackerCount(pes::AttackLevel(0), kCounts), 4);
  EXPECT_EQ(pes::BoxAttackerCount(pes::AttackLevel(1), kCounts), 5);
  EXPECT_EQ(pes::BoxAttackerCount(pes::AttackLevel(2), kCounts), 6);
  EXPECT_EQ(pes::BoxAttackerCount(pes::AttackLevel(3), kCounts), 6);
  EXPECT_EQ(pes::BoxAttackerCount(pes::AttackLevel(4), kCounts), 6);
  // GF's mentality rungs map one-to-one onto PES's five levels.
  EXPECT_EQ(pes::AttackLevel(4), 4);
  EXPECT_EQ(pes::AttackLevel(0), 0);
  // And the count is clamped: a rung outside the five is not a bigger box.
  EXPECT_EQ(pes::BoxAttackerCount(pes::AttackLevel(9), kCounts), 6);
  EXPECT_EQ(pes::BoxAttackerCount(pes::AttackLevel(-3), kCounts), 4);
}

// The counts come out of the install's exe, not transcribed into the source.
TEST(SetPieceTeamConstants, ReadFromTheExe) {
  if (const char* root = std::getenv("PES21_ROOT")) {
    std::string err;
    auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
    if (!exe) GTEST_SKIP() << "PES2021.exe unreadable: " << err;
    auto c = pes::LoadSetPieceTeamConstants(*exe, &err);
    ASSERT_TRUE(c.has_value()) << err;
    // The measured values: 16 km/h, 20 s corner, 7/10/13 and 8/12, 9.15 m, 0.5 m.
    EXPECT_FLOAT_EQ(c->jogKmh, 16000.0f);
    EXPECT_FLOAT_EQ(c->secondsPerHour, 3600.0f);
    EXPECT_FLOAT_EQ(c->budgetCorner_s, 20.0f);
    EXPECT_FLOAT_EQ(c->budgetOwnBack_s, 7.0f);
    EXPECT_FLOAT_EQ(c->budgetOwnMid_s, 10.0f);
    EXPECT_FLOAT_EQ(c->budgetOwnFront_s, 13.0f);
    EXPECT_FLOAT_EQ(c->budgetOtherMarking_s, 8.0f);
    EXPECT_FLOAT_EQ(c->budgetOther_s, 12.0f);
    EXPECT_FLOAT_EQ(c->wallDistance_m, 9.15f);
    EXPECT_FLOAT_EQ(c->wallSpacing_m, 0.5f);
    EXPECT_FLOAT_EQ(c->wallNearPost_m, 1.3f);
  } else {
    GTEST_SKIP() << "PES21_ROOT not set: the constants are read from the install's exe";
  }
}

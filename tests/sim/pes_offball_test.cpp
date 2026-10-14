// PES 2021's off-ball run manager, Chance run target and stop test, and the
// designated supporter (docs/re/pes-re-off-ball.md, pes-re-run-targets.md).
// Synthetic constants everywhere except the one install test.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cmath>

#include "onthepitch/pesoffball.hpp"

namespace {

using namespace pes::offball;

OffBallConstants Synthetic() {
  OffBallConstants k{};
  // selectorVision.o columns: L = 0..4, one row per run in the file's key order.
  const uint8_t caps[kCapRunCount][kAttackLevels] = {
      {1, 1, 2, 2, 3}, {0, 1, 2, 2, 3}, {0, 1, 1, 2, 2},
      {1, 2, 3, 4, 5}, {2, 3, 4, 4, 4}, {0, 1, 1, 1, 1}};
  for (int r = 0; r < kCapRunCount; ++r)
    for (int l = 0; l < kAttackLevels; ++l) k.offerNum[r][l] = caps[r][l];
  k.chanceScoreLimit = 60;
  k.chanceScoreBase = 100.0f;
  k.laneAhead_m = 0.2f;
  k.blockConeFlat = 0.6f;
  k.blockConeSlope = 0.4f;
  k.blockRadius_m = 7.0f;
  k.laneWindow = 8;
  k.reachDhFrom_m = 12.0f;
  k.reachDhTo_m = 30.0f;
  k.reachDhDiv_m = -18.0f;
  k.reachBase_m = 15.0f;
  k.reachDhGain_m = 5.0f;
  k.reachCap_m = 37.0f;
  k.awareFrom = 75.0f;
  k.awareTo = 95.0f;
  k.awareSpan = 20.0f;
  k.awareBonus_m = 18.0f;
  k.gridCellsX = 104.0f;
  k.gridCellsZ = 68.0f;
  k.gridRadiusSq_m2 = 16.0f;
  k.gridFalloff_m = 4.0f;
  k.gridScale = 0.25f;
  k.fallbackSide_m = 10.0f;
  k.predNearSq_m2 = 196.0f;
  k.predMidSq_m2 = 676.0f;
  k.predNear_s = 0.28f;
  k.predMid_s = 0.4f;
  k.predFar_s = 0.6f;
  k.predLead_s = 0.1f;
  k.predOffBall_s = 0.3f;
  return k;
}

TEST(OffBallRank, RunsNeverPreemptRunsOrConceptMoves) {
  EXPECT_TRUE(MayTake(kChance, kBasePosition));
  EXPECT_TRUE(MayTake(kChance, kNothing));
  EXPECT_TRUE(MayTake(kChance, kMoveOnPass));
  EXPECT_FALSE(MayTake(kChance, kCounter));
  EXPECT_FALSE(MayTake(kCounter, kChance));
  EXPECT_FALSE(MayTake(kChance, kOneTwoPassRun));
  EXPECT_FALSE(MayTake(kChance, kTeammateMoving));
}

TEST(OffBallCaps, ColumnByAttackLevelWithTikiTakaOverride) {
  const OffBallConstants k = Synthetic();
  EXPECT_EQ(RunCap(k, kCapChance, 2, false), 3);
  EXPECT_EQ(RunCap(k, kCapCounter, 0, false), 2);
  EXPECT_EQ(RunCap(k, kCapChance, 2, true), 1);
  EXPECT_EQ(RunCap(k, kCapSecondLine, 3, true), 0);
  EXPECT_EQ(RunCap(k, kCapPullAway, 4, true), 0);
  EXPECT_EQ(RunCap(k, kCapCounter, 2, true), 4);
}

TEST(OffBallSelect, ChanceMayTakeOnePastTheCapOtherRunsStopAtIt) {
  std::array<float, kSlots> scores{};
  scores[3] = 120.0f;
  scores[5] = 140.0f;
  scores[7] = 110.0f;
  // Cap reached: an ordinary run takes nobody, Chance takes its best one.
  EXPECT_TRUE(SelectRunners(scores, 2, 2, 1, false).empty());
  const std::vector<int> c = SelectRunners(scores, 2, 2, kChancePerTick, true);
  ASSERT_EQ(c.size(), 1u);
  EXPECT_EQ(c[0], 5);
  // Room for two: best first, stops at the cap.
  const std::vector<int> two = SelectRunners(scores, 0, 2, kChancePerTick, true);
  ASSERT_EQ(two.size(), 2u);
  EXPECT_EQ(two[0], 5);
  EXPECT_EQ(two[1], 3);
  // One new runner per tick for non-Chance selectors.
  EXPECT_EQ(SelectRunners(scores, 0, 4, 1, false).size(), 1u);
  // A zero score is never taken.
  std::array<float, kSlots> none{};
  EXPECT_TRUE(SelectRunners(none, 0, 4, kChancePerTick, true).empty());
}

TEST(OffBallChance, ScoreIsBasePlusForwardX) {
  const OffBallConstants k = Synthetic();
  EXPECT_FLOAT_EQ(ChanceScore(k, -1.0f, -20.0f), 120.0f);
  EXPECT_GT(ChanceScore(k, 1.0f, 30.0f), ChanceScore(k, 1.0f, 10.0f));
}

TEST(OffBallLane, DefendersArePredictedFurtherTheFurtherTheRunnerIsFromTheBall) {
  const OffBallConstants k = Synthetic();
  // int(54·a + 0.5) + int(54·0.1 + 0.5) + int(54·0.3 + 0.5)
  EXPECT_EQ(DefenderPredictFrames(k, 100.0f), 15 + 5 + 16);
  EXPECT_EQ(DefenderPredictFrames(k, 400.0f), 22 + 5 + 16);
  EXPECT_EQ(DefenderPredictFrames(k, 900.0f), 32 + 5 + 16);
}

LaneSearchInput OpenField() {
  LaneSearchInput in;
  in.s = 1.0f;
  in.runner = {10.0f, 0.0f};
  in.runnerPred = {10.0f, 0.0f};
  in.ball = {0.0f, 0.0f};
  in.defLineX = 25.0f;
  in.opponents = {{25.0f, 0.0f}, {25.0f, 1.0f}, {40.0f, 20.0f}};
  in.deepest = {{25.0f, 0.0f}, {25.0f, 1.0f}};
  in.teamDir_deg = 0.0f;
  in.limit = 60;
  return in;
}

TEST(OffBallLane, TargetsTheLineInTheFreeLaneNotIntoTheDefenders) {
  const OffBallConstants k = Synthetic();
  const auto t = ChanceLaneTarget(k, OpenField());
  ASSERT_TRUE(t.has_value());
  EXPECT_FLOAT_EQ(t->x, 25.0f);
  // Lanes are 2 m apart from the touchline: z = F1 - 2 - 2k.
  const float lane = (kFieldHalfWidth_m - 2.0f - t->z) / 2.0f;
  EXPECT_NEAR(lane, std::round(lane), 1e-4f);
  // The defenders at z = 0..1 empty the S table there: not that lane.
  EXPECT_GT(std::fabs(t->z), 2.0f);
}

TEST(OffBallLane, LineBehindTheRunnerMovesJustAheadOfHim) {
  const OffBallConstants k = Synthetic();
  LaneSearchInput in = OpenField();
  in.runner = in.runnerPred = {30.0f, 0.0f};
  in.ball = {24.0f, 0.0f};  // the runner within 10 m of it (FUN_14066d720)
  const auto t = ChanceLaneTarget(k, in);
  ASSERT_TRUE(t.has_value());
  EXPECT_FLOAT_EQ(t->x, 30.2f);
}

TEST(OffBallLane, AnOpponentAcrossTheRouteBlocksThatLane) {
  const OffBallConstants k = Synthetic();
  LaneSearchInput in = OpenField();
  const auto open = ChanceLaneTarget(k, in);
  ASSERT_TRUE(open.has_value());
  // Off the line (no S effect), out of the cones, sat on the route to it.
  in.opponents.push_back({20.0f, open->z});
  const auto t = ChanceLaneTarget(k, in);
  ASSERT_TRUE(t.has_value());
  EXPECT_NE(t->z, open->z);
}

TEST(OffBallLane, RunnerWellAheadOfTheBallTakesNoLaneFarFromIt) {
  // FUN_14066d720: outside the box, a runner 10 m+ ahead of the ball rejects
  // lanes more than 27 m (729 m²) from it.
  const OffBallConstants k = Synthetic();
  LaneSearchInput in = OpenField();
  // Exactly 10 m ahead is not "more than": the open field's z = 16 lane stands.
  ASSERT_TRUE(ChanceLaneTarget(k, in).has_value());
  // 11 m ahead: every lane past |z| = 10.2 is over 27 m from the ball, and the
  // nearer ones are blocked; the line point 10 m aside (725 m²) is left.
  in.runner = in.runnerPred = {11.0f, 0.0f};
  const auto t = ChanceLaneTarget(k, in);
  ASSERT_TRUE(t.has_value());
  EXPECT_FLOAT_EQ(t->x, 25.0f);
  EXPECT_FLOAT_EQ(t->z, 10.0f);
}

TEST(OffBallLane, UnderTheLimitFallsBackToTheLineBesideTheBall) {
  const OffBallConstants k = Synthetic();
  LaneSearchInput in = OpenField();
  in.limit = 101;
  in.runner = in.runnerPred = {10.0f, -3.0f};  // below the ball: the -z side
  const auto t = ChanceLaneTarget(k, in);
  ASSERT_TRUE(t.has_value());
  EXPECT_FLOAT_EQ(t->x, in.defLineX);
  EXPECT_FLOAT_EQ(t->z, -10.0f);
  in.ball = {-20.0f, 0.0f};  // far behind: the fallback fails the far-lane leaf too
  EXPECT_FALSE(ChanceLaneTarget(k, in).has_value());
}

// Team 0 attacks +x with the ball at its carrier (id 0) on the halfway line;
// runner id 3 is in a Chance run 10 m ahead; team 1 holds its line at 25 m.
constexpr int kRunner = 3;
RunWorld Running() {
  RunWorld w;
  for (int i = 0; i < 2 * kSlots; ++i) {
    w.p[i].active = true;
    w.p[i].roleClass = 4;
    w.p[i].pos = {i < kSlots ? -20.0f : 40.0f, -30.0f + 3.0f * (i % kSlots)};
    w.p[i].aim = w.p[i].pos;
    w.p[i].slotPoint = w.p[i].pos;
    w.p[i].facing = i < kSlots ? 90.0f : 270.0f;
  }
  w.carrier = 0;
  w.receiver = {0, -1};
  w.p[0].pos = w.ball = {0.0f, 0.0f};
  PlayerFacts& r = w.p[kRunner];
  r.pos = {10.0f, 0.0f};
  r.aim = {25.0f, 0.0f};
  r.state = kChance;
  r.t = 30.0f;
  w.lineX = {-30.0f, 25.0f};
  w.possession = {true, false};
  w.possessionAge = {500.0f, 0.0f};
  return w;
}

TEST(OffBallMask, AttackSlotsOpenToAllUntilEnoughAttackersAreForward) {
  RunWorld w = Running();
  for (int i = 0; i < kSlots; ++i) w.p[i].position = i < 9 ? std::min(i, 6) : 12;  // 9, 10 forwards
  EXPECT_EQ(AttackSlotMask(w, 0, 4), (1u << kSlots) - 1);
  // Level 2 needs 4 attackers ahead of the ball band; only the runner is.
  EXPECT_EQ(AttackSlotMask(w, 0, 2), (1u << kSlots) - 1);
  for (int i = 7; i < kSlots; ++i) w.p[i].pos = {12.0f, -10.0f + 5.0f * i - 35.0f};
  const uint32_t m = AttackSlotMask(w, 0, 2);
  EXPECT_TRUE((m >> 9) & 1u);  // forwards always, and they count toward the 4
  EXPECT_TRUE((m >> 10) & 1u);
  int others = 0;
  for (int i = 0; i < 9; ++i) others += (m >> i) & 1u;
  EXPECT_EQ(others, 2);
  EXPECT_TRUE((m >> 7) & 1u);  // the two nearest the goal line among the rest
  EXPECT_TRUE((m >> 8) & 1u);
}

TEST(OffBallMask, ADefenderNeedsTheReachAForwardDoesNot) {
  const OffBallConstants k = Synthetic();
  RunWorld w = Running();
  w.p[kRunner].state = kBasePosition;
  w.p[kRunner].t = 1e9f;
  w.lineX[1] = 45.0f;                // the runner stays onside
  w.p[kRunner].pos = {30.0f, 0.0f};  // 30 m ahead of the ball: past 15 m
  w.p[kRunner].position = 1;         // CB
  EXPECT_FALSE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
  w.p[kRunner].position = 12;        // CF: no reach path
  w.lineX[1] = 25.0f;                // ... but not from beyond the line
  EXPECT_FALSE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
  w.lineX[1] = 45.0f;
  w.p[kRunner].roleClass = 9;
  EXPECT_TRUE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
  EXPECT_FALSE(ChanceMaskBit(k, w, kRunner, ~(1u << kRunner), 2, false, 50.0f));
  w.p[kRunner].pos = {30.0f, 43.0f};  // |dz| > 42
  EXPECT_FALSE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
  w.p[kRunner].pos = {12.0f, 0.0f};
  w.p[kRunner].position = 1;
  EXPECT_TRUE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
  w.p[kRunner].pos = {19.0f, 0.0f};  // 19 m: past 15, inside Long Pass's 21
  EXPECT_FALSE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
  EXPECT_TRUE(ChanceMaskBit(k, w, kRunner, ~0u, 2, true, 50.0f));
}

TEST(OffBallMask, NoChanceRunsFromOwnHalfUntilFourSecondsOfPossession) {
  // FUN_1405938a0: in its own half a team runs only with a free forward-facing
  // focus player or after 4 s of possession.
  const OffBallConstants k = Synthetic();
  RunWorld w = Running();
  w.p[kRunner].state = kBasePosition;
  w.p[kRunner].position = 12;
  w.p[0].pos = w.ball = {-5.0f, 0.0f};
  w.p[kRunner].pos = {5.0f, 0.0f};
  EXPECT_TRUE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
  w.possessionAge[0] = 100.0f;
  EXPECT_FALSE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
  w.p[0].vel = {5.0f, 0.0f};  // the carrier runs forward, nobody near: free
  EXPECT_TRUE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
}

TEST(OffBallStop, ChanceKeepsItsMinimumRunTimeThenTheCommonTestDecides) {
  RunWorld w = Running();
  EXPECT_FALSE(RunStop(w, kRunner, kChance));
  w.carrier = kRunner;
  EXPECT_TRUE(RunStop(w, kRunner, kChance));
  w.p[kRunner].t = 5.0f;
  EXPECT_FALSE(RunStop(w, kRunner, kChance));  // under 11 frames nothing stops it
  w = Running();
  w.possession = {false, true};
  EXPECT_TRUE(RunStop(w, kRunner, kChance));
}

TEST(OffBallStop, ReleasedPastTheLineOrNearTheGoalLine) {
  RunWorld w = Running();
  w.p[kRunner].pos = {26.0f, 0.0f};  // 1 m past: under 1.5 early, over 0.7 late
  w.p[0].pos = w.ball = {20.0f, 0.0f};
  EXPECT_FALSE(RunStop(w, kRunner, kChance));
  w.p[kRunner].t = 50.0f;
  EXPECT_TRUE(RunStop(w, kRunner, kChance));
  w = Running();
  w.lineX[1] = 60.0f;
  w.p[0].pos = w.ball = {45.0f, 0.0f};
  w.p[kRunner].pos = {50.0f, 0.0f};
  EXPECT_TRUE(RunStop(w, kRunner, kChance));
}

TEST(OffBallStop, FarRunnerStopsUnlessWellBehindTheBall) {
  RunWorld w = Running();
  w.p[kRunner].pos = {-35.0f, 0.0f};
  w.p[kRunner].aim = {-20.0f, 0.0f};
  w.p[0].pos = w.ball = {10.0f, 0.0f};  // 45 m away, 45 m behind: keep
  EXPECT_FALSE(RunStop(w, kRunner, kChance));
  w.p[kRunner].pos = {10.0f, 0.0f};
  w.p[0].pos = w.ball = {10.0f, -45.0f};  // 45 m away, level: stop
  EXPECT_TRUE(RunStop(w, kRunner, kChance));
}

TEST(OffBallStop, LooseBallKickedFarPastHimEndsTheRun) {
  // FUN_140692810: no carrier, a kick within the last second carried the ball
  // 20 m+, and it is not ahead of the runner.
  RunWorld w = Running();
  w.carrier = -1;
  w.ball = {8.0f, 0.0f};
  w.ev = {true, 1, 20.0f, {-15.0f, 0.0f}, 7};
  EXPECT_TRUE(RunStop(w, kRunner, kChance));
  w.ev.pos = {0.0f, 0.0f};  // only 8 m: no
  EXPECT_FALSE(RunStop(w, kRunner, kChance));
}

TEST(OffBallStop, ThePassToHimEndsTheRunAThroughBallAfterEighteenFrames) {
  RunWorld w = Running();
  w.ev = {true, 1, 2.0f, {0.0f, 0.0f}, kRunner};
  EXPECT_TRUE(RunStop(w, kRunner, kChance));
  w.ev.kind = 2;
  EXPECT_FALSE(RunStop(w, kRunner, kChance));
  w.ev.age = 18.0f;
  EXPECT_TRUE(RunStop(w, kRunner, kChance));
}

TEST(OffBallStop, ADefenderRunsOnlyIfNoCloserDefenderIsRunning) {
  // FUN_140596240 over the role-class map: a CB/SB runner stops when another
  // CB/SB runner is nearer the ball.
  RunWorld w = Running();
  w.p[kRunner].roleClass = 2;
  EXPECT_FALSE(RunStop(w, kRunner, kChance));
  w.p[5].roleClass = 1;
  w.p[5].state = kChance;
  w.p[5].pos = {5.0f, 2.0f};
  EXPECT_TRUE(RunStop(w, kRunner, kChance));
}

TEST(OffBallSupport, SupporterCostPrefersInFrontAndPenalisesTooClose) {
  EXPECT_FLOAT_EQ(SupporterCost(10.0f, 12.0f), 19.0f);
  EXPECT_FLOAT_EQ(SupporterCost(0.0f, 8.0f), 23.0f);
  EXPECT_LT(SupporterCost(0.0f, 12.0f), SupporterCost(40.0f, 12.0f));
}

TEST(OffBallSupport, DistanceGrowsWithSupportRangeAndDeepBall) {
  SupportDistanceInput in;
  in.inPlay = true;
  in.supportRange = 1;
  in.ballX = 0.0f;
  in.ownLineX = -30.0f;
  in.oppLineX = 30.0f;
  in.bearingFromAttack_deg = 0.0f;
  const float low = SupportDistance_m(in);
  in.supportRange = 10;
  EXPECT_NEAR(SupportDistance_m(in) - low, 3.0f, 1e-4f);
  in.ballX = -25.0f;  // deep in own half
  EXPECT_GT(SupportDistance_m(in), low + 3.0f);
  in.inPlay = false;
  EXPECT_FLOAT_EQ(SupportDistance_m(in), 8.0f);
}

TEST(OffBallInstall, CapsAndLimitFromTheInstall) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT unset";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto k = LoadOffBallConstants(*exe, vfs, &err);
  ASSERT_TRUE(k) << err;
  for (int r = 0; r < kCapRunCount; ++r)
    for (int l = 1; l < kAttackLevels; ++l) EXPECT_GE(k->offerNum[r][l], k->offerNum[r][l - 1]);
  EXPECT_GT(k->chanceScoreLimit, 0);
  EXPECT_LE(k->chanceScoreLimit, 255);
  EXPECT_GT(k->reachCap_m, k->reachBase_m);
  EXPECT_GT(k->awareTo, k->awareFrom);
  EXPECT_EQ(k->laneWindow, 8);
}

}  // namespace

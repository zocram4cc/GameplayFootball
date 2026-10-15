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

// FUN_140654e60: the frames to reach a point. A player walking at the slow
// tier covers 100/(fps·72) m per frame and a jogger 100/(fps·12.9).
TEST(OffBallReach, FramesToReachGrowsWithDistanceAndFallsWithSpeed) {
  RunWorld w = Running();
  w.p[0].pos = {0.0f, 0.0f};
  w.p[0].facing = 0.0f;               // toward +x, the target's direction
  w.p[0].position = 5;                // a midfielder: no keeper's half scale
  w.p[0].vel = {0.02f, 0.0f};         // 1.08 km/h-equivalent: standing still
  const int near = FramesToReach(w, 0, {10.0f, 0.0f});
  const int far = FramesToReach(w, 0, {30.0f, 0.0f});
  EXPECT_GT(far, near);
  // A faster player reaches the same point sooner.
  w.p[0].vel = {0.15f, 0.0f};         // 8.1 km/h
  EXPECT_LT(FramesToReach(w, 0, {10.0f, 0.0f}), near);
  // Already there and facing it: only the speed-ramped term, and a player at
  // rest below 11 km/h has u = 0, so no frames at all.
  w.p[0].vel = {0.0f, 0.0f};
  EXPECT_EQ(FramesToReach(w, 0, {0.0f, 0.0f}), 0);
  // Past the 20 s ceiling FUN_140654e60 falls back to the straight-line jog
  // tier 100/(fps·12.9) m/frame, which needs 154 m of run to reach.
  w.p[0].vel = {0.02f, 0.0f};
  const int capped = FramesToReach(w, 0, {200.0f, 0.0f});
  EXPECT_EQ(capped, static_cast<int>(200.0f / (100.0f / (kPesFps * 12.9f))));
}

// FUN_14068b1a0: the crowded-mate stop. An SB (AI class 2) runner inside 20 m
// of the ball, behind the opponent line by at most 7 m, is released when
// another runner who is not a class-1 Chance mate stands within 12 m.
TEST(OffBallStop, AnSBWithAnotherRunnerTwelveMetresAwayIsReleased) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.aiClass = 2;             // SB
  r.state = kChance;
  r.t = kPesFps * 4.0f;      // past every early exemption
  r.pos = {20.0f, 3.0f};     // 5 m behind the 25 m line, 10 m from the ball
  r.aim = {25.0f, 3.0f};
  w.p[0].pos = w.ball = {10.0f, 3.0f};
  w.lineX[1] = 25.0f;
  PlayerFacts& m = w.p[5];
  m.pos = {22.0f, 3.0f};     // 2 m away
  m.state = kChance;         // on a run
  m.aiClass = 5;
  EXPECT_TRUE(CrowdedMateStop(w, kRunner));
  // A CB instead of an SB: not this leaf.
  r.aiClass = 1;
  EXPECT_FALSE(CrowdedMateStop(w, kRunner));
  r.aiClass = 2;
  // More than 7 m behind the line: not this leaf.
  r.pos = {17.0f, 3.0f};
  EXPECT_FALSE(CrowdedMateStop(w, kRunner));
  r.pos = {20.0f, 3.0f};
  // Too far from the ball: not this leaf.
  r.pos = {45.0f, 3.0f};
  EXPECT_FALSE(CrowdedMateStop(w, kRunner));
}

TEST(OffBallStop, ACrowdingMateMustBeRunningAndTwelveMetresAway) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.aiClass = 2;
  r.state = kChance;
  r.t = kPesFps * 4.0f;
  r.pos = {20.0f, 3.0f};
  r.aim = {25.0f, 3.0f};
  w.p[0].pos = w.ball = {10.0f, 3.0f};
  PlayerFacts& m = w.p[5];
  m.pos = {22.0f, 3.0f};
  m.state = kNothing;       // not running: he cannot crowd
  EXPECT_FALSE(CrowdedMateStop(w, kRunner));
  m.state = kChance;        // running, and inside 12 m
  EXPECT_TRUE(CrowdedMateStop(w, kRunner));
  m.pos = {40.0f, 3.0f};    // 20 m away
  EXPECT_FALSE(CrowdedMateStop(w, kRunner));
  m.pos = {22.0f, 3.0f};
  m.aiClass = 1;            // a class-1 Chance mate: exempt
  m.state = kChance;
  EXPECT_FALSE(CrowdedMateStop(w, kRunner));
}

// FUN_14068aeb0: the pass-to-me stop. With a kick in flight whose recorded
// target is not this runner, an opponent within 5 m of him and the runner
// 1.2-4 m behind the opponent line, the run is released when he reaches his
// own route point inside the receiver's lead plus int(fps·0.15+0.5): the
// receiver can no longer beat him to the point the pass is played to.
TEST(OffBallStop, TheRunnerIsReleasedWhenHeBeatsTheReceiverToHisPoint) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.aiClass = 5;                 // not 2 (SB) and not 0x13
  r.state = kChance;
  r.t = kPesFps * 5.0f;          // past the 2·fps gate
  r.pos = {22.0f, 3.0f};         // 3 m behind the 25 m line
  r.aim = {25.0f, 3.0f};
  w.p[0].pos = w.ball = {10.0f, 3.0f};
  w.lineX[1] = 25.0f;
  // The kick goes to slot 7, not to the runner, and is under way.
  w.receiver[0] = 7;
  w.ev = {true, 1, 3.0f, {10.0f, 3.0f}, 7};
  // An opponent next to the runner (5 m cone) and the receiver, slower, far off.
  PlayerFacts& o = w.p[kSlots + 4];
  o.pos = {24.0f, 3.0f};
  PlayerFacts& q = w.p[7];
  q.pos = {-6.0f, 3.0f};           // 16 m from the ball: the 0.28 s lead band
  r.aim = {22.1f, 3.0f};          // he is already at his own route point
  EXPECT_TRUE(PassToMeStop(w, kRunner));
  // The runner's own target pushed far away: he needs more frames than the
  // receiver's lead, so the keep ends.
  r.aim = {2.0f, 3.0f};
  EXPECT_FALSE(PassToMeStop(w, kRunner));
  r.aim = {22.1f, 3.0f};
  // The pass is to the runner himself: never a keep.
  w.ev.target = kRunner;
  EXPECT_FALSE(PassToMeStop(w, kRunner));
  w.ev.target = 7;
  // No kick at all: never a keep.
  w.ev.valid = false;
  EXPECT_FALSE(PassToMeStop(w, kRunner));
  w.ev.valid = true;
  // No opponent within 5 m: never a keep.
  o.pos = {45.0f, 3.0f};
  EXPECT_FALSE(PassToMeStop(w, kRunner));
}

// The lead band: the receiver's lead comes from his distance to the ball,
// 196/676 m² thresholds to 0.28/0.35/0.38 s (FUN_140653480), and the runner is
// released only when he reaches his own route point inside that lead plus
// int(fps·0.15+0.5) = 8. Each pair below pins one threshold by choosing the
// runner's distance to his own point so the margin lands just inside or just
// outside the band.
TEST(OffBallStop, PassToMeStopReleasesInsideTheNearLeadBand) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.aiClass = 5;
  r.state = kChance;
  r.t = kPesFps * 5.0f;
  r.pos = {22.0f, 3.0f};
  r.facing = 90.0f;
  r.aim = r.pos;                 // on his own point: 2 frames, 15 >= 2 + 8
  w.p[0].pos = w.ball = {10.0f, 3.0f};
  w.lineX[1] = 25.0f;
  w.receiver[0] = 7;
  w.ev = {true, 1, 3.0f, {10.0f, 3.0f}, 7};
  w.p[kSlots + 4].pos = {24.0f, 3.0f};
  w.p[7].pos = {-3.0f, 3.0f};    // 13 m from the ball (169 m²): int(fps·0.28+0.5) = 15
  EXPECT_TRUE(PassToMeStop(w, kRunner));
  w.p[7].pos = {3.0f, 3.0f};     // 7 m from the ball (49 m²): the same 15
  EXPECT_TRUE(PassToMeStop(w, kRunner));
  w.p[7].pos = {-17.0f, 3.0f};   // 27 m from the ball (729 m²): int(fps·0.38+0.5) = 21
  EXPECT_TRUE(PassToMeStop(w, kRunner));
}

TEST(OffBallStop, PassToMeStopKeepsOutsideTheNearLeadBand) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.aiClass = 5;
  r.state = kChance;
  r.t = kPesFps * 5.0f;
  r.pos = {22.0f, 3.0f};
  r.facing = 90.0f;
  r.aim = {22.25f, 3.0f};        // 0.25 m out: 11 frames, 15 < 11 + 8
  w.p[0].pos = w.ball = {10.0f, 3.0f};
  w.lineX[1] = 25.0f;
  w.receiver[0] = 7;
  w.ev = {true, 1, 3.0f, {10.0f, 3.0f}, 7};
  w.p[kSlots + 4].pos = {24.0f, 3.0f};
  w.p[7].pos = {3.0f, 3.0f};     // 49 m²: the 15-frame band cannot reach him
  EXPECT_FALSE(PassToMeStop(w, kRunner));
  w.p[7].pos = {-17.0f, 3.0f};   // 729 m²: the 21-frame band can
  EXPECT_TRUE(PassToMeStop(w, kRunner));
}

// The lead bands, FUN_140653480's distance ramp against the ball: 0.28 s inside
// 196 m², 0.35 s inside 676 m², 0.38 s beyond it. A runner 0.25 m from his own
// point needs 12 frames, so the 15-frame near band and the 19-frame middle band
// keep him while the 21-frame far band releases him - which pins all three
// thresholds at once.
TEST(OffBallStop, PassToMeStopReleasesInTheFarAndMiddleLeadBands) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.aiClass = 5;
  r.state = kChance;
  r.t = kPesFps * 5.0f;
  r.pos = {22.0f, 3.0f};
  r.facing = 90.0f;
  r.aim = {22.25f, 3.0f};        // 12 frames to his own point
  w.p[0].pos = w.ball = {10.0f, 3.0f};
  w.lineX[1] = 25.0f;
  w.receiver[0] = 7;
  w.ev = {true, 1, 3.0f, {10.0f, 3.0f}, 7};
  w.p[kSlots + 4].pos = {24.0f, 3.0f};
  w.p[7].pos = {-17.0f, 3.0f};   // 729 m²: int(fps·0.38+0.5) = 21, and 21 >= 20
  EXPECT_TRUE(PassToMeStop(w, kRunner));
  w.p[7].pos = {-15.0f, 3.0f};   // 625 m²: int(fps·0.35+0.5) = 19, and 19 < 20
  EXPECT_FALSE(PassToMeStop(w, kRunner));
  w.p[7].pos = {3.0f, 3.0f};     // 49 m²: the 15-frame near band cannot reach him
  EXPECT_FALSE(PassToMeStop(w, kRunner));
}

// The far band's ceiling: a runner 0.45 m from his own point (16 frames) is
// NOT released by a far-band receiver - 21 < 24 - so a longer far lead than
// 0.38 s would wrongly release him.
TEST(OffBallStop, PassToMeStopFarBandKeepsARunnerTooFarFromHisPoint) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.aiClass = 5;
  r.state = kChance;
  r.t = kPesFps * 5.0f;
  r.pos = {22.0f, 3.0f};
  r.facing = 90.0f;
  r.aim = {22.45f, 3.0f};        // 16 frames to his own point
  w.p[0].pos = w.ball = {10.0f, 3.0f};
  w.lineX[1] = 25.0f;
  w.receiver[0] = 7;
  w.ev = {true, 1, 3.0f, {10.0f, 3.0f}, 7};
  w.p[kSlots + 4].pos = {24.0f, 3.0f};
  w.p[7].pos = {-17.0f, 3.0f};   // 729 m²: int(fps·0.38+0.5) = 21 < 24
  EXPECT_FALSE(PassToMeStop(w, kRunner));
  w.p[7].pos = {3.0f, 3.0f};     // 49 m²: 15 < 24 as well
  EXPECT_FALSE(PassToMeStop(w, kRunner));
}

TEST(OffBallStop, PassToMeStopNeedsTheRunnerNearTheLine) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.aiClass = 5;
  r.state = kChance;
  r.t = kPesFps * 5.0f;
  r.pos = {22.0f, 3.0f};
  r.aim = {22.1f, 3.0f};
  w.p[0].pos = w.ball = {10.0f, 3.0f};
  w.lineX[1] = 25.0f;
  w.receiver[0] = 7;
  w.ev = {true, 1, 3.0f, {10.0f, 3.0f}, 7};
  w.p[kSlots + 4].pos = {24.0f, 3.0f};
  PlayerFacts& q = w.p[7];
  q.pos = {-6.0f, 3.0f};
  EXPECT_TRUE(PassToMeStop(w, kRunner));
  // More than 4 m behind the line (a4c9a0 -4): not this keep.
  r.pos = {19.0f, 3.0f};
  EXPECT_FALSE(PassToMeStop(w, kRunner));
  r.pos = {22.0f, 3.0f};
  r.aim = {22.1f, 3.0f};
  // More than 1.2 m past the line (a4c9a0 -1.2): not this keep.
  r.pos = {26.5f, 3.0f};
  EXPECT_FALSE(PassToMeStop(w, kRunner));
}

// FUN_140606cd0's rank > 1 angle sub-test (raw 0x140607741-0x140607814): a
// forward on the second line or later whose route leaves him within 80 deg of
// the attack direction and within 20 m of the ball-line point is only a
// candidate while that point is in the attacking half - GF carries no
// instruction 2, so a4c720's arguments are a = 0 and k = 0.
TEST(OffBallMask, ASecondLineForwardNeedsTheLinePointInTheAttackingHalf) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.state = kBasePosition;
  r.t = 1e9f;
  r.position = 12;                 // CF: the reach path needs no style bit
  r.pos = {0.0f, -2.0f};           // on the halfway line, off centre
  r.aim = {4.0f, -2.0f};
  r.facing = 90.0f;                // the attack direction, +x
  w.lineX[1] = 45.0f;
  w.carrier = 0;
  // A teammate further up the pitch makes this runner the second line.
  w.p[9].pos = {40.0f, 6.0f};
  w.p[10].pos = {41.0f, -6.0f};
  w.p[8].pos = {39.0f, 10.0f};
  // The ball-line point beside the runner, 6 m to his goal side and 2 m into
  // the attacking half: the bearing from it to him is inside the 45 deg window
  // of his facing, and inside 20 m.
  w.p[0].pos = w.ball = {2.0f, -8.0f};
  EXPECT_TRUE(RankAngleOk(w, kRunner, 4));
  // The same line point 2 m into the defending half: the sub-test rejects him.
  w.p[0].pos = w.ball = {-2.0f, -8.0f};
  EXPECT_FALSE(RankAngleOk(w, kRunner, 4));
}

TEST(OffBallMask, TheAngleSubTestOnlyRunsForAForwardOnTheSecondLine) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.state = kBasePosition;
  r.t = 1e9f;
  r.position = 12;                 // CF
  r.pos = {0.0f, -2.0f};
  r.aim = {4.0f, -2.0f};
  r.facing = 90.0f;
  w.p[0].pos = w.ball = {2.0f, -8.0f};   // 6 m to his side, in the window
  w.lineX[1] = 45.0f;
  w.carrier = 0;
  w.p[9].pos = {40.0f, 6.0f};
  w.p[10].pos = {41.0f, -6.0f};
  w.p[8].pos = {39.0f, 10.0f};
  EXPECT_TRUE(RankAngleOk(w, kRunner, 4));
  // An AMF with three attackers ahead: positions 4 and up take no reach path,
  // so the sub-test is not entered at all and the line point's half does not
  // matter. (With four ahead the OR gate would let every rank in.)
  r.position = 8;
  w.p[8].pos = {-20.0f, -14.0f};
  w.p[9].pos = {-20.0f, -17.0f};
  w.p[10].pos = {-20.0f, -20.0f};
  w.p[0].pos = w.ball = {-2.0f, -8.0f};
  EXPECT_TRUE(RankAngleOk(w, kRunner, 4));
  r.position = 12;
  // A CF on the first line: the rank gate is not entered, so no veto.
  for (int i = 0; i < kSlots; ++i) w.p[i].pos.x = -20.0f;
  r.pos = {0.0f, -2.0f};
  EXPECT_TRUE(RankAngleOk(w, kRunner, 4));
  // Back on the second line, 26 m from the ball-line point: past the 20 m
  // window, so the sub-test rejects him wherever the line point sits.
  w.p[9].pos = {40.0f, 6.0f};
  w.p[10].pos = {41.0f, -6.0f};
  w.p[8].pos = {39.0f, 10.0f};
  w.p[0].pos = w.ball = {0.0f, -28.0f};
  EXPECT_FALSE(RankAngleOk(w, kRunner, 4));
  // An opponent inside the 80 deg cone about the attack direction lets him
  // through even then.
  w.p[kSlots + 3].pos = {2.0f, -2.0f};
  EXPECT_TRUE(RankAngleOk(w, kRunner, 4));
}

// The sub-test's three gates, one at a time: the rank, the facing window and
// the 20 m window. Each fixture below holds the other two open.
TEST(OffBallMask, TheAngleSubTestNeedsTheSecondLineNotJustAForward) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.state = kBasePosition;
  r.t = 1e9f;
  r.position = 12;                 // CF
  r.pos = {0.0f, -2.0f};
  r.aim = {4.0f, -2.0f};
  r.facing = 90.0f;
  w.p[0].pos = w.ball = {-2.0f, -8.0f};   // the veto condition
  w.lineX[1] = 45.0f;
  w.carrier = 0;
  // Three teammates ahead of the ball band, and this runner behind them all:
  // he is the second line, so the sub-test runs.
  w.p[9].pos = {40.0f, 6.0f};
  w.p[10].pos = {41.0f, -6.0f};
  w.p[8].pos = {39.0f, 10.0f};
  for (int i = 0; i < kSlots; ++i)
    if (i != 8 && i != 9 && i != 10 && i != kRunner) w.p[i].pos = {-20.0f, -30.0f + 3.0f * i};
  EXPECT_FALSE(RankAngleOk(w, kRunner, 4));
  // Now the runner is the first line: the sub-test does not run.
  w.p[9].pos = {-20.0f, 6.0f};
  w.p[10].pos = {-20.0f, -6.0f};
  w.p[8].pos = {-20.0f, 10.0f};
  EXPECT_TRUE(RankAngleOk(w, kRunner, 4));
}

TEST(OffBallMask, TheAngleSubTestFacingWindowIsFortyFiveDegrees) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.state = kBasePosition;
  r.t = 1e9f;
  r.position = 12;                 // CF
  r.pos = {0.0f, -2.0f};
  r.aim = {4.0f, -2.0f};
  w.p[9].pos = {40.0f, 6.0f};
  w.p[10].pos = {41.0f, -6.0f};
  w.p[8].pos = {39.0f, 10.0f};
  w.p[0].pos = w.ball = {-2.0f, -8.0f};
  w.lineX[1] = 45.0f;
  w.carrier = 0;
  // Facing 60 deg off the bearing to him: outside the 45 deg window, so the
  // sub-test lets him through even with the line point behind.
  r.facing = 131.6f;
  EXPECT_TRUE(RankAngleOk(w, kRunner, 4));
  // Facing 18 deg off it (68.4): inside the window, so the veto applies.
  r.facing = 50.0f;
  EXPECT_FALSE(RankAngleOk(w, kRunner, 4));
}

TEST(OffBallMask, TheAngleSubTestWindowIsTwentyMetres) {
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.state = kBasePosition;
  r.t = 1e9f;
  r.position = 12;                 // CF
  r.pos = {0.0f, -2.0f};
  r.aim = {4.0f, -2.0f};
  r.facing = 90.0f;
  w.p[9].pos = {40.0f, 6.0f};
  w.p[10].pos = {41.0f, -6.0f};
  w.p[8].pos = {39.0f, 10.0f};
  w.p[7].pos = {38.0f, 14.0f};
  w.lineX[1] = 45.0f;
  w.carrier = 0;
  // 26.7 m from the line point (712 m2, past the 400 m2 window) with the point
  // in the attacking half, so only the window can reject him.
  w.p[0].pos = w.ball = {6.0f, -28.0f};
  EXPECT_FALSE(RankAngleOk(w, kRunner, 4));
  // Inside it, 6 m to his side (40 m2): the half test passes and he is fine.
  w.p[0].pos = w.ball = {2.0f, -8.0f};
  EXPECT_TRUE(RankAngleOk(w, kRunner, 4));
  // 12 m out (144 m2), still inside 400 m2 but past a 100 m2 window.
  w.p[0].pos = w.ball = {6.0f, -14.0f};
  EXPECT_TRUE(RankAngleOk(w, kRunner, 4));
}

// FUN_140606cd0's gates, one reason each. The classifier names the gate that
// rejected a slot, so a live trace can be read without a debugger.
TEST(OffBallMaskTrace, TheClassifierNamesTheRejectingGate) {
  const OffBallConstants k = Synthetic();
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.state = kBasePosition;
  r.t = 1e9f;
  r.position = 12;
  r.pos = {10.0f, 0.0f};
  r.aim = {20.0f, 0.0f};
  w.p[0].pos = w.ball = {0.0f, 0.0f};
  w.lineX[1] = 25.0f;
  w.carrier = 0;
  // A clean candidate: no gate rejects him.
  EXPECT_EQ(kMaskPass, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  EXPECT_TRUE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
  // The generic tail's three disjuncts need no carrier to be seen: with one,
  // the first disjunct always passes. Without one, a receiver inside
  // team-may-run's 25 m but past the tail's 49 m2 (7 m) rejects him.
  w.carrier = -1;
  w.receiver[0] = 1;
  w.p[1].pos = {10.0f, 2.0f};   // 104 m2 from the ball, 10.2 m away
  EXPECT_EQ(kMaskGeneric, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  // The tail's !Valid(q) disjunct is dead in the walk order: with no carrier
  // TeamMayRun already needs a valid focus player (593290), and with a
  // carrier the first disjunct passes first. A slot with neither never
  // reaches the tail - the team-may-run gate stops him.
  w.receiver[0] = -1;
  EXPECT_EQ(kMaskTeamMayNotRun, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  // Restored: the clean candidate the test starts on.
  w.p[0].pos = w.ball = {0.0f, 0.0f};
  w.carrier = 0;
  w.receiver[0] = 1;
  w.p[1].pos = {11.0f, 0.0f};

  // The rank/state gate: a running slot may only come from PULL_AWAY.
  r.state = kChance;
  EXPECT_EQ(kMaskRankOrState, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  r.state = kBasePosition;
  // The carrier, the receiver and the goalkeeper are never candidates.
  EXPECT_EQ(kMaskCarrierOrReceiver, ChanceMaskReject(k, w, 0, ~0u, 2, false, 50.0f));
  EXPECT_EQ(kMaskCarrierOrReceiver, ChanceMaskReject(k, w, 1, ~0u, 2, false, 50.0f));  // w.receiver[0]
  w.receiver[0] = -1;
  r.position = 0;  // the keeper
  EXPECT_EQ(kMaskCarrierOrReceiver, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  r.position = 12;
  w.receiver[0] = 1;
  // Every state IsRun() accepts also loses the rank test first, so
  // 593430's own "already running" test is unreachable through Chance:
  // kMaskRankOrState fires for all of them. (0x1d..0x21 rank 2 = Chance's
  // own rank, 0x24/0x25/0x2c rank 0, so MayTake is false either way.)
  r.state = kSecondLine;
  EXPECT_EQ(kMaskRankOrState, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  r.state = kOneTwoPassRun;
  EXPECT_EQ(kMaskRankOrState, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  r.state = kChance;
  EXPECT_EQ(kMaskRankOrState, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  r.state = kBasePosition;
  // 593430's line test: already past the opponents' line.
  w.p[kRunner].pos = {26.0f, 0.0f};
  EXPECT_EQ(kMaskOnTheLine, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  w.p[kRunner].pos = {10.0f, 0.0f};
  // The slot mask.
  EXPECT_EQ(kMaskSlotMask, ChanceMaskReject(k, w, kRunner, 0u, 2, false, 50.0f));
  // The depth limit: past F8 (41.56 m), with the line pushed far enough ahead
  // that the line test does not fire first.
  w.lineX[1] = 60.0f;
  w.p[kRunner].pos = {44.0f, 0.0f};
  EXPECT_EQ(kMaskDepth, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  w.lineX[1] = 25.0f;
  w.p[kRunner].pos = {10.0f, 0.0f};
  // The attack level's position skip: level 0 skips positions 0..4.
  r.position = 2;  // a full back
  EXPECT_EQ(kMaskAttackLevel, ChanceMaskReject(k, w, kRunner, ~0u, 0, false, 50.0f));
  r.position = 12;
  // |dz| past 42 m.
  w.p[kRunner].pos = {10.0f, 43.0f};
  EXPECT_EQ(kMaskDz, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  // Inside the box zone (past F6 - 3, within F5 + 1.5 of the centre) an
  // opponent within 6.5 m is required; z = 8 keeps the default defenders' z
  // ladder (steps of 3 from -30) clear of him and stays inside F5 + 1.5.
  w.p[kRunner].pos = {40.0f, 8.0f};
  w.lineX[1] = 60.0f;
  EXPECT_EQ(kMaskBoxZone, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  // The goal-area gate (s·x > F4) sits behind the box-zone gate, so it is only
  // reached once the opponent is there: with one beside a runner at x = 48 the
  // box zone lets him through and the goal-area gate stops him.
  w.p[kSlots + 4].pos = {42.0f, 8.0f};
  w.p[kRunner].pos = {48.0f, 8.0f};
  EXPECT_EQ(kMaskGoalArea, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  w.lineX[1] = 25.0f;
  w.p[kRunner].pos = {10.0f, 0.0f};
  // FUN_140596240: a runner of class < 3 may not run when a teammate of a
  // lower class is already running and no further from the ball than him.
  w.p[5].roleClass = 1;   // a CB
  w.p[5].pos = {9.0f, 0.0f};
  w.p[5].state = kChance;
  r.roleClass = 2;
  EXPECT_EQ(kMaskRoleClass, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  r.roleClass = 4;        // class 4 and up are exempt
  EXPECT_EQ(kMaskPass, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  w.p[5].state = kBasePosition;
  EXPECT_EQ(kMaskPass, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  r.roleClass = 4;
  // TeamMayRun: in the own half with young possession and no free
  // forward-facing focus player, the team may not start Chance runs.
  w.carrier = -1;
  w.receiver[0] = 1;
  w.p[1].pos = {11.0f, 0.0f};
  w.possessionAge[0] = 100.0f;
  w.p[1].vel = {0.0f, 0.0f};
  EXPECT_EQ(kMaskTeamMayNotRun, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  // Old possession (or a free forward-facing focus player) opens it, and then
  // the generic tail - no carrier and a receiver far from the ball - is what
  // rejects him.
  w.possessionAge[0] = 1e6f;
  EXPECT_EQ(kMaskGeneric, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
}

// The reach path is separate because its rejection depends on the runner's
// distance to the ball and the attack direction: a defender needs it, a
// forward does not, and the reach limit grows with Build Up Long.
TEST(OffBallMaskTrace, TheClassifierNamesTheReachRejection) {
  const OffBallConstants k = Synthetic();
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.state = kBasePosition;
  r.t = 1e9f;
  r.pos = {30.0f, 0.0f};          // 30 m ahead of the ball
  r.aim = {40.0f, 0.0f};
  r.position = 1;                 // a CB: the reach path applies
  w.p[0].pos = w.ball = {0.0f, 0.0f};
  w.lineX[1] = 45.0f;             // keeps him onside
  w.carrier = 0;
  EXPECT_EQ(kMaskReach, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  r.position = 12;                // a CF: no reach path
  EXPECT_EQ(kMaskPass, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  // A CB at 19 m: past the 15 m base, inside Build Up Long's 21 m.
  r.position = 1;
  r.pos = {19.0f, 0.0f};
  EXPECT_EQ(kMaskReach, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  EXPECT_EQ(kMaskPass, ChanceMaskReject(k, w, kRunner, ~0u, 2, true, 50.0f));
}

// Step 2 of the manager tick (off-ball 1.2): A[s] = int(fps·2+0.5) frames when
// a slot newly enters a run state, counting down otherwise and cleared when the
// slot is the pass receiver. While it is nonzero the slot is not a candidate -
// the 2 s re-run block, which is what keeps a released runner from being taken
// again on the very next tick.
TEST(OffBallMaskTrace, ASlotOnTheTwoSecondRerunBlockIsNotACandidate) {
  const OffBallConstants k = Synthetic();
  RunWorld w = Running();
  PlayerFacts& r = w.p[kRunner];
  r.state = kBasePosition;
  r.t = 1e9f;
  r.position = 12;
  r.pos = {10.0f, 0.0f};
  r.aim = {20.0f, 0.0f};
  w.p[0].pos = w.ball = {0.0f, 0.0f};
  w.lineX[1] = 25.0f;
  w.carrier = 0;
  EXPECT_EQ(kMaskPass, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  // On cooldown: the block gate stops him before the slot mask.
  r.runCooldown = 1;
  EXPECT_EQ(kMaskRunCooldown, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
  EXPECT_FALSE(ChanceMaskBit(k, w, kRunner, ~0u, 2, false, 50.0f));
  // Counted down to zero: a candidate again.
  r.runCooldown = 0;
  EXPECT_EQ(kMaskPass, ChanceMaskReject(k, w, kRunner, ~0u, 2, false, 50.0f));
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

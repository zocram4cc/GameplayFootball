// PES 2021's team sheet and transition concepts (pesteamconcepts.hpp).
// Synthetic constants exercise the rules; one install test checks the loader
// against the user's PES2021.exe and VFS through relations, not pinned numbers.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>

#include "onthepitch/pesteamconcepts.hpp"

namespace {

using blunted::Vector3;

constexpr float kHalfLength = 52.5f;  // gametypes pitchHalfW
constexpr float kBoxLine = 36.14f;    // F6
constexpr float kBoxHalfWidth = 20.12f;  // F7

pes::TeamConceptConstants Synthetic() {
  pes::TeamConceptConstants k{};
  k.defaultSupportRange = 5, k.defaultDefensiveLine = 5, k.defaultCompactness = 5;
  k.enabled.fill(true);
  k.endTestAfter_s = 0.1f;
  k.chaseWindow_s = 3, k.chaseThirdMargin_m = 10, k.chaseWideMargin_m = 2;
  k.chaseRangeSq_m2 = 100, k.chaseGoalSideMax_deg = 100, k.chaseInsideMin_deg = 90;
  k.chaseSecondRangeSq_m2 = 400, k.chaseSecondWidthSlack_m = 15;
  k.chaseTargetDistance_m = 8, k.chaseTargetTurn_deg = 160;
  k.counterWindow_s = 3, k.counterKickoffGuard_s = 6, k.counterFacingMax_deg = 55;
  k.counterBallMaxDepth_m = -12, k.counterLineMargin_m = -10, k.counterAbortLineMargin_m = -14;
  k.counterAbortWideMargin_m = 4, k.counterMaxGoalSide = 2;
  k.runnerUnmarkedSq_m2 = 42.25f, k.runnerChannelShare = 0.5f, k.runnerChannelSlack_m = 11;
  k.runnerCentreFactor = 1.2f, k.runnerAreaFactor = 0.7f, k.runnerWideLane_m = 17;
  k.supportAhead_m = 15, k.supportSide_m = 12, k.supportTurn_deg = 22, k.supportMinAhead_m = 8;
  k.supportHoldBonus_m = -5;
  k.counterRunGrace_s = 2, k.counterMaxAge_s = 6, k.counterRunnerFarSq = 1369, k.counterRunnerMaxDx_m = 30;
  return k;
}

TEST(PesTeamSheet, GatesFollowTheToggles) {
  pes::TeamSheet s{};
  s.toggles[pes::kDefensiveStyle] = 0;
  s.toggles[pes::kAttackingStyle] = 0;
  EXPECT_TRUE(pes::ConceptGate(pes::kConceptTransitionChase, s));
  EXPECT_TRUE(pes::ConceptGate(pes::kConceptSideCounter, s));
  s.toggles[pes::kDefensiveStyle] = 1;  // All-out Defence: no fore-check family
  s.toggles[pes::kAttackingStyle] = 1;  // Possession Game: no counters
  EXPECT_FALSE(pes::ConceptGate(pes::kConceptTransitionChase, s));
  EXPECT_FALSE(pes::ConceptGate(pes::kConceptSideCounter, s));
}

// A wide carrier in our attacking third, the chaser goal-side and inside him,
// one wide team-mate within reach.
pes::TransitionChaseInput ChaseScene() {
  pes::TransitionChaseInput in;
  in.inPlay = true;
  in.sincePossessionChange_s = 1.0f;
  in.dir = 1;
  in.carrier = Vector3(30, 22, 0);
  in.chaser = 0;
  in.men = {{Vector3(26, 19, 0), false, false},  // chaser: behind (toward +x goal) and inside
            {Vector3(24, 25, 0), false, true},   // widest
            {Vector3(28, 10, 0), false, true}};
  return in;
}

TEST(PesTransitionChase, StartsOnAWideCarrierAndPicksTheWidestSecondMan) {
  const auto pick = pes::TransitionChaseStart(Synthetic(), ChaseScene());
  ASSERT_TRUE(pick);
  EXPECT_EQ(pick->chaser, 0);
  EXPECT_EQ(pick->second, 1);
}

TEST(PesTransitionChase, EveryStartRuleVetoes) {
  const auto k = Synthetic();
  auto in = ChaseScene();
  in.sincePossessionChange_s = 3.5f;
  EXPECT_FALSE(pes::TransitionChaseStart(k, in));
  in = ChaseScene();
  in.carrier.coords[1] = 5;  // central
  EXPECT_FALSE(pes::TransitionChaseStart(k, in));
  in = ChaseScene();
  in.carrier.coords[0] = kBoxLine - 12;  // short of our attacking third
  EXPECT_FALSE(pes::TransitionChaseStart(k, in));
  in = ChaseScene();
  in.carrierKicking = true;
  EXPECT_FALSE(pes::TransitionChaseStart(k, in));
  in = ChaseScene();
  in.men[0].pos = Vector3(26, 24, 0);  // outside the carrier
  EXPECT_FALSE(pes::TransitionChaseStart(k, in));
  in = ChaseScene();
  in.men[0].pos = Vector3(34, 20, 0);  // not goal-side (the opponents attack -x)
  EXPECT_FALSE(pes::TransitionChaseStart(k, in));
  in = ChaseScene();
  in.men[1].available = in.men[2].available = false;
  EXPECT_FALSE(pes::TransitionChaseStart(k, in));
}

TEST(PesTransitionChase, TrapPointIsBehindTheChaserOnTheTouchlineSide) {
  const auto k = Synthetic();
  const Vector3 p = pes::TransitionChaseTarget(k, Vector3(26, 19, 0), Vector3(30, 22, 0), 1);
  const float rad = 20.0f * 3.14159265f / 180.0f;  // 180 - 160
  EXPECT_NEAR(p.coords[0], 26 - 8 * std::cos(rad), 1e-3);
  EXPECT_NEAR(p.coords[1], 19 + 8 * std::sin(rad), 1e-3);
  // Mirrored team and touchline: still behind, still outward.
  const Vector3 q = pes::TransitionChaseTarget(k, Vector3(-26, -19, 0), Vector3(-30, -22, 0), -1);
  EXPECT_NEAR(q.coords[0], -26 + 8 * std::cos(rad), 1e-3);
  EXPECT_NEAR(q.coords[1], -19 - 8 * std::sin(rad), 1e-3);
}

// Ball won in our half, two opponents goal-side, a free winger.
pes::SideCounterInput CounterScene() {
  pes::SideCounterInput in;
  in.attackAllowed = true;
  in.sincePossessionChange_s = 0.5f;
  in.sinceKickoff_s = 1e9f;
  in.dir = 1;
  in.ball = Vector3(-20, 0, 0);
  in.holder = 0;
  in.holderHeading_deg = 90;  // facing +x
  in.oppLineX = 10;
  in.opponents = {Vector3(12, 5, 0), Vector3(14, -6, 0), Vector3(-25, 3, 0)};
  in.men = {{Vector3(-20, 0, 0), false, true, 100},
            {Vector3(-5, 25, 0), true, true, 400},    // winger, free
            {Vector3(-15, 8, 0), false, true, 100},
            {Vector3(-10, -12, 0), true, true, 16}};  // marked
  return in;
}

TEST(PesSideCounter, StartsAndPicksTheFreeWideMan) {
  const auto runner = pes::SideCounterStart(Synthetic(), CounterScene());
  ASSERT_TRUE(runner);
  EXPECT_EQ(*runner, 1);
}

TEST(PesSideCounter, EveryStartRuleVetoes) {
  const auto k = Synthetic();
  auto in = CounterScene();
  in.attackAllowed = false;
  EXPECT_FALSE(pes::SideCounterStart(k, in));
  in = CounterScene();
  in.sinceKickoff_s = 2;
  EXPECT_FALSE(pes::SideCounterStart(k, in));
  in = CounterScene();
  in.holderHeading_deg = 160;
  EXPECT_FALSE(pes::SideCounterStart(k, in));
  in = CounterScene();
  in.ball.coords[0] = -5;  // not 12 m short of halfway
  EXPECT_FALSE(pes::SideCounterStart(k, in));
  in = CounterScene();
  in.opponents.push_back(Vector3(15, -2, 0));  // a third man goal-side
  EXPECT_FALSE(pes::SideCounterStart(k, in));
  in = CounterScene();
  in.keeperHolds = true;
  EXPECT_FALSE(pes::SideCounterStart(k, in));
}

TEST(PesSideCounter, RunTargetIsTheChannelAtTheOffsideLine) {
  const auto k = Synthetic();
  const Vector3 t = pes::SideCounterRunTarget(k, Vector3(-5, 30, 0), 10);
  EXPECT_FLOAT_EQ(t.coords[0], 10);
  EXPECT_FLOAT_EQ(t.coords[1], 30);  // already wider than the channel
  const Vector3 u = pes::SideCounterRunTarget(k, Vector3(-5, -3, 0), 10);
  EXPECT_FLOAT_EQ(u.coords[1], -(kBoxHalfWidth + 0.5f * (34.0f - kBoxHalfWidth)));
}

TEST(PesSideCounter, SupportersTakeAheadAndBothSides) {
  const auto k = Synthetic();
  auto in = CounterScene();
  in.men.push_back({Vector3(-8, 0, 0), false, true, 100});    // 12 m ahead of the ball
  in.men.push_back({Vector3(-18, -12, 0), false, true, 100}); // low side
  const auto s = pes::SideCounterSupporters(k, in, 1, {-1, -1, -1});
  EXPECT_EQ(s.men[0], 4);
  EXPECT_NEAR(s.points[0].coords[0], -20 + 15, 1e-3);
  EXPECT_EQ(s.men[1], 5);  // point 1 turns from 180 deg: the low side
  EXPECT_EQ(s.men[2], 2);
}

TEST(PesSideCounter, EndsWhenTheRunIsOverOrStale) {
  const auto k = Synthetic();
  const Vector3 ball(-20, 0, 0);
  EXPECT_FALSE(pes::SideCounterEnd(k, true, true, 1.0f, ball, Vector3(0, 20, 0)));
  EXPECT_TRUE(pes::SideCounterEnd(k, false, true, 1.0f, ball, Vector3(0, 20, 0)));
  EXPECT_TRUE(pes::SideCounterEnd(k, true, false, 1.0f, ball, Vector3(0, 20, 0)));
  EXPECT_TRUE(pes::SideCounterEnd(k, true, true, 2.5f, ball, Vector3(15, 20, 0)));  // 35 m ahead
  EXPECT_FALSE(pes::SideCounterEnd(k, true, true, 2.5f, ball, Vector3(5, 20, 0)));
  EXPECT_TRUE(pes::SideCounterEnd(k, true, true, 6.5f, ball, Vector3(5, 20, 0)));
}

TEST(PesTeamConceptsInstall, LoadsRelations) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT unset";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto k = pes::LoadTeamConceptConstants(*exe, vfs, &err);
  ASSERT_TRUE(k) << err;
  for (int v : {k->defaultSupportRange, k->defaultDefensiveLine, k->defaultCompactness}) {
    EXPECT_GE(v, 1);
    EXPECT_LE(v, 10);
  }
  EXPECT_GT(k->chaseSecondRangeSq_m2, k->chaseRangeSq_m2);
  EXPECT_LT(k->chaseInsideMin_deg, k->chaseGoalSideMax_deg);
  EXPECT_GT(k->counterKickoffGuard_s, k->counterWindow_s);
  EXPECT_GT(k->counterMaxAge_s, k->counterRunGrace_s);
  EXPECT_LT(k->counterAbortLineMargin_m, k->counterLineMargin_m);
  EXPECT_GT(k->supportAhead_m, k->supportSide_m);
  EXPECT_LT(k->runnerAreaFactor, 1.0f);
  EXPECT_GT(k->runnerCentreFactor, 1.0f);
  // The concepts this port runs are enabled in whatever the VFS resolves.
  EXPECT_TRUE(k->enabled[pes::kConceptTransitionChase]);
  (void)kHalfLength;
}

}  // namespace

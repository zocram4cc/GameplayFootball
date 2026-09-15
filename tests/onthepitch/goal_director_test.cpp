// PES's goal walk as the director drives it: run, celebrate, hug, approach,
// finish - and the situations that replace all of it.

#include <gtest/gtest.h>

#include "onthepitch/goaldirector.hpp"

using namespace GoalDirector;

namespace {

// Shaped like the export: one run of each length, two celebrations, both hugs,
// a fence approach that only big occasions admit, a finish, the own-goal set.
const char* kText = R"(# PES cutscene director
state GOAL_RUN_30_BANZAI phase run
  shot weight 1043.1 flags 00000001 on 1 rec 8000e02
    follow goal_2018_run_cmnCam_B_M dur 180 near 0.5 far 400
    actors goal_2018_run_30_banzai
  shot weight 1366.2 flags 00000001 on 1 rec 22010e00
    track goal_2018_run_30_banzai_Z_fromR
    actors goal_2018_run_30_banzai
state GOAL_RUN_40_PLANE phase run
  shot weight 1366.2 flags 00000001 on 1 rec 22000e00
    track goal_2018_run_40_plane_Z_fromL
    actors goal_2018_run_40_plane
state GOAL_CELEBRATE_0032 phase celebrate
  shot weight 1586.4 flags 00000001 on 1 rec 3000f01
    track goal_cmnCam_outM00
    track goal_celebrate_0032_base
    actors goal_celebrate_0032_base
state GOAL_CELEBRATE_0057 phase celebrate
  shot weight 1200.0 flags 00000007 on 3 rec 3000f01
    track goal_celebrate_0057_base
    actors goal_celebrate_0057_base
state GOAL_HUGA_LV2 phase hug
  shot weight 900.0 flags 00000003 on 3 rec 100
    track goal_celebrate_0300
    actors goal_celebrate_0300
state GOAL_HUGA_LV3 phase hug
  shot weight 900.0 flags 0000001f on 3 rec 100
    track goal_celebrate_0301
    actors goal_celebrate_0301
state GOAL_A_BEHINDGOAL01 phase approach
  shot weight 700.0 flags 0020001f on 3 rec 100
    track goal_A_behindGoal01
    actors goal_A_behindGoal01
state GOAL_C_FINISH_SUCCESS phase finish
  shot weight 36.3 flags 0000003f on 1 rec 100
    track goal_C_coop_success_002
    actors goal_C_coop_success_002
state GOAL_S_OWNGOAL_GK phase situation
  shot weight 30.1 flags 0020103e on 1 rec 100
    track goal_S_owngoal_02_GK
    actors goal_S_owngoal_02_GK
)";

std::vector<std::string> Names(const std::vector<Beat>& beats) {
  std::vector<std::string> out;
  for (const Beat& b : beats) out.push_back(b.state->name);
  return out;
}

}  // namespace

TEST(GoalDirector, ParsesStatesShotsAndLayers) {
  Director d = Parse(kText);
  ASSERT_EQ(d.states.size(), 9u);
  const State* run = d.Find("GOAL_RUN_30_BANZAI");
  ASSERT_TRUE(run);
  EXPECT_EQ(run->phase, Phase::Run);
  ASSERT_EQ(run->shots.size(), 2u);
  EXPECT_FLOAT_EQ(run->shots[1].weight, 1366.2f);
  EXPECT_EQ(run->shots[1].flags, 1u);
  EXPECT_EQ(run->shots[1].record, 0x22010e00u);
  EXPECT_EQ(run->shots[1].Track(), "goal_2018_run_30_banzai_Z_fromR");
  EXPECT_EQ(run->shots[1].Actors(), "goal_2018_run_30_banzai");
  const Layer* follow = run->shots[0].FollowCamera();
  ASSERT_TRUE(follow);
  EXPECT_FLOAT_EQ(follow->follow.durationFrames, 180.0f);
  EXPECT_FLOAT_EQ(follow->follow.nearPlane, 0.5f);
  EXPECT_FLOAT_EQ(follow->follow.farPlane, 400.0f);
}

TEST(GoalDirector, AnOrdinaryGoalRunsCelebratesAndFinishes) {
  Director d = Parse(kText);
  Situation s;
  s.runDistance = 30.0f;
  s.teammatesNear = 0;
  s.seed = 1;  // not a multiple of 3: no approach for a lone scorer
  std::vector<Beat> beats = Plan(d, s);
  EXPECT_EQ(Names(beats), (std::vector<std::string>{"GOAL_RUN_30_BANZAI", "GOAL_CELEBRATE_0032",
                                                    "GOAL_C_FINISH_SUCCESS"}));
}

TEST(GoalDirector, TeammatesBringTheHugAndTheBigOccasionTheFence) {
  Director d = Parse(kText);
  Situation s;
  s.runDistance = 40.0f;
  s.teammatesNear = 5;
  s.seed = 2;
  std::vector<Beat> beats = Plan(d, s);
  EXPECT_EQ(Names(beats), (std::vector<std::string>{"GOAL_RUN_40_PLANE", "GOAL_CELEBRATE_0032",
                                                    "GOAL_HUGA_LV3", "GOAL_A_BEHINDGOAL01",
                                                    "GOAL_C_FINISH_SUCCESS"}));
}

TEST(GoalDirector, TheScorersOwnCelebrationIsHonoured) {
  Director d = Parse(kText);
  Situation s;
  s.celebration = "GOAL_CELEBRATE_0057";
  s.teammatesNear = 1;
  s.seed = 4;
  std::vector<Beat> beats = Plan(d, s);
  ASSERT_GE(beats.size(), 2u);
  EXPECT_EQ(beats[1].state->name, "GOAL_CELEBRATE_0057");
}

TEST(GoalDirector, AnOwnGoalIsOnlyTheOwnGoalScene) {
  Director d = Parse(kText);
  Situation s;
  s.ownGoal = true;
  EXPECT_EQ(Names(Plan(d, s)), (std::vector<std::string>{"GOAL_S_OWNGOAL_GK"}));
}

TEST(GoalDirector, AQuadrantPinnedShotWaitsForItsQuadrant) {
  Director d = Parse(kText);
  const State* run = d.Find("GOAL_RUN_30_BANZAI");
  Situation s;
  s.quadrant = 1;  // rec 0x22010e00: byte 2 == 1
  const Shot* shot = ChooseShot(*run, s);
  ASSERT_TRUE(shot);
  EXPECT_EQ(shot->Track(), "goal_2018_run_30_banzai_Z_fromR");
  s.quadrant = 3;  // only the anywhere row (quadrant 0) is left
  shot = ChooseShot(*run, s);
  ASSERT_TRUE(shot);
  EXPECT_TRUE(shot->FollowCamera());
}
TEST(GoalDirector, NoRoomMeansNoRun) {
  Director d = Parse(kText);
  Situation s;
  s.runDistance = 5.0f;  // at the byline: straight to the celebration
  s.teammatesNear = 0;
  s.seed = 1;
  EXPECT_EQ(Names(Plan(d, s)), (std::vector<std::string>{"GOAL_CELEBRATE_0032",
                                                          "GOAL_C_FINISH_SUCCESS"}));
}

TEST(GoalDirector, AnAuthoredCameraBeatsEveryFollowRow) {
  Director d = Parse(kText);
  // GOAL_RUN_30_BANZAI's first row is follow-only, its second has a track:
  // every seed must come back with the track.
  const State* run = d.Find("GOAL_RUN_30_BANZAI");
  for (int seed = 0; seed < 8; seed++) {
    Situation s;
    s.seed = seed;
    s.quadrant = 1;
    const Shot* shot = ChooseShot(*run, s);
    ASSERT_TRUE(shot);
    EXPECT_EQ(shot->Track(), "goal_2018_run_30_banzai_Z_fromR");
  }
}

TEST(GoalDirector, HugsNeedAnOccasionTheirMaskAdmits) {
  Director d = Parse(kText);
  const State* lv3 = d.Find("GOAL_HUGA_LV3");
  Situation s;
  s.teammatesNear = 1;  // occasion 0x2, mask 0x1f admits it
  EXPECT_TRUE(ChooseShot(*lv3, s));
  const State* fence = d.Find("GOAL_A_BEHINDGOAL01");
  s.teammatesNear = 0;  // occasion 0x1 - and 0x1f has that bit too: PES's ladders include 1
  EXPECT_TRUE(ChooseShot(*fence, s));
}

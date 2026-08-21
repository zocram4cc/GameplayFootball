// Cutscenes that run as several shots rather than one.
//
// The end-of-match camerawork - 140 tracks of crowd, celebration, dejection, the walk
// to the stand, the team photo - was imported and never played: GameOver asked for one
// eight-second "result" shot and nothing else. And a goal played the scorer's own
// celebration and stopped, though PES ships the shots that follow it.

#include <set>
#include <string>

#include <gtest/gtest.h>

#include "onthepitch/cutscenesequence.hpp"

namespace {

// A pool test that says yes to everything named here and no to anything else.
CutsceneSequence::PoolTest Installed(const std::set<std::string>& pools) {
  return [pools](const std::string& pool) { return pools.count(pool) > 0; };
}

CutsceneSequence::PoolTest All() {
  return [](const std::string&) { return true; };
}

CutsceneSequence::PoolTest None() {
  return [](const std::string&) { return false; };
}

bool Mentions(const std::vector<CutsceneSequence::Stage>& stages, const std::string& pool) {
  for (unsigned int i = 0; i < stages.size(); i++) {
    if (stages.at(i).pool == pool)
      return true;
  }
  return false;
}

}  // namespace

TEST(ClosingSequence, TheWinnersCelebrateAndTheLosersDoNot) {
  const std::vector<CutsceneSequence::Stage> won =
      CutsceneSequence::ClosingStages(2, "st011", All());
  const std::vector<CutsceneSequence::Stage> lost =
      CutsceneSequence::ClosingStages(-2, "st011", All());
  EXPECT_TRUE(Mentions(won, "end/joy")) << "the winners do not celebrate";
  EXPECT_FALSE(Mentions(won, "end/sad"));
  EXPECT_TRUE(Mentions(lost, "end/sad")) << "the losers are not shown losing";
  EXPECT_FALSE(Mentions(lost, "end/joy"));
}

TEST(ClosingSequence, ADrawIsNeitherCelebratedNorMourned) {
  const std::vector<CutsceneSequence::Stage> drawn =
      CutsceneSequence::ClosingStages(0, "st011", All());
  EXPECT_FALSE(Mentions(drawn, "end/joy"));
  EXPECT_FALSE(Mentions(drawn, "end/sad"));
  EXPECT_FALSE(drawn.empty()) << "a draw still gets a closing sequence";
}

TEST(ClosingSequence, TheStadiumsOwnCrowdIsShownWhenItWasImported) {
  const std::vector<CutsceneSequence::Stage> stages =
      CutsceneSequence::ClosingStages(1, "st011", All());
  EXPECT_TRUE(Mentions(stages, "end/audience_st011"))
      << "another ground's stands would be showing";
}

TEST(ClosingSequence, AGroundWithNoCrowdShotsSkipsThemRatherThanShowingAnothers) {
  const std::vector<CutsceneSequence::Stage> stages = CutsceneSequence::ClosingStages(
      1, "st099", Installed({"end", "end/joy", "end/greet", "end/photo"}));
  EXPECT_FALSE(Mentions(stages, "end/audience_st099"));
  EXPECT_FALSE(Mentions(stages, "end/audience_st011"));
  EXPECT_FALSE(stages.empty());
}

TEST(ClosingSequence, NothingInstalledMeansNoStagesRatherThanEmptyShots) {
  EXPECT_TRUE(CutsceneSequence::ClosingStages(1, "st011", None()).empty());
}

TEST(ClosingSequence, EveryStageNamesAPoolAndAPositiveLength) {
  const std::vector<CutsceneSequence::Stage> stages =
      CutsceneSequence::ClosingStages(3, "st011", All());
  ASSERT_FALSE(stages.empty());
  for (unsigned int i = 0; i < stages.size(); i++) {
    EXPECT_FALSE(stages.at(i).pool.empty()) << "stage " << i;
    EXPECT_GT(stages.at(i).seconds, 0.0f) << "stage " << i;
  }
}

TEST(ClosingSequence, TheCrowdComesFirstAndThePhotoLast) {
  // the broadcast holds on the ground, then the players, then the photo
  const std::vector<CutsceneSequence::Stage> stages =
      CutsceneSequence::ClosingStages(2, "st011", All());
  ASSERT_GE(stages.size(), 3u);
  EXPECT_EQ(stages.front().pool, "end/audience_st011");
  EXPECT_EQ(stages.back().pool, "end/photo");
}

TEST(ClosingSequence, ItRunsLongEnoughToBeACeremonyRatherThanACut) {
  const float total = CutsceneSequence::TotalSeconds(
      CutsceneSequence::ClosingStages(2, "st011", All()));
  EXPECT_GE(total, 15.0f) << "a whole closing sequence in " << total << "s";
  EXPECT_LE(total, 60.0f) << "nobody watches a minute of it: " << total << "s";
}

// Which pool a closing camtrack belongs in: PES exports them flat, with the family in
// the file name, so the loader has to read the name to tell a crowd shot from a
// celebration.

TEST(ClosingPools, ACrowdShotIsKeptWithItsOwnGround) {
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile("end_audience_st011_ha_home.camtrack"),
            "end/audience_st011");
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile("end_audience_st060_ha_away.camtrack"),
            "end/audience_st060");
}

TEST(ClosingPools, CelebrationAndDejectionAreToldApart) {
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile("end_joy_high_2.camtrack"), "end/joy");
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile("end_shareJoy_1_few.camtrack"), "end/joy");
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile("end_lose_sad_3.camtrack"), "end/sad");
}

TEST(ClosingPools, TheWalkToTheStandAndThePhotoAreTheirOwnStages) {
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile("end_greet_audi_2.camtrack"), "end/greet");
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile("end_photo_1_cmn.camtrack"), "end/photo");
}

TEST(ClosingPools, AnUnrecognisedNameClaimsNoFamily) {
  // it stays in the category pool rather than being filed under a guess
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile("end_retire_1.camtrack"), "end/retire");
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile("something_else.camtrack"), "");
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile(""), "");
}

TEST(ClosingPools, TheNamesMatchWhatTheSequenceAsksFor) {
  // the two halves have to agree, or the sequence names pools nothing fills
  EXPECT_EQ(CutsceneSequence::ClosingPoolForFile("end_joy_high_1.camtrack"),
            CutsceneSequence::ClosingStages(2, "st011", [](const std::string& pool) {
              return pool == "end/joy";
            }).front().pool);
}

TEST(CutsceneSequenceTotals, AnEmptySequenceIsNoTime) {
  EXPECT_FLOAT_EQ(CutsceneSequence::TotalSeconds({}), 0.0f);
}

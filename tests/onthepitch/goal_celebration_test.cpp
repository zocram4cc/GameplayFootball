// Which celebration a scorer performs, and which camera films it.
//
// PES ships the two together: goal_2018_run_30_banzai.chor says the scorer plays
// dml_goal_move3_0002.anim, and goal_2018_run_30_banzai_Z_fromL/R.camtrack are the
// angles it shot that performance from. tools/pes21_import/goal_cutscenes.py reads
// that out of PES's own files into celebrations.txt.
//
// It matters because a camera is only right for the celebration it was shot for. The
// engine used to pick a track with `(teamID * 7 + score(0) + score(1) * 3) % count` -
// any camera for any celebration - which is one of the reasons a long-lens shot ended
// up jammed against a scorer's head.

#include <cmath>
#include <gtest/gtest.h>

#include "onthepitch/goalcelebration.hpp"
#include "onthepitch/goalsequence.hpp"

namespace {
const char* kManifest =
    "# a comment\n"
    "celebration goal_2018_run_30\n"
    "clip dml_goal_move3_0001.anim\n"
    "camera goal_2018_run_30_Z_fromL\n"
    "camera goal_2018_run_30_Z_fromR\n"
    "celebration goal_2018_run_30_banzai\n"
    "clip dml_goal_move3_0002.anim\n"
    "var 101\n"
    "camera goal_2018_run_30_banzai_Z_fromL\n"
    "camera goal_2018_run_30_banzai_Z_fromR\n"
    "celebration goal_2018_run_30_plane\n"
    "clip dml_goal_move3_0006.anim\n";
}  // namespace

TEST(GoalCelebration, AManifestIsRead) {
  const auto set = GoalCelebration::Parse(kManifest);
  ASSERT_EQ(set.size(), 3u);
  EXPECT_EQ(set[0].name, "goal_2018_run_30");
  EXPECT_EQ(set[1].clip, "dml_goal_move3_0002.anim");
  EXPECT_EQ(set[1].cameras.size(), 2u);
  EXPECT_EQ(set[1].cameras[0], "goal_2018_run_30_banzai_Z_fromL");
}

// The number both sides agree on: the manifest carries it, install_anims.py stamps the
// clip with it, and the controller asks for it.
TEST(GoalCelebration, AFilmedCelebrationCarriesTheVariableItIsAskedForBy) {
  const auto set = GoalCelebration::Parse(kManifest);
  EXPECT_EQ(set[1].var, 101);
}

TEST(GoalCelebration, AnUnfilmedOneHasNone) {
  const auto set = GoalCelebration::Parse(kManifest);
  EXPECT_EQ(set[2].var, 0);
}

TEST(GoalCelebration, ACelebrationPesNeverFilmedKeepsItsPerformance) {
  const auto set = GoalCelebration::Parse(kManifest);
  EXPECT_EQ(set[2].name, "goal_2018_run_30_plane");
  EXPECT_TRUE(set[2].cameras.empty());
}

TEST(GoalCelebration, NothingIsNoCelebrations) {
  EXPECT_TRUE(GoalCelebration::Parse("").empty());
  EXPECT_TRUE(GoalCelebration::Parse("# only a comment\n").empty());
}

TEST(GoalCelebration, ACelebrationWithoutAClipIsNotOne) {
  // a stanza with no performance would put the scorer in his idle pose
  EXPECT_TRUE(GoalCelebration::Parse("celebration x\ncamera x_Z_fromL\n").empty());
}

// Which one a player performs. A player carries an assignment so the same man
// celebrates the same way twice - that is what makes it his - and anyone without one
// draws from the filmed set by a seed, so a match is varied rather than random.
TEST(GoalCelebration, APlayerWithAnAssignmentPerformsIt) {
  const auto set = GoalCelebration::Parse(kManifest);
  const int chosen = GoalCelebration::Choose(set, "goal_2018_run_30_banzai", 0);
  ASSERT_GE(chosen, 0);
  EXPECT_EQ(set[chosen].name, "goal_2018_run_30_banzai");
}

TEST(GoalCelebration, AnAssignmentNobodyShipsFallsBackRatherThanFailing) {
  const auto set = GoalCelebration::Parse(kManifest);
  const int chosen = GoalCelebration::Choose(set, "goal_2018_run_30_moonwalk", 0);
  ASSERT_GE(chosen, 0);
  EXPECT_FALSE(set[chosen].name.empty());
}

TEST(GoalCelebration, WithoutAnAssignmentTheSeedDecides) {
  const auto set = GoalCelebration::Parse(kManifest);
  EXPECT_EQ(GoalCelebration::Choose(set, "", 4), GoalCelebration::Choose(set, "", 4));
  // and different seeds do not all land on the same one
  bool differs = false;
  for (int seed = 0; seed < 8; ++seed)
    if (GoalCelebration::Choose(set, "", seed) != GoalCelebration::Choose(set, "", 0))
      differs = true;
  EXPECT_TRUE(differs);
}

TEST(GoalCelebration, TheUnfilmedOnesAreNotDrawnByASeed) {
  // picked at random it would be a goal with no camerawork at all; it is only
  // performed when a player is assigned it by name
  const auto set = GoalCelebration::Parse(kManifest);
  for (int seed = 0; seed < 32; ++seed) {
    const int chosen = GoalCelebration::Choose(set, "", seed);
    ASSERT_GE(chosen, 0);
    EXPECT_FALSE(set[chosen].cameras.empty()) << "seed " << seed;
  }
}

TEST(GoalCelebration, ChoosingFromNothingIsNoChoice) {
  EXPECT_EQ(GoalCelebration::Choose({}, "", 0), -1);
}

// Which angle. PES shoots most celebrations from both sides; the one to use is the
// one looking back up the pitch the scorer is running into, so the shot holds the
// crowd behind him rather than the empty half he came from.
TEST(GoalCelebration, TheAngleFollowsTheSideAttacked) {
  const auto set = GoalCelebration::Parse(kManifest);
  const auto& banzai = set[1];
  EXPECT_EQ(GoalCelebration::PickCamera(banzai, -1, 0), "goal_2018_run_30_banzai_Z_fromL");
  EXPECT_EQ(GoalCelebration::PickCamera(banzai, 1, 0), "goal_2018_run_30_banzai_Z_fromR");
}

TEST(GoalCelebration, OneAngleServesBothSides) {
  GoalCelebration::Celebration one;
  one.name = "x";
  one.clip = "x.anim";
  one.cameras.push_back("x_Z_fromL");
  EXPECT_EQ(GoalCelebration::PickCamera(one, 1, 0), "x_Z_fromL");
  EXPECT_EQ(GoalCelebration::PickCamera(one, -1, 0), "x_Z_fromL");
}

TEST(GoalCelebration, NoAngleIsNoCamera) {
  GoalCelebration::Celebration bare;
  bare.name = "x";
  bare.clip = "x.anim";
  EXPECT_TRUE(GoalCelebration::PickCamera(bare, 1, 0).empty());
}

// The two halves of a celebration. PES authors an intro and a loop the scorer holds
// until the celebration is over, and the engine asks for a special animation by
// specialvar1/specialvar2 - so the loop is the same mood with specialvar2 raised by
// ten (tools/pes21_import/install_anims.py, LOOP_VAR_OFFSET).
//
// Measured over the 40 imported goal intros: 330 to 1850 ms, median 1220. The hold is
// the longest of them, so no intro is cut off; a short one holds its last pose for a
// moment first, which is the price of not plumbing per-clip lengths through the
// controller.
TEST(GoalCelebration, ItOpensWithTheIntro) {
  EXPECT_EQ(GoalCelebration::Phase(0, 1900), GoalCelebration::e_Intro);
  EXPECT_EQ(GoalCelebration::Phase(1899, 1900), GoalCelebration::e_Intro);
}

TEST(GoalCelebration, ThenItHoldsTheLoop) {
  EXPECT_EQ(GoalCelebration::Phase(1900, 1900), GoalCelebration::e_Loop);
  EXPECT_EQ(GoalCelebration::Phase(9000, 1900), GoalCelebration::e_Loop);
}

TEST(GoalCelebration, WithNoIntroToPlayItIsAllLoop) {
  EXPECT_EQ(GoalCelebration::Phase(0, 0), GoalCelebration::e_Loop);
}

TEST(GoalCelebration, TheLoopIsTheSameMoodAskedForDifferently) {
  EXPECT_EQ(GoalCelebration::LoopVariable(1), 11);
  EXPECT_EQ(GoalCelebration::LoopVariable(2), 12);
}

// How long a celebration actually is.
//
// The intro was held for a flat 1900 ms - the longest of the 40 imported intros - and
// the loop then ran until an unrelated nine-second timer cut it off. So a short intro
// held its last pose, and a loop either got cut partway or, once it had finished,
// left the scorer running in place until the timer expired. The clips know their own
// lengths; the engine reads an .anim at 10 ms a frame.

TEST(CelebrationClips, AFrameIsTenMilliseconds) {
  EXPECT_EQ(GoalCelebration::ClipLength_ms(100), 1000u);
  EXPECT_EQ(GoalCelebration::ClipLength_ms(0), 0u);
}

TEST(CelebrationClips, AnIntroIsHeldForItsOwnLength) {
  // the imported intros run 330 to 1850 ms
  EXPECT_EQ(GoalCelebration::IntroHold_ms(33), 330u);
  EXPECT_EQ(GoalCelebration::IntroHold_ms(185), 1850u);
}

TEST(CelebrationClips, AnUnknownIntroFallsBackToTheOldFlatHold) {
  EXPECT_EQ(GoalCelebration::IntroHold_ms(0), GoalCelebration::kIntroHold_ms);
  EXPECT_EQ(GoalCelebration::IntroHold_ms(-5), GoalCelebration::kIntroHold_ms);
}

TEST(CelebrationClips, TheTotalIsTheIntroPlusTheLoop) {
  EXPECT_EQ(GoalCelebration::CelebrationTotal_ms(120, 400), 1200u + 4000u);
}

TEST(CelebrationClips, AnUnknownLoopStillGivesTheIntroTimeToPlay) {
  const unsigned long total = GoalCelebration::CelebrationTotal_ms(120, 0);
  EXPECT_GE(total, 1200u) << "the intro has to fit in whatever is returned";
}

TEST(CelebrationClips, TheLongestClipsStillFitTheSchedule) {
  // 1000 frames is the longest imported celebration; two of them chained is the
  // worst case the buffer is sized for
  const unsigned long total = GoalCelebration::CelebrationTotal_ms(1000, 1000);
  EXPECT_LE(total, GoalSequence::kLongestCelebration_ms)
      << "a celebration longer than this outruns the replay buffer";
}

// How long the players actually perform.
//
// The controller issued the celebration command for a flat slice of the stoppage -
// "more than 2000 ms and less than 4000 ms since the whistle" - while the clip on
// screen runs as long as it runs: the imported performances reach 10 s and the median
// is 2.7 s. A 5.2 s celebration was therefore cut at 4 s, dropping the scorer out of
// his pose and back into ordinary play while the camera was still on him.

TEST(CelebrationWindow, NobodyReactsBeforeTheWhistleHasSettled) {
  EXPECT_FALSE(GoalCelebration::IsPerforming(0, 5200));
  EXPECT_FALSE(GoalCelebration::IsPerforming(GoalCelebration::kReactionDelay_ms - 1, 5200));
  EXPECT_TRUE(GoalCelebration::IsPerforming(GoalCelebration::kReactionDelay_ms, 5200));
}

TEST(CelebrationWindow, ALongCelebrationRunsAllTheWayThrough) {
  // 4000 ms was the old ceiling; a 5.2 s clip has to outlast it
  EXPECT_TRUE(GoalCelebration::IsPerforming(4500, 5200));
  EXPECT_TRUE(GoalCelebration::IsPerforming(5199, 5200));
  EXPECT_FALSE(GoalCelebration::IsPerforming(5200, 5200));
}

TEST(CelebrationWindow, AShortClipStillGetsAMomentRatherThanAFlicker) {
  // the shortest imported clips are 400 ms, which would otherwise be over before the
  // reaction delay had passed
  EXPECT_TRUE(GoalCelebration::IsPerforming(GoalCelebration::kReactionDelay_ms + 100, 400));
  EXPECT_FALSE(GoalCelebration::IsPerforming(60000, 400));
}

TEST(CelebrationWindow, AnUnknownLengthFallsBackRatherThanEndingAtOnce) {
  EXPECT_TRUE(GoalCelebration::IsPerforming(GoalCelebration::kReactionDelay_ms + 500, 0));
}

TEST(CelebrationWindow, ItNeverOutlastsTheGoalSequence) {
  // the restart is scheduled off the same measurement, so performing past it would
  // have players posing as the game kicks off again
  EXPECT_FALSE(GoalCelebration::IsPerforming(30000, 5200));
}

// Whose celebration this is.
//
// The draw was seeded from the score and the scoring side - so at 0-0 every scorer in
// the match gave the same celebration, and the variety in 40 imported performances
// went unseen. A man's own celebration should be his: stable across matches, and not
// his team-mate's.

namespace {

std::vector<GoalCelebration::Celebration> FilmedSet(int howMany) {
  std::vector<GoalCelebration::Celebration> set;
  for (int i = 0; i < howMany; i++) {
    GoalCelebration::Celebration one;
    one.name = "celebrate_" + std::to_string(i);
    one.var = 100 + i;
    one.cameras.push_back("cam_" + std::to_string(i));
    set.push_back(one);
  }
  return set;
}

}  // namespace

TEST(CelebrationIdentity, TheSameManCelebratesTheSameWayTwice) {
  // nothing but his own id decides it, so the score cannot move it
  const std::vector<GoalCelebration::Celebration> set = FilmedSet(40);
  const int first = GoalCelebration::Choose(set, "", GoalCelebration::SeedFor(452));
  const int again = GoalCelebration::Choose(set, "", GoalCelebration::SeedFor(452));
  EXPECT_EQ(first, again);
}

TEST(CelebrationIdentity, DifferentMenGetDifferentCelebrations) {
  const std::vector<GoalCelebration::Celebration> set = FilmedSet(40);
  std::set<int> chosen;
  for (int id = 450; id < 473; id++)
    chosen.insert(GoalCelebration::Choose(set, "", GoalCelebration::SeedFor(id)));
  // a squad of 23 drawing from 40 performances should not collapse onto a handful
  EXPECT_GE(chosen.size(), 12u) << "only " << chosen.size() << " celebrations across a squad";
}

TEST(CelebrationIdentity, TheWholeSetGetsUsedAcrossALeague) {
  const std::vector<GoalCelebration::Celebration> set = FilmedSet(40);
  std::set<int> chosen;
  for (int id = 1; id < 500; id++)
    chosen.insert(GoalCelebration::Choose(set, "", GoalCelebration::SeedFor(id)));
  EXPECT_EQ(chosen.size(), set.size()) << "some performances are never drawn";
}

TEST(CelebrationIdentity, AnAssignmentStillWins) {
  const std::vector<GoalCelebration::Celebration> set = FilmedSet(40);
  EXPECT_EQ(GoalCelebration::Choose(set, "celebrate_7", GoalCelebration::SeedFor(452)), 7);
}

TEST(CelebrationIdentity, APlayerWithNoDatabaseIdStillCelebrates) {
  const std::vector<GoalCelebration::Celebration> set = FilmedSet(40);
  EXPECT_GE(GoalCelebration::Choose(set, "", GoalCelebration::SeedFor(0)), 0);
}

// Where the scorer runs, and when he stops running to perform.

namespace {
constexpr float kHalfW = 55.0f, kHalfH = 34.0f;
}

TEST(CelebrationRun, HeStopsShortOfTheCornerFlag) {
  float x = 0.0f, y = 0.0f;
  GoalCelebration::RunTarget(0.0f, 0.0f, 1, 1, kHalfW, kHalfH, &x, &y);
  EXPECT_LT(x, kHalfW - GoalCelebration::kGoalLineInset_m + 0.01f);
  EXPECT_LT(y, kHalfH - GoalCelebration::kTouchlineInset_m + 0.01f);
}

TEST(CelebrationRun, TheRunIsCapped) {
  float x = 0.0f, y = 0.0f;
  // from the far corner, the near one is over 100 m away
  GoalCelebration::RunTarget(-50.0f, -30.0f, 1, 1, kHalfW, kHalfH, &x, &y);
  const float dx = x + 50.0f, dy = y + 30.0f;
  EXPECT_NEAR(std::sqrt(dx * dx + dy * dy), GoalCelebration::kMaxRun_m, 0.1f);
}

TEST(CelebrationRun, HeRunsTowardsTheHalfHeScoredIn) {
  float x = 0.0f, y = 0.0f;
  GoalCelebration::RunTarget(0.0f, 0.0f, 1, 1, kHalfW, kHalfH, &x, &y);
  EXPECT_GT(x, 0.0f);
  GoalCelebration::RunTarget(0.0f, 0.0f, -1, -1, kHalfW, kHalfH, &x, &y);
  EXPECT_LT(x, 0.0f);
  EXPECT_LT(y, 0.0f);
}

TEST(CelebrationRun, AScorerAlreadyThereDoesNotRunPastIt) {
  float x = 0.0f, y = 0.0f;
  GoalCelebration::RunTarget(48.0f, 30.0f, 1, 1, kHalfW, kHalfH, &x, &y);
  EXPECT_LE(x, kHalfW - GoalCelebration::kGoalLineInset_m + 0.01f);
  EXPECT_LE(y, kHalfH - GoalCelebration::kTouchlineInset_m + 0.01f);
}

TEST(CelebrationRun, TheTargetStaysOnThePitch) {
  float x = 0.0f, y = 0.0f;
  for (int side = -1; side <= 1; side += 2) {
    for (int near = -1; near <= 1; near += 2) {
      GoalCelebration::RunTarget(0.0f, 0.0f, side, near, kHalfW, kHalfH, &x, &y);
      EXPECT_LT(std::fabs(x), kHalfW);
      EXPECT_LT(std::fabs(y), kHalfH);
    }
  }
}

TEST(CelebrationArrival, HePerformsWhenHeGetsThere) {
  EXPECT_TRUE(GoalCelebration::HasArrived(1.0f, 500));
  EXPECT_FALSE(GoalCelebration::HasArrived(9.0f, 500));
}

TEST(CelebrationArrival, ABlockedScorerStillCelebrates) {
  EXPECT_TRUE(GoalCelebration::HasArrived(30.0f, GoalCelebration::kApproachCap_ms));
}

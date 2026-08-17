// Tests for AI vs AI manager mode ("coach mode") from TECHNICAL_ROADMAP.md
// section 3B: both teams are played by the AI while humans control tactics,
// lineups and substitutions.

#include <gtest/gtest.h>

#include "onthepitch/coachmode.hpp"

TEST(CoachModeParseTest, RecognizesTheControlModes) {
  EXPECT_EQ(CoachMode::Parse("ai"), CoachMode::e_TeamControl_AI);
  EXPECT_EQ(CoachMode::Parse("Players"), CoachMode::e_TeamControl_HumanPlayers);
  EXPECT_EQ(CoachMode::Parse("COACH"), CoachMode::e_TeamControl_HumanCoach);
}

TEST(CoachModeParseTest, UnknownModesFallBackToAI) {
  EXPECT_EQ(CoachMode::Parse(""), CoachMode::e_TeamControl_AI);
  EXPECT_EQ(CoachMode::Parse("spectator-manager"), CoachMode::e_TeamControl_AI);
}

TEST(CoachModeParseTest, CanonicalNamesRoundTrip) {
  for (int i = 0; i < CoachMode::e_TeamControl_Count; i++) {
    const CoachMode::e_TeamControl control = static_cast<CoachMode::e_TeamControl>(i);
    EXPECT_EQ(CoachMode::Parse(CoachMode::GetName(control)), control);
  }
}

TEST(CoachModeSetupTest, DefaultsToAIOnBothSides) {
  const CoachMode::Setup setup;
  EXPECT_EQ(setup.control[0], CoachMode::e_TeamControl_AI);
  EXPECT_EQ(setup.control[1], CoachMode::e_TeamControl_AI);
  EXPECT_FALSE(CoachMode::IsCoachMode(setup));
}

TEST(CoachModeSetupTest, TwoHumanCoachesIsTheHeadlineCase) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_HumanCoach);

  EXPECT_TRUE(CoachMode::IsCoachMode(setup));
  EXPECT_TRUE(CoachMode::IsManagerDuel(setup));
  EXPECT_TRUE(CoachMode::CanEditTactics(setup, 0));
  EXPECT_TRUE(CoachMode::CanEditTactics(setup, 1));
}

TEST(CoachModeSetupTest, OneCoachAgainstTheAIIsStillCoachMode) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_AI);

  EXPECT_TRUE(CoachMode::IsCoachMode(setup));
  EXPECT_FALSE(CoachMode::IsManagerDuel(setup));
  EXPECT_TRUE(CoachMode::CanEditTactics(setup, 0));
  EXPECT_FALSE(CoachMode::CanEditTactics(setup, 1));
}

TEST(CoachModeSetupTest, NobodyControlsPlayersOnThePitchInCoachMode) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_HumanCoach);

  EXPECT_FALSE(CoachMode::ControlsPlayersOnPitch(setup, 0));
  EXPECT_FALSE(CoachMode::ControlsPlayersOnPitch(setup, 1));
}

TEST(CoachModeSetupTest, APlayingHumanControlsPlayersAndTactics) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanPlayers, CoachMode::e_TeamControl_AI);

  EXPECT_TRUE(CoachMode::ControlsPlayersOnPitch(setup, 0));
  EXPECT_TRUE(CoachMode::CanEditTactics(setup, 0));
  // A team played by a human is not a coached team.
  EXPECT_FALSE(CoachMode::IsCoachMode(setup));
}

TEST(CoachModeSetupTest, TheAINeverGetsATacticsMenu) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_AI, CoachMode::e_TeamControl_AI);
  EXPECT_FALSE(CoachMode::CanEditTactics(setup, 0));
  EXPECT_FALSE(CoachMode::CanEditTactics(setup, 1));
}

TEST(CoachModeSetupTest, OutOfRangeTeamIdsAreHandled) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_AI);
  EXPECT_TRUE(CoachMode::CanEditTactics(setup, -5));  // clamps to team 0
  EXPECT_FALSE(CoachMode::CanEditTactics(setup, 7));  // clamps to team 1
}

// Derived from the number of human gamers assigned to each team, so an existing
// human-vs-AI match keeps behaving exactly as before.
TEST(CoachModeDeriveTest, TeamsWithHumanGamersArePlayedNotCoached) {
  const CoachMode::Setup setup = CoachMode::FromHumanGamerCounts(1, 0, false);
  EXPECT_EQ(setup.control[0], CoachMode::e_TeamControl_HumanPlayers);
  EXPECT_EQ(setup.control[1], CoachMode::e_TeamControl_AI);
}

TEST(CoachModeDeriveTest, CoachModeFlagTurnsEmptyTeamsIntoCoachedTeams) {
  const CoachMode::Setup setup = CoachMode::FromHumanGamerCounts(0, 0, true);
  EXPECT_EQ(setup.control[0], CoachMode::e_TeamControl_HumanCoach);
  EXPECT_EQ(setup.control[1], CoachMode::e_TeamControl_HumanCoach);
  EXPECT_TRUE(CoachMode::IsManagerDuel(setup));
}

TEST(CoachModeDeriveTest, CoachModeDoesNotStealTeamsFromPlayingHumans) {
  const CoachMode::Setup setup = CoachMode::FromHumanGamerCounts(2, 0, true);
  EXPECT_EQ(setup.control[0], CoachMode::e_TeamControl_HumanPlayers);
  EXPECT_EQ(setup.control[1], CoachMode::e_TeamControl_HumanCoach);
}

// "When coach mode is on, only the user(s) make tactical changes; the AI
// manager is completely disabled." Sparing only the human-coached team was not
// enough: the AI manager went on reshaping the other bench, so a manager duel
// was really one manager against a CPU that kept second-guessing him.

TEST(CoachModeTest, TheAIManagerRunsAnOrdinaryCPUTeam) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanPlayers, CoachMode::e_TeamControl_AI);
  EXPECT_TRUE(CoachMode::AIManagerRuns(setup, 1));
}

TEST(CoachModeTest, TheAIManagerNeverRunsATeamAHumanIsOnTheSticksFor) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanPlayers, CoachMode::e_TeamControl_AI);
  EXPECT_FALSE(CoachMode::AIManagerRuns(setup, 0));
}

TEST(CoachModeTest, CoachModeDisablesTheAIManagerForBothTeams) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_AI);
  ASSERT_TRUE(CoachMode::IsCoachMode(setup));
  EXPECT_FALSE(CoachMode::AIManagerRuns(setup, 0)) << "the coached team is the human's";
  EXPECT_FALSE(CoachMode::AIManagerRuns(setup, 1))
      << "and nothing on the other bench second-guesses him";
}

TEST(CoachModeTest, AManagerDuelLeavesBothBenchesToTheHumans) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_HumanCoach);
  EXPECT_FALSE(CoachMode::AIManagerRuns(setup, 0));
  EXPECT_FALSE(CoachMode::AIManagerRuns(setup, 1));
}

TEST(CoachModeTest, APlainAIvsAIMatchStillHasItsManagers) {
  // No humans at all and coach mode off: nothing has changed for these.
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_AI, CoachMode::e_TeamControl_AI);
  ASSERT_FALSE(CoachMode::IsCoachMode(setup));
  EXPECT_TRUE(CoachMode::AIManagerRuns(setup, 0));
  EXPECT_TRUE(CoachMode::AIManagerRuns(setup, 1));
}

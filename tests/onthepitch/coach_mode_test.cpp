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

TEST(CoachModeTest, TheAIManagerRunsWhicheverBenchNoHumanIsRunning) {
  // Coaching one side leaves the other to the CPU and its manager: "coach against
  // CPU" is against a managed CPU, not against a bench nobody is running.
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_AI);
  ASSERT_TRUE(CoachMode::IsCoachMode(setup));
  EXPECT_FALSE(CoachMode::AIManagerRuns(setup, 0)) << "the coached team is the human's";
  EXPECT_TRUE(CoachMode::AIManagerRuns(setup, 1)) << "the CPU still manages its own side";
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


// Who is coaching, decided by the side-selection screen rather than by counting
// heads.
//
// FromHumanGamerCounts could only infer it: a side with nobody on it became coached
// whenever the global setting was on, which meant assigning one pad to a team and
// turning coach mode on coached the *opponent*. PES marks the bench on the select-
// sides screen instead, and a pad coaching a side is a different thing from a pad
// playing it.

TEST(CoachModeSelections, APadPlayingASideMakesItAPlayersSide) {
  const int playing[2] = {1, 0};
  const int coaching[2] = {0, 0};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, false);
  EXPECT_TRUE(CoachMode::ControlsPlayersOnPitch(setup, 0));
  EXPECT_FALSE(CoachMode::IsCoachMode(setup));
}

TEST(CoachModeSelections, APadCoachingASideCoachesThatSideAndNotTheOther) {
  const int playing[2] = {0, 0};
  const int coaching[2] = {1, 0};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, false);
  EXPECT_TRUE(CoachMode::CanEditTactics(setup, 0));
  EXPECT_FALSE(CoachMode::CanEditTactics(setup, 1)) << "the opponent is not coached by default";
  EXPECT_FALSE(CoachMode::ControlsPlayersOnPitch(setup, 0)) << "a coach has nobody on the sticks";
}

TEST(CoachModeSelections, BothBenchesMayBeCoached) {
  const int playing[2] = {0, 0};
  const int coaching[2] = {1, 1};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, false);
  EXPECT_TRUE(CoachMode::IsManagerDuel(setup));
}

TEST(CoachModeSelections, OnePadMayPlayWhileAnotherCoachesTheOtherSide) {
  const int playing[2] = {1, 0};
  const int coaching[2] = {0, 1};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, false);
  EXPECT_TRUE(CoachMode::ControlsPlayersOnPitch(setup, 0));
  EXPECT_TRUE(CoachMode::CanEditTactics(setup, 1));
}

TEST(CoachModeSelections, PlayingWinsOverCoachingOnTheSameSide) {
  // somebody is on the sticks for that team, so it is not a bench-only side
  const int playing[2] = {1, 0};
  const int coaching[2] = {1, 0};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, false);
  EXPECT_TRUE(CoachMode::ControlsPlayersOnPitch(setup, 0));
}

TEST(CoachModeSelections, StreamerModeCoachesBothBenchesFromOnePad) {
  const int playing[2] = {0, 0};
  const int coaching[2] = {0, 0};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, true);
  EXPECT_TRUE(CoachMode::IsManagerDuel(setup));
}

TEST(CoachModeSelections, StreamerModeDoesNotOverrideAPlayedSide) {
  const int playing[2] = {1, 0};
  const int coaching[2] = {0, 0};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, true);
  EXPECT_TRUE(CoachMode::ControlsPlayersOnPitch(setup, 0)) << "somebody is on the sticks";
  EXPECT_TRUE(CoachMode::CanEditTactics(setup, 1));
}

// The four arrangements that have to be reachable.

TEST(CoachModeArrangements, PlayerVersusCoachPlayer) {
  const int playing[2] = {1, 0};
  const int coaching[2] = {0, 1};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, false);
  EXPECT_TRUE(CoachMode::ControlsPlayersOnPitch(setup, 0));
  EXPECT_TRUE(CoachMode::CanEditTactics(setup, 1));
  EXPECT_FALSE(CoachMode::ControlsPlayersOnPitch(setup, 1));
}

TEST(CoachModeArrangements, CoachVersusCPULeavesTheOtherBenchToTheAIManager) {
  // the case the old head-count rule got wrong: it coached the empty side too
  const int playing[2] = {0, 0};
  const int coaching[2] = {1, 0};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, false);
  EXPECT_TRUE(CoachMode::CanEditTactics(setup, 0));
  EXPECT_FALSE(CoachMode::CanEditTactics(setup, 1));
  EXPECT_TRUE(CoachMode::AIManagerRuns(setup, 1)) << "the CPU still manages its own side";
}

TEST(CoachModeArrangements, CPUVersusCPUIsUntouched) {
  const int playing[2] = {0, 0};
  const int coaching[2] = {0, 0};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, false);
  EXPECT_FALSE(CoachMode::IsCoachMode(setup));
  EXPECT_TRUE(CoachMode::AIManagerRuns(setup, 0));
  EXPECT_TRUE(CoachMode::AIManagerRuns(setup, 1));
}

TEST(CoachModeArrangements, PlayerVersusCPUIsUntouched) {
  const int playing[2] = {1, 0};
  const int coaching[2] = {0, 0};
  const CoachMode::Setup setup = CoachMode::FromSelections(playing, coaching, false);
  EXPECT_TRUE(CoachMode::ControlsPlayersOnPitch(setup, 0));
  EXPECT_TRUE(CoachMode::AIManagerRuns(setup, 1));
}

// The line the select-sides screen shows. Coach mode had no presence outside
// hotkey routing, so the screen that assigns a bench is where it has to be said.

TEST(CoachModeTip, ANormalMatchGetsNoTip) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanPlayers, CoachMode::e_TeamControl_AI);
  EXPECT_TRUE(CoachMode::Tip(setup, "Home", "Away").empty());
}

TEST(CoachModeTip, ItNamesTheCoachedSide) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_AI);
  const std::string got = CoachMode::Tip(setup, "Home", "Away");
  EXPECT_NE(got.find("Home"), std::string::npos) << got;
  EXPECT_EQ(got.find("Away"), std::string::npos) << got;
}

TEST(CoachModeTip, AManagerDuelNamesBothBenches) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_HumanCoach);
  const std::string got = CoachMode::Tip(setup, "Home", "Away");
  EXPECT_NE(got.find("Home"), std::string::npos) << got;
  EXPECT_NE(got.find("Away"), std::string::npos) << got;
}

TEST(CoachModeTip, ItSaysHowToDriveIt) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_AI);
  const std::string got = CoachMode::Tip(setup, "Home", "Away");
  EXPECT_NE(got.find("RT"), std::string::npos) << got;
  EXPECT_NE(got.find("F5"), std::string::npos) << got;
}

TEST(CoachModeTip, AnUnnamedTeamDoesNotProduceARaggedTip) {
  const CoachMode::Setup setup =
      CoachMode::Create(CoachMode::e_TeamControl_HumanCoach, CoachMode::e_TeamControl_AI);
  const std::string got = CoachMode::Tip(setup, "", "Away");
  EXPECT_FALSE(got.empty());
  EXPECT_EQ(got.find("  "), std::string::npos) << "double space: " << got;
}

TEST(CoachModeDescribe, NamesEachSidesRole) {
  EXPECT_EQ(CoachMode::Describe(CoachMode::Create(CoachMode::e_TeamControl_HumanPlayers,
                                                  CoachMode::e_TeamControl_AI)),
            "Player vs CPU");
  EXPECT_EQ(CoachMode::Describe(CoachMode::Create(CoachMode::e_TeamControl_HumanCoach,
                                                  CoachMode::e_TeamControl_AI)),
            "Coach vs CPU");
  EXPECT_EQ(CoachMode::Describe(CoachMode::Create(CoachMode::e_TeamControl_HumanPlayers,
                                                  CoachMode::e_TeamControl_HumanCoach)),
            "Player vs Coach");
  EXPECT_EQ(CoachMode::Describe(CoachMode::Create(CoachMode::e_TeamControl_AI,
                                                  CoachMode::e_TeamControl_AI)),
            "CPU vs CPU");
}

// Tests for the substitution rules needed by coach mode
// (TECHNICAL_ROADMAP.md section 3B.2).

#include "onthepitch/substitutions.hpp"

#include <gtest/gtest.h>

namespace {

Substitutions::SquadView ValidSwap() {
  Substitutions::SquadView squad;
  squad.playerOutIsOnPitch = true;
  squad.playerOutIsSentOff = false;
  squad.playerInIsOnBench = true;
  squad.playerInHasPlayed = false;
  return squad;
}

}  // namespace

TEST(SubstitutionsTest, AFreshMatchHasAFullSetOfSubstitutionsForBothTeams) {
  const Substitutions::State state;
  EXPECT_EQ(Substitutions::GetRemaining(state, 0), Substitutions::maxSubstitutions);
  EXPECT_EQ(Substitutions::GetRemaining(state, 1), Substitutions::maxSubstitutions);
}

TEST(SubstitutionsTest, AValidSwapAtAStoppageIsAccepted) {
  const Substitutions::State state;
  EXPECT_EQ(Substitutions::Validate(state, 0, ValidSwap(), true), Substitutions::e_Result_Accepted);
}

TEST(SubstitutionsTest, SubstitutionsWaitForTheNextStoppage) {
  const Substitutions::State state;
  EXPECT_EQ(Substitutions::Validate(state, 0, ValidSwap(), false),
            Substitutions::e_Result_NotAStoppage);
}

TEST(SubstitutionsTest, ThePlayerComingOffMustBeOnThePitch) {
  Substitutions::SquadView squad = ValidSwap();
  squad.playerOutIsOnPitch = false;
  EXPECT_EQ(Substitutions::Validate(Substitutions::State(), 0, squad, true),
            Substitutions::e_Result_PlayerNotOnPitch);
}

TEST(SubstitutionsTest, ASentOffPlayerCannotBeReplaced) {
  Substitutions::SquadView squad = ValidSwap();
  squad.playerOutIsSentOff = true;
  EXPECT_EQ(Substitutions::Validate(Substitutions::State(), 0, squad, true),
            Substitutions::e_Result_PlayerSentOff);
}

TEST(SubstitutionsTest, ThePlayerComingOnMustBeOnTheBench) {
  Substitutions::SquadView squad = ValidSwap();
  squad.playerInIsOnBench = false;
  EXPECT_EQ(Substitutions::Validate(Substitutions::State(), 0, squad, true),
            Substitutions::e_Result_PlayerNotAvailable);
}

TEST(SubstitutionsTest, ASubstitutedPlayerCannotComeBackOn) {
  Substitutions::SquadView squad = ValidSwap();
  squad.playerInHasPlayed = true;
  EXPECT_EQ(Substitutions::Validate(Substitutions::State(), 0, squad, true),
            Substitutions::e_Result_PlayerNotAvailable);
}

TEST(SubstitutionsTest, CommittingASwapSpendsOneOfTheTeamsSubstitutions) {
  Substitutions::State state;
  Substitutions::Commit(state, 0);

  EXPECT_EQ(Substitutions::GetRemaining(state, 0), Substitutions::maxSubstitutions - 1);
  EXPECT_EQ(Substitutions::GetRemaining(state, 1), Substitutions::maxSubstitutions);
}

TEST(SubstitutionsTest, ATeamRunsOutOfSubstitutionsAfterThree) {
  Substitutions::State state;
  for (int i = 0; i < Substitutions::maxSubstitutions; i++) {
    ASSERT_EQ(Substitutions::Validate(state, 1, ValidSwap(), true),
              Substitutions::e_Result_Accepted);
    Substitutions::Commit(state, 1);
  }

  EXPECT_EQ(Substitutions::GetRemaining(state, 1), 0);
  EXPECT_EQ(Substitutions::Validate(state, 1, ValidSwap(), true),
            Substitutions::e_Result_NoSubstitutionsLeft);
  // The other team still has all three.
  EXPECT_EQ(Substitutions::Validate(state, 0, ValidSwap(), true), Substitutions::e_Result_Accepted);
}

TEST(SubstitutionsTest, CommittingBeyondTheLimitNeverGoesNegative) {
  Substitutions::State state;
  for (int i = 0; i < Substitutions::maxSubstitutions + 2; i++)
    Substitutions::Commit(state, 0);

  EXPECT_EQ(Substitutions::GetRemaining(state, 0), 0);
}

TEST(SubstitutionsTest, TheStoppageRuleIsCheckedBeforeSquadDetails) {
  // In play, with an invalid swap: the caller should hear about the stoppage
  // first, since that is the condition that will change on its own.
  Substitutions::SquadView squad = ValidSwap();
  squad.playerInIsOnBench = false;
  EXPECT_EQ(Substitutions::Validate(Substitutions::State(), 0, squad, false),
            Substitutions::e_Result_NotAStoppage);
}

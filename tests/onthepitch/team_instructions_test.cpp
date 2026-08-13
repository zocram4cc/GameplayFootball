// Tests for manager instructions: the mentality a coach sets and the advanced
// instructions he toggles, in the style of PES team tactics. These sit on top of
// the team's philosophy and tactic sliders as offsets.

#include "onthepitch/teaminstructions.hpp"

#include <gtest/gtest.h>

#include "base/properties.hpp"

using blunted::Properties;

namespace {

Properties NeutralTactics() {
  Properties tactics;
  tactics.Set("position_offense_depth_factor", 0.5f);
  tactics.Set("position_defense_depth_factor", 0.5f);
  tactics.Set("position_offense_width_factor", 0.5f);
  tactics.Set("position_defense_width_factor", 0.5f);
  tactics.Set("team_pressure", 0.5f);
  tactics.Set("support_distance", 0.5f);
  tactics.Set("counter_attack", 0.5f);
  tactics.Set("dribble_offensiveness", 0.5f);
  return tactics;
}

}  // namespace

TEST(TeamInstructionsTest, StartsBalancedWithNoInstructionsSet) {
  const TeamInstructions::State state;
  EXPECT_EQ(state.mentality, TeamInstructions::e_Mentality_Balanced);
  EXPECT_EQ(state.instructions, TeamInstructions::instructionsNone);
}

TEST(TeamInstructionsMentalityTest, PushingUpWalksThroughTheMentalityLadder) {
  TeamInstructions::State state;

  TeamInstructions::PushUp(state);
  EXPECT_EQ(state.mentality, TeamInstructions::e_Mentality_Attacking);
  TeamInstructions::PushUp(state);
  EXPECT_EQ(state.mentality, TeamInstructions::e_Mentality_AllOutAttack);
  // ...and stops at the top.
  TeamInstructions::PushUp(state);
  EXPECT_EQ(state.mentality, TeamInstructions::e_Mentality_AllOutAttack);
}

TEST(TeamInstructionsMentalityTest, DroppingBackWalksTheOtherWay) {
  TeamInstructions::State state;

  TeamInstructions::DropBack(state);
  EXPECT_EQ(state.mentality, TeamInstructions::e_Mentality_Defensive);
  TeamInstructions::DropBack(state);
  EXPECT_EQ(state.mentality, TeamInstructions::e_Mentality_AllOutDefence);
  TeamInstructions::DropBack(state);
  EXPECT_EQ(state.mentality, TeamInstructions::e_Mentality_AllOutDefence);
}

TEST(TeamInstructionsMentalityTest, EveryMentalityHasAReadableName) {
  for (int i = 0; i < TeamInstructions::e_Mentality_Count; i++) {
    const std::string name =
        TeamInstructions::GetMentalityName(static_cast<TeamInstructions::e_Mentality>(i));
    EXPECT_FALSE(name.empty());
  }
}

TEST(TeamInstructionsMentalityTest, AttackingMentalityPushesTheTeamUpThePitch) {
  Properties attacking = NeutralTactics();
  TeamInstructions::State state;
  TeamInstructions::PushUp(state);
  TeamInstructions::Apply(state, attacking);

  const Properties reference = NeutralTactics();
  EXPECT_GT(attacking.GetReal("position_offense_depth_factor"),
            reference.GetReal("position_offense_depth_factor"));
  EXPECT_GT(attacking.GetReal("position_defense_depth_factor"),
            reference.GetReal("position_defense_depth_factor"));
}

TEST(TeamInstructionsMentalityTest, DefensiveMentalityDropsTheTeamBack) {
  Properties defensive = NeutralTactics();
  TeamInstructions::State state;
  TeamInstructions::DropBack(state);
  TeamInstructions::Apply(state, defensive);

  EXPECT_LT(defensive.GetReal("position_defense_depth_factor"), 0.5f);
  EXPECT_LT(defensive.GetReal("position_offense_depth_factor"), 0.5f);
}

TEST(TeamInstructionsMentalityTest, BalancedChangesNothing) {
  Properties tactics = NeutralTactics();
  const TeamInstructions::State state;
  TeamInstructions::Apply(state, tactics);

  const Properties reference = NeutralTactics();
  const blunted::map_Properties* properties = reference.GetProperties();
  for (const auto& entry : *properties) {
    EXPECT_FLOAT_EQ(tactics.GetReal(entry.first.c_str()),
                    reference.GetReal(entry.first.c_str()))
        << entry.first;
  }
}

TEST(TeamInstructionsMentalityTest, TheLadderIsMonotonicAndStaysInRange) {
  float previousDepth = -1.0f;
  for (int i = 0; i < TeamInstructions::e_Mentality_Count; i++) {
    Properties tactics = NeutralTactics();
    TeamInstructions::State state;
    state.mentality = static_cast<TeamInstructions::e_Mentality>(i);
    TeamInstructions::Apply(state, tactics);

    const float depth = tactics.GetReal("position_offense_depth_factor");
    EXPECT_GT(depth, previousDepth) << TeamInstructions::GetMentalityName(state.mentality);
    previousDepth = depth;

    const blunted::map_Properties* properties = tactics.GetProperties();
    for (const auto& entry : *properties) {
      EXPECT_GE(tactics.GetReal(entry.first.c_str()), 0.0f) << entry.first;
      EXPECT_LE(tactics.GetReal(entry.first.c_str()), 1.0f) << entry.first;
    }
  }
}

// --- Advanced instructions ---

TEST(TeamInstructionsAdvancedTest, InstructionsToggleIndependently) {
  TeamInstructions::State state;

  TeamInstructions::Toggle(state, TeamInstructions::e_Instruction_FrontlinePressure);
  EXPECT_TRUE(TeamInstructions::Has(state, TeamInstructions::e_Instruction_FrontlinePressure));
  EXPECT_FALSE(TeamInstructions::Has(state, TeamInstructions::e_Instruction_HugTheTouchline));

  TeamInstructions::Toggle(state, TeamInstructions::e_Instruction_HugTheTouchline);
  EXPECT_TRUE(TeamInstructions::Has(state, TeamInstructions::e_Instruction_FrontlinePressure));
  EXPECT_TRUE(TeamInstructions::Has(state, TeamInstructions::e_Instruction_HugTheTouchline));

  TeamInstructions::Toggle(state, TeamInstructions::e_Instruction_FrontlinePressure);
  EXPECT_FALSE(TeamInstructions::Has(state, TeamInstructions::e_Instruction_FrontlinePressure));
}

TEST(TeamInstructionsAdvancedTest, EveryInstructionHasAReadableName) {
  for (int i = 0; i < TeamInstructions::instructionCount; i++) {
    const TeamInstructions::e_Instruction instruction = TeamInstructions::GetInstructionAt(i);
    EXPECT_FALSE(TeamInstructions::GetInstructionName(instruction).empty());
  }
}

TEST(TeamInstructionsAdvancedTest, FrontlinePressureRaisesPressingAndTheLine) {
  Properties tactics = NeutralTactics();
  TeamInstructions::State state;
  TeamInstructions::Toggle(state, TeamInstructions::e_Instruction_FrontlinePressure);
  TeamInstructions::Apply(state, tactics);

  EXPECT_GT(tactics.GetReal("team_pressure"), 0.5f);
  EXPECT_GT(tactics.GetReal("position_defense_depth_factor"), 0.5f);
}

TEST(TeamInstructionsAdvancedTest, DeepDefensiveLineDropsTheBackLine) {
  Properties tactics = NeutralTactics();
  TeamInstructions::State state;
  TeamInstructions::Toggle(state, TeamInstructions::e_Instruction_DeepDefensiveLine);
  TeamInstructions::Apply(state, tactics);

  EXPECT_LT(tactics.GetReal("position_defense_depth_factor"), 0.5f);
}

TEST(TeamInstructionsAdvancedTest, HugTheTouchlineWidensTheTeamAndCentreShadingNarrowsIt) {
  Properties wide = NeutralTactics();
  TeamInstructions::State wideState;
  TeamInstructions::Toggle(wideState, TeamInstructions::e_Instruction_HugTheTouchline);
  TeamInstructions::Apply(wideState, wide);

  Properties narrow = NeutralTactics();
  TeamInstructions::State narrowState;
  TeamInstructions::Toggle(narrowState, TeamInstructions::e_Instruction_CentreShading);
  TeamInstructions::Apply(narrowState, narrow);

  EXPECT_GT(wide.GetReal("position_offense_width_factor"),
            narrow.GetReal("position_offense_width_factor"));
}

TEST(TeamInstructionsAdvancedTest, TikiTakaShortensSupportAndLongBallCounterLengthensIt) {
  Properties tiki = NeutralTactics();
  TeamInstructions::State tikiState;
  TeamInstructions::Toggle(tikiState, TeamInstructions::e_Instruction_TikiTaka);
  TeamInstructions::Apply(tikiState, tiki);

  Properties longBall = NeutralTactics();
  TeamInstructions::State longBallState;
  TeamInstructions::Toggle(longBallState, TeamInstructions::e_Instruction_LongBallCounter);
  TeamInstructions::Apply(longBallState, longBall);

  EXPECT_LT(tiki.GetReal("support_distance"), longBall.GetReal("support_distance"));
  EXPECT_GT(longBall.GetReal("counter_attack"), tiki.GetReal("counter_attack"));
}

TEST(TeamInstructionsAdvancedTest, ConflictingInstructionsStillLeaveValidTactics) {
  Properties tactics = NeutralTactics();
  TeamInstructions::State state;
  state.mentality = TeamInstructions::e_Mentality_AllOutAttack;
  for (int i = 0; i < TeamInstructions::instructionCount; i++)
    TeamInstructions::Toggle(state, TeamInstructions::GetInstructionAt(i));
  TeamInstructions::Apply(state, tactics);

  const blunted::map_Properties* properties = tactics.GetProperties();
  for (const auto& entry : *properties) {
    EXPECT_GE(tactics.GetReal(entry.first.c_str()), 0.0f) << entry.first;
    EXPECT_LE(tactics.GetReal(entry.first.c_str()), 1.0f) << entry.first;
  }
}

// --- Feedback shown on screen when a hotkey is pressed ---

TEST(TeamInstructionsFeedbackTest, DescribesTheMentalityAndActiveInstructions) {
  TeamInstructions::State state;
  TeamInstructions::PushUp(state);
  TeamInstructions::Toggle(state, TeamInstructions::e_Instruction_FrontlinePressure);

  const std::string feedback = TeamInstructions::Describe(state);
  EXPECT_NE(feedback.find(TeamInstructions::GetMentalityName(state.mentality)), std::string::npos);
  EXPECT_NE(feedback.find(TeamInstructions::GetInstructionName(
                TeamInstructions::e_Instruction_FrontlinePressure)),
            std::string::npos);
}

TEST(TeamInstructionsFeedbackTest, SaysSoWhenNothingIsSet) {
  const TeamInstructions::State state;
  const std::string feedback = TeamInstructions::Describe(state);
  EXPECT_FALSE(feedback.empty());
  EXPECT_NE(feedback.find(TeamInstructions::GetMentalityName(state.mentality)), std::string::npos);
}

// Tests for manager instructions: the mentality a coach sets and the advanced
// instructions he toggles, in the style of PES team tactics. These sit on top of
// the team's philosophy and tactic sliders as offsets.

#include <set>

#include <gtest/gtest.h>

#include "base/properties.hpp"
#include "onthepitch/teaminstructions.hpp"
#include "onthepitch/teamphilosophy.hpp"

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
    EXPECT_FLOAT_EQ(tactics.GetReal(entry.first.c_str()), reference.GetReal(entry.first.c_str()))
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

// --- Gamepad presets: hold RT and pick a mentality on the d-pad ---

TEST(TeamInstructionsPresetTest, TheFourDirectionsCoverTheExtremes) {
  EXPECT_EQ(TeamInstructions::GetPresetForDirection(TeamInstructions::e_PresetDirection_Up),
            TeamInstructions::e_Mentality_AllOutAttack);
  EXPECT_EQ(TeamInstructions::GetPresetForDirection(TeamInstructions::e_PresetDirection_Right),
            TeamInstructions::e_Mentality_Attacking);
  EXPECT_EQ(TeamInstructions::GetPresetForDirection(TeamInstructions::e_PresetDirection_Left),
            TeamInstructions::e_Mentality_Defensive);
  EXPECT_EQ(TeamInstructions::GetPresetForDirection(TeamInstructions::e_PresetDirection_Down),
            TeamInstructions::e_Mentality_AllOutDefence);
}

TEST(TeamInstructionsPresetTest, EachDirectionIsADistinctPreset) {
  std::set<int> mentalities;
  for (int i = 0; i < TeamInstructions::presetDirectionCount; i++) {
    mentalities.insert(static_cast<int>(TeamInstructions::GetPresetForDirection(
        static_cast<TeamInstructions::e_PresetDirection>(i))));
  }
  EXPECT_EQ(static_cast<int>(mentalities.size()), TeamInstructions::presetDirectionCount);
}

TEST(TeamInstructionsPresetTest, SelectingAPresetSetsItOutright) {
  TeamInstructions::State state;
  TeamInstructions::SelectMentality(state, TeamInstructions::e_Mentality_AllOutAttack);
  EXPECT_EQ(state.mentality, TeamInstructions::e_Mentality_AllOutAttack);

  // A different preset replaces it, without walking the ladder.
  TeamInstructions::SelectMentality(state, TeamInstructions::e_Mentality_AllOutDefence);
  EXPECT_EQ(state.mentality, TeamInstructions::e_Mentality_AllOutDefence);
}

TEST(TeamInstructionsPresetTest, PressingTheSamePresetAgainReturnsToBalanced) {
  TeamInstructions::State state;
  TeamInstructions::SelectMentality(state, TeamInstructions::e_Mentality_Attacking);
  TeamInstructions::SelectMentality(state, TeamInstructions::e_Mentality_Attacking);
  EXPECT_EQ(state.mentality, TeamInstructions::e_Mentality_Balanced);
}

TEST(TeamInstructionsPresetTest, TheDpadReachesEveryMentalityIncludingBalanced) {
  TeamInstructions::State state;
  std::set<int> reached;
  reached.insert(static_cast<int>(state.mentality));  // balanced to start with

  for (int i = 0; i < TeamInstructions::presetDirectionCount; i++) {
    TeamInstructions::SelectMentality(state,
                                      TeamInstructions::GetPresetForDirection(
                                          static_cast<TeamInstructions::e_PresetDirection>(i)));
    reached.insert(static_cast<int>(state.mentality));
  }
  EXPECT_EQ(static_cast<int>(reached.size()), TeamInstructions::e_Mentality_Count);
}

TEST(TeamInstructionsPresetTest, TheFaceButtonInstructionsAreTheHandyFour) {
  std::set<int> instructions;
  for (int i = 0; i < TeamInstructions::quickInstructionCount; i++) {
    const TeamInstructions::e_Instruction instruction = TeamInstructions::GetQuickInstructionAt(i);
    EXPECT_FALSE(TeamInstructions::GetInstructionName(instruction).empty());
    instructions.insert(static_cast<int>(instruction));
  }
  EXPECT_EQ(static_cast<int>(instructions.size()), TeamInstructions::quickInstructionCount);
}

// Setting the advanced instructions before kick-off.
//
// They lived only on the TeamAIController and started at their defaults every match,
// so a manager could only reach them from the touchline once play had begun - which
// makes them useless for a side you are not going to be shouting at. They are now
// carried in the team's tactics beside the philosophy, which is what the game plan
// edits and what the database stores.

TEST(InstructionsInTactics, ASavedStateComesBackTheSame) {
  TeamInstructions::State state;
  state.mentality = TeamInstructions::e_Mentality_Attacking;
  TeamInstructions::Toggle(state, TeamInstructions::e_Instruction_TikiTaka);
  TeamInstructions::Toggle(state, TeamInstructions::e_Instruction_CentreShading);

  blunted::Properties props;
  TeamInstructions::Save(state, props);
  const TeamInstructions::State back = TeamInstructions::Load(props);

  EXPECT_EQ(back.mentality, state.mentality);
  EXPECT_EQ(back.instructions, state.instructions);
}

TEST(InstructionsInTactics, TacticsWithNothingSavedGiveTheDefaults) {
  blunted::Properties props;
  const TeamInstructions::State back = TeamInstructions::Load(props);
  EXPECT_EQ(back.mentality, TeamInstructions::e_Mentality_Balanced);
  EXPECT_EQ(back.instructions, TeamInstructions::instructionsNone);
}

TEST(InstructionsInTactics, EveryMentalityRoundTrips) {
  for (int i = 0; i < TeamInstructions::e_Mentality_Count; i++) {
    TeamInstructions::State state;
    state.mentality = static_cast<TeamInstructions::e_Mentality>(i);
    blunted::Properties props;
    TeamInstructions::Save(state, props);
    EXPECT_EQ(TeamInstructions::Load(props).mentality, state.mentality) << "mentality " << i;
  }
}

TEST(InstructionsInTactics, EveryInstructionRoundTrips) {
  for (int i = 0; i < TeamInstructions::instructionCount; i++) {
    TeamInstructions::State state;
    TeamInstructions::Toggle(state, TeamInstructions::GetInstructionAt(i));
    blunted::Properties props;
    TeamInstructions::Save(state, props);
    EXPECT_TRUE(TeamInstructions::Has(TeamInstructions::Load(props),
                                      TeamInstructions::GetInstructionAt(i)))
        << "instruction " << i << " did not survive";
  }
}

TEST(InstructionsInTactics, NonsenseInTheTacticsIsIgnoredRatherThanTrusted) {
  blunted::Properties props;
  props.Set("mentality", 99.0f);
  props.Set("instructions", -3.0f);
  const TeamInstructions::State back = TeamInstructions::Load(props);
  EXPECT_LT(back.mentality, TeamInstructions::e_Mentality_Count);
  EXPECT_GE(back.mentality, 0);
}

TEST(InstructionsInTactics, TheKeysAreNotOfferedAsSliders) {
  // they would otherwise join the tactics sliders, the way philosophy did
  EXPECT_FALSE(TeamPhilosophy::IsSliderTactic("mentality"));
  EXPECT_FALSE(TeamPhilosophy::IsSliderTactic("instructions"));
}

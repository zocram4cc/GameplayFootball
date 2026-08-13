// Tests for tactical archetypes ("team philosophies") described in
// SIMULATION_IMPROVEMENT_PROPOSAL.md section 2A.

#include "onthepitch/teamphilosophy.hpp"

#include <gtest/gtest.h>

#include "base/properties.hpp"
#include "gamedefines.hpp"

using blunted::Properties;

namespace {

Properties NeutralTactics() {
  Properties tactics;
  tactics.Set("team_pressure", 0.5f);
  tactics.Set("position_offense_midfieldfocus", 0.5f);
  tactics.Set("position_defense_midfieldfocus", 0.5f);
  tactics.Set("position_defense_depth_factor", 0.5f);
  tactics.Set("position_defense_microfocus_strength", 0.5f);
  tactics.Set("dribble_offensiveness", 0.5f);
  tactics.Set("support_distance", 0.5f);
  return tactics;
}

}  // namespace

TEST(TeamPhilosophyParseTest, RecognizesArchetypeNamesCaseInsensitively) {
  EXPECT_EQ(TeamPhilosophy::Parse("gegenpressing"), TeamPhilosophy::e_Philosophy_Gegenpressing);
  EXPECT_EQ(TeamPhilosophy::Parse("GegenPressing"), TeamPhilosophy::e_Philosophy_Gegenpressing);
  EXPECT_EQ(TeamPhilosophy::Parse("TIKI-TAKA"), TeamPhilosophy::e_Philosophy_TikiTaka);
  EXPECT_EQ(TeamPhilosophy::Parse("Park The Bus"), TeamPhilosophy::e_Philosophy_ParkTheBus);
}

TEST(TeamPhilosophyParseTest, AcceptsSpacingAndPunctuationVariants) {
  EXPECT_EQ(TeamPhilosophy::Parse("tikitaka"), TeamPhilosophy::e_Philosophy_TikiTaka);
  EXPECT_EQ(TeamPhilosophy::Parse("tiki taka"), TeamPhilosophy::e_Philosophy_TikiTaka);
  EXPECT_EQ(TeamPhilosophy::Parse("park_the_bus"), TeamPhilosophy::e_Philosophy_ParkTheBus);
  EXPECT_EQ(TeamPhilosophy::Parse("  gegenpressing  "), TeamPhilosophy::e_Philosophy_Gegenpressing);
}

TEST(TeamPhilosophyParseTest, UnknownAndEmptyNamesFallBackToBalanced) {
  EXPECT_EQ(TeamPhilosophy::Parse(""), TeamPhilosophy::e_Philosophy_Balanced);
  EXPECT_EQ(TeamPhilosophy::Parse("catenaccio-ish nonsense"), TeamPhilosophy::e_Philosophy_Balanced);
}

TEST(TeamPhilosophyParseTest, CanonicalNamesRoundTrip) {
  for (int i = 0; i < TeamPhilosophy::e_Philosophy_Count; i++) {
    const TeamPhilosophy::e_Philosophy philosophy = static_cast<TeamPhilosophy::e_Philosophy>(i);
    EXPECT_EQ(TeamPhilosophy::Parse(TeamPhilosophy::GetName(philosophy)), philosophy);
  }
}

TEST(TeamPhilosophyPresetTest, BalancedLeavesTacticsUntouched) {
  Properties tactics = NeutralTactics();
  TeamPhilosophy::ApplyPreset(TeamPhilosophy::e_Philosophy_Balanced, tactics);

  EXPECT_FLOAT_EQ(tactics.GetReal("team_pressure"), 0.5f);
  EXPECT_FLOAT_EQ(tactics.GetReal("position_defense_depth_factor"), 0.5f);
  EXPECT_FLOAT_EQ(tactics.GetReal("dribble_offensiveness"), 0.5f);
}

TEST(TeamPhilosophyPresetTest, GegenpressingMaxesPressureAndCompactness) {
  Properties tactics = NeutralTactics();
  TeamPhilosophy::ApplyPreset(TeamPhilosophy::e_Philosophy_Gegenpressing, tactics);

  EXPECT_GE(tactics.GetReal("team_pressure"), 0.9f);
  EXPECT_FLOAT_EQ(tactics.GetReal("position_defense_microfocus_strength"), 1.0f);
  // A high defensive line supports winning the ball back immediately.
  EXPECT_GE(tactics.GetReal("position_defense_depth_factor"), 0.8f);
}

TEST(TeamPhilosophyPresetTest, TikiTakaFocusesMidfieldAndDiscouragesDribbling) {
  Properties tactics = NeutralTactics();
  TeamPhilosophy::ApplyPreset(TeamPhilosophy::e_Philosophy_TikiTaka, tactics);

  EXPECT_GE(tactics.GetReal("position_offense_midfieldfocus"), 0.8f);
  EXPECT_LE(tactics.GetReal("dribble_offensiveness"), 0.2f);
  // Short passing links mean support players stay close.
  EXPECT_LE(tactics.GetReal("support_distance"), 0.3f);
}

TEST(TeamPhilosophyPresetTest, ParkTheBusMinimizesDefensiveDepth) {
  Properties tactics = NeutralTactics();
  TeamPhilosophy::ApplyPreset(TeamPhilosophy::e_Philosophy_ParkTheBus, tactics);

  EXPECT_FLOAT_EQ(tactics.GetReal("position_defense_depth_factor"), 0.0f);
  EXPECT_LE(tactics.GetReal("team_pressure"), 0.2f);
}

TEST(TeamPhilosophyPresetTest, PresetsStayWithinSliderRange) {
  for (int i = 0; i < TeamPhilosophy::e_Philosophy_Count; i++) {
    Properties tactics = NeutralTactics();
    TeamPhilosophy::ApplyPreset(static_cast<TeamPhilosophy::e_Philosophy>(i), tactics);
    const blunted::map_Properties* properties = tactics.GetProperties();
    for (const auto& entry : *properties) {
      const float value = tactics.GetReal(entry.first.c_str(), -1.0f);
      EXPECT_GE(value, 0.0f) << entry.first << " in philosophy " << i;
      EXPECT_LE(value, 1.0f) << entry.first << " in philosophy " << i;
    }
  }
}

TEST(TeamPhilosophyBehaviorTest, OnlyGegenpressingExtendsThePressureWindow) {
  EXPECT_EQ(TeamPhilosophy::GetTeamPressureDurationBonus_ms(
                TeamPhilosophy::e_Philosophy_Gegenpressing),
            5000UL);
  EXPECT_EQ(
      TeamPhilosophy::GetTeamPressureDurationBonus_ms(TeamPhilosophy::e_Philosophy_Balanced), 0UL);
  EXPECT_EQ(
      TeamPhilosophy::GetTeamPressureDurationBonus_ms(TeamPhilosophy::e_Philosophy_ParkTheBus), 0UL);
}

TEST(TeamPhilosophyBehaviorTest, OnlyGegenpressingCounterPressesOnPossessionLoss) {
  EXPECT_TRUE(TeamPhilosophy::PressesOnPossessionLoss(TeamPhilosophy::e_Philosophy_Gegenpressing));
  EXPECT_FALSE(TeamPhilosophy::PressesOnPossessionLoss(TeamPhilosophy::e_Philosophy_Balanced));
  EXPECT_FALSE(TeamPhilosophy::PressesOnPossessionLoss(TeamPhilosophy::e_Philosophy_TikiTaka));
}

TEST(TeamPhilosophyBehaviorTest, GegenpressingCostsTwentyPercentExtraStamina) {
  EXPECT_FLOAT_EQ(
      TeamPhilosophy::GetStaminaDrainMultiplier(TeamPhilosophy::e_Philosophy_Gegenpressing), 1.2f);
  EXPECT_FLOAT_EQ(TeamPhilosophy::GetStaminaDrainMultiplier(TeamPhilosophy::e_Philosophy_Balanced),
                  1.0f);
  EXPECT_FLOAT_EQ(TeamPhilosophy::GetStaminaDrainMultiplier(TeamPhilosophy::e_Philosophy_ParkTheBus),
                  1.0f);
}

TEST(TeamPhilosophyBehaviorTest, OnlyTikiTakaPrefersShortPassing) {
  EXPECT_TRUE(TeamPhilosophy::PrefersShortPassing(TeamPhilosophy::e_Philosophy_TikiTaka));
  EXPECT_FALSE(TeamPhilosophy::PrefersShortPassing(TeamPhilosophy::e_Philosophy_Balanced));
  EXPECT_FALSE(TeamPhilosophy::PrefersShortPassing(TeamPhilosophy::e_Philosophy_Gegenpressing));
}

// Own goal sits at x == side * pitchHalfW, so the penalty-box edge for a team is
// side * (pitchHalfW - penaltyBoxDepth) and "forward" is the -side direction.
TEST(TeamPhilosophyOffsideTrapTest, ParkTheBusPullsAnAdvancedTrapBackToTheBoxEdge) {
  const float boxEdgeRight = pitchHalfW - TeamPhilosophy::penaltyBoxDepth;

  // Team with side +1 defends the +x goal; a trap at the halfway line is far too high.
  EXPECT_FLOAT_EQ(TeamPhilosophy::AdaptOffsideTrapX(TeamPhilosophy::e_Philosophy_ParkTheBus, 0.0f, 1),
                  boxEdgeRight);
  // Mirrored for the team defending the -x goal.
  EXPECT_FLOAT_EQ(
      TeamPhilosophy::AdaptOffsideTrapX(TeamPhilosophy::e_Philosophy_ParkTheBus, 0.0f, -1),
      -boxEdgeRight);
}

TEST(TeamPhilosophyOffsideTrapTest, ParkTheBusKeepsATrapThatIsAlreadyDeeper) {
  const float deepTrap = pitchHalfW - 8.0f;  // inside the box already
  EXPECT_FLOAT_EQ(
      TeamPhilosophy::AdaptOffsideTrapX(TeamPhilosophy::e_Philosophy_ParkTheBus, deepTrap, 1),
      deepTrap);
  EXPECT_FLOAT_EQ(
      TeamPhilosophy::AdaptOffsideTrapX(TeamPhilosophy::e_Philosophy_ParkTheBus, -deepTrap, -1),
      -deepTrap);
}

TEST(TeamPhilosophyOffsideTrapTest, OtherPhilosophiesLeaveTheTrapAlone) {
  EXPECT_FLOAT_EQ(
      TeamPhilosophy::AdaptOffsideTrapX(TeamPhilosophy::e_Philosophy_Balanced, 12.5f, 1), 12.5f);
  EXPECT_FLOAT_EQ(
      TeamPhilosophy::AdaptOffsideTrapX(TeamPhilosophy::e_Philosophy_Gegenpressing, -30.0f, -1),
      -30.0f);
}

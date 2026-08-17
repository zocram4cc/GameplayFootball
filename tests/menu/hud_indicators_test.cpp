// What the in-play player indicator has to say.
//
// Gui2PlayerHUD exists and its Redraw() is empty, so a match shows no current
// player, no philosophy and no attack/defence level at all. PES's own indicator
// (docs/VGL26_REFERENCE.md, measured off the VGL26 broadcast) is, from the outer
// edge inwards: the team badge, a dark rounded plate carrying the shirt number
// and the name, a thin green stamina bar along the plate's top edge, a small
// vertical box whose white band is the attack/defence level, and a two-tone
// circular dial for the tactical style. The away side is drawn dimmer.
//
// The values behind it all already exist: the philosophy the manager picked
// (TeamPhilosophy) and the mentality he set from the touchline (TeamInstructions,
// five rungs from AllOutDefence to AllOutAttack - the same ladder PES's box
// shows). Nothing here derives a level of its own. An indicator showing a number
// nobody set would be worse than no indicator at all.

#include <gtest/gtest.h>

#include "menu/ingame/hudindicators.hpp"

TEST(AttackDefenceLevel, TheBandFollowsTheMentalityTheManagerSet) {
  // TeamInstructions::e_Mentality is the control PES's vertical box shows:
  // AllOutDefence, Defensive, Balanced, Attacking, AllOutAttack. The box is drawn
  // bottom-up, so the most defensive rung puts the band at the bottom.
  EXPECT_FLOAT_EQ(HudIndicators::LevelBandPosition(0, 5), 0.0f);
  EXPECT_FLOAT_EQ(HudIndicators::LevelBandPosition(2, 5), 0.5f);
  EXPECT_FLOAT_EQ(HudIndicators::LevelBandPosition(4, 5), 1.0f);
}

TEST(AttackDefenceLevel, EveryRungGetsItsOwnPositionSoTheyCanBeToldApart) {
  float previous = -1.0f;
  for (int mentality = 0; mentality < 5; mentality++) {
    const float position = HudIndicators::LevelBandPosition(mentality, 5);
    EXPECT_GT(position, previous) << "rung " << mentality;
    previous = position;
  }
}

TEST(AttackDefenceLevel, ARungOutsideTheLadderIsClampedRatherThanDrawnOffTheBox) {
  EXPECT_FLOAT_EQ(HudIndicators::LevelBandPosition(-1, 5), 0.0f);
  EXPECT_FLOAT_EQ(HudIndicators::LevelBandPosition(9, 5), 1.0f);
}

TEST(AttackDefenceLevel, ALadderWithOneRungOrNoneSitsInTheMiddleRatherThanDividingByZero) {
  EXPECT_FLOAT_EQ(HudIndicators::LevelBandPosition(0, 1), 0.5f);
  EXPECT_FLOAT_EQ(HudIndicators::LevelBandPosition(0, 0), 0.5f);
}

TEST(PlateText, TheUsersSidePutsTheNumberBeforeTheName) {
  EXPECT_EQ(HudIndicators::PlateText(32, "THE CHAMP!", true), "32 THE CHAMP!");
}

TEST(PlateText, TheOtherSidePutsTheNumberAfterIt) {
  EXPECT_EQ(HudIndicators::PlateText(31, "Exploding Tama", false), "Exploding Tama 31");
}

TEST(PlateText, AnUnnumberedPlayerIsJustHisName) {
  EXPECT_EQ(HudIndicators::PlateText(0, "Woomy", true), "Woomy");
  EXPECT_EQ(HudIndicators::PlateText(0, "Woomy", false), "Woomy");
}

TEST(PlateText, ANamelessPlayerIsStillIdentifiedByHisNumber) {
  EXPECT_EQ(HudIndicators::PlateText(7, "", true), "7");
}

TEST(StaminaFraction, AFreshPlayersBarIsFullAndASpentOnesIsEmpty) {
  EXPECT_FLOAT_EQ(HudIndicators::StaminaFraction(1.0f), 1.0f);
  EXPECT_FLOAT_EQ(HudIndicators::StaminaFraction(0.0f), 0.0f);
}

TEST(StaminaFraction, AConditionOutsideTheRangeIsClampedRatherThanDrawnOffThePlate) {
  EXPECT_FLOAT_EQ(HudIndicators::StaminaFraction(1.4f), 1.0f);
  EXPECT_FLOAT_EQ(HudIndicators::StaminaFraction(-0.2f), 0.0f);
}

TEST(StaminaFraction, TinyDriftGivesTheSameValueSoTheBarIsNotRescaledEveryFrame) {
  // Resizing a Gui2Image rescales its surface. Fatigue drifts continuously, so an
  // unquantised bar would rescale two surfaces on almost every frame - the same
  // per-frame cost the skinning work just removed. A hundredth of the bar is well
  // under a pixel at any sane HUD size.
  EXPECT_FLOAT_EQ(HudIndicators::StaminaFraction(0.8000f), HudIndicators::StaminaFraction(0.8003f));
  EXPECT_FLOAT_EQ(HudIndicators::StaminaFraction(0.5001f), HudIndicators::StaminaFraction(0.4999f));
}

TEST(StaminaFraction, AChangeWorthSeeingStillComesThrough) {
  EXPECT_NE(HudIndicators::StaminaFraction(0.80f), HudIndicators::StaminaFraction(0.75f));
}

TEST(PhilosophyDial, TheAttackingShareGrowsFromParkTheBusToGegenpressing) {
  // Philosophy indices are TeamPhilosophy::e_Philosophy: Balanced,
  // Gegenpressing, TikiTaka, ParkTheBus.
  const float balanced = HudIndicators::PhilosophyDialSplit(0);
  const float gegen = HudIndicators::PhilosophyDialSplit(1);
  const float parkTheBus = HudIndicators::PhilosophyDialSplit(3);
  EXPECT_LT(parkTheBus, balanced);
  EXPECT_LT(balanced, gegen);
}

TEST(PhilosophyDial, BalancedSplitsTheDialEvenly) {
  EXPECT_FLOAT_EQ(HudIndicators::PhilosophyDialSplit(0), 0.5f);
}

TEST(PhilosophyDial, EveryPhilosophyGetsAShareOfTheDialRatherThanNone) {
  for (int philosophy = 0; philosophy < 4; philosophy++) {
    const float split = HudIndicators::PhilosophyDialSplit(philosophy);
    EXPECT_GT(split, 0.0f) << "philosophy " << philosophy;
    EXPECT_LT(split, 1.0f) << "philosophy " << philosophy;
  }
}

TEST(PhilosophyDial, AnUnknownPhilosophyFallsBackToAnEvenSplit) {
  EXPECT_FLOAT_EQ(HudIndicators::PhilosophyDialSplit(99), 0.5f);
  EXPECT_FLOAT_EQ(HudIndicators::PhilosophyDialSplit(-1), 0.5f);
}

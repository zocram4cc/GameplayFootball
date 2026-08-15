// Law 11: there is no offside offence when the ball is received directly from
// a goal kick, a throw-in or a corner kick. GF used to exempt only throw-ins,
// which made every corner a phantom-offside generator (docs/RULESET_AUDIT.md
// gap 1).

#include <gtest/gtest.h>

#include "onthepitch/offsiderule.hpp"

TEST(OffsideRuleTest, TheLawsExemptRestartsCannotCreateOffside) {
  EXPECT_FALSE(OffsideRule::CanCreateOffside(e_SetPiece_ThrowIn));
  EXPECT_FALSE(OffsideRule::CanCreateOffside(e_SetPiece_GoalKick));
  EXPECT_FALSE(OffsideRule::CanCreateOffside(e_SetPiece_Corner));
}

TEST(OffsideRuleTest, EveryOtherRestartLeavesOffsideInForce) {
  EXPECT_TRUE(OffsideRule::CanCreateOffside(e_SetPiece_None));
  EXPECT_TRUE(OffsideRule::CanCreateOffside(e_SetPiece_KickOff));
  EXPECT_TRUE(OffsideRule::CanCreateOffside(e_SetPiece_FreeKick));
  EXPECT_TRUE(OffsideRule::CanCreateOffside(e_SetPiece_Penalty));
}

// The referee snapshots offside positions at the moment of a touch. That
// snapshot must be skipped when the touch is the delivery of an exempt
// restart, and must always be skipped when the ball is out of play.

TEST(OffsideRuleTest, OpenPlayTouchesAreSnapshotted) {
  EXPECT_TRUE(OffsideRule::ShouldSnapshot(true, false, e_SetPiece_None));
}

TEST(OffsideRuleTest, ExemptRestartDeliveriesAreNotSnapshotted) {
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_ThrowIn));
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_GoalKick));
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_Corner));
}

TEST(OffsideRuleTest, FreeKickDeliveriesStillArmOffside) {
  EXPECT_TRUE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_FreeKick));
  EXPECT_TRUE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_KickOff));
}

TEST(OffsideRuleTest, NothingIsSnapshottedOutOfPlay) {
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(false, false, e_SetPiece_None));
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(false, true, e_SetPiece_FreeKick));
}

// Law 11: an attacker in an offside position becomes onside again when an
// opponent deliberately plays the ball (a controlled pass or clearance). A
// deflection, or a deliberate save, does not reset the offside phase.

TEST(OffsideRuleTest, OnlyAControlledKickIsADeliberatePlay) {
  EXPECT_TRUE(OffsideRule::IsDeliberatePlay(e_TouchType_Intentional_Kicked));
  // A collision-driven deflection is never a deliberate play.
  EXPECT_FALSE(OffsideRule::IsDeliberatePlay(e_TouchType_Accidental));
  // GF cannot tell a save from a deliberate header, and a save must never
  // reset the phase, so non-kicked touches conservatively keep it.
  EXPECT_FALSE(OffsideRule::IsDeliberatePlay(e_TouchType_Intentional_Nonkicked));
  EXPECT_FALSE(OffsideRule::IsDeliberatePlay(e_TouchType_None));
}

TEST(OffsideRuleTest, ATeammateTouchAlwaysStartsANewPhase) {
  // The flagged players' own team playing the ball is a fresh judgement, no
  // matter how the ball was touched.
  EXPECT_TRUE(OffsideRule::TouchResetsPhase(false, e_TouchType_Intentional_Kicked));
  EXPECT_TRUE(OffsideRule::TouchResetsPhase(false, e_TouchType_Intentional_Nonkicked));
  EXPECT_TRUE(OffsideRule::TouchResetsPhase(false, e_TouchType_Accidental));
}

TEST(OffsideRuleTest, ADefendersDeliberatePlayResetsThePhase) {
  EXPECT_TRUE(OffsideRule::TouchResetsPhase(true, e_TouchType_Intentional_Kicked));
}

TEST(OffsideRuleTest, ADefendersDeflectionOrSaveKeepsThePhaseAlive) {
  EXPECT_FALSE(OffsideRule::TouchResetsPhase(true, e_TouchType_Accidental));
  EXPECT_FALSE(OffsideRule::TouchResetsPhase(true, e_TouchType_Intentional_Nonkicked));
  EXPECT_FALSE(OffsideRule::TouchResetsPhase(true, e_TouchType_None));
}

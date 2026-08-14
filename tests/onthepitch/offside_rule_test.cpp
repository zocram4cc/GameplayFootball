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

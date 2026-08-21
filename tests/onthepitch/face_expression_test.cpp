// Which face a player wears.
//
// The rig is PES's own - 1,642 face-weighted vertices off face_skel.frig, of which
// 1,372 bind to the stock body - and it was driven by two lines buried in
// HumanoidBase::Process. Pulled out so the rule can be stated and tested, and so a
// player who has just been hurt stops smiling.

#include <gtest/gtest.h>

#include "onthepitch/player/humanoid/facerig.hpp"

using blunted::ChooseExpression;
using blunted::e_FaceExpression;

namespace {

constexpr float kWalking = 2.0f;
constexpr float kSprinting = 8.0f;
constexpr float kFit = 0.0f;
constexpr float kHurt = 0.4f;

}  // namespace

TEST(FaceExpression, AtRestTheFaceIsNeutral) {
  EXPECT_EQ(ChooseExpression("movement", "", kWalking, kFit), e_FaceExpression::Neutral);
}

TEST(FaceExpression, SprintingIsAnEffort) {
  EXPECT_EQ(ChooseExpression("movement", "", kSprinting, kFit), e_FaceExpression::Exert);
}

TEST(FaceExpression, RunningIsNotSprinting) {
  // the threshold is the engine's own sprint speed, not any movement at all
  EXPECT_EQ(ChooseExpression("movement", "", 6.9f, kFit), e_FaceExpression::Neutral);
}

TEST(FaceExpression, ACelebrationSmilesAndTheOtherMoodDoesNot) {
  EXPECT_EQ(ChooseExpression("special", "1", kWalking, kFit), e_FaceExpression::Happy);
  EXPECT_EQ(ChooseExpression("special", "2", kWalking, kFit), e_FaceExpression::Sad);
}

TEST(FaceExpression, BeingHurtBeatsEverythingElse) {
  EXPECT_EQ(ChooseExpression("special", "1", kWalking, kHurt), e_FaceExpression::Pain);
  EXPECT_EQ(ChooseExpression("movement", "", kSprinting, kHurt), e_FaceExpression::Pain);
  EXPECT_EQ(ChooseExpression("movement", "", kWalking, kHurt), e_FaceExpression::Pain);
}

TEST(FaceExpression, AFitPlayerIsNeverInPain) {
  EXPECT_NE(ChooseExpression("movement", "", kWalking, kFit), e_FaceExpression::Pain);
  EXPECT_NE(ChooseExpression("movement", "", kSprinting, 0.0f), e_FaceExpression::Pain);
}

TEST(FaceExpression, ACelebrationBeatsTheSprintFace) {
  // a scorer running to the corner is celebrating, not straining
  EXPECT_EQ(ChooseExpression("special", "1", kSprinting, kFit), e_FaceExpression::Happy);
}

TEST(FaceExpression, AnUnknownMoodStillSmilesRatherThanFailing) {
  EXPECT_EQ(ChooseExpression("special", "", kWalking, kFit), e_FaceExpression::Happy);
  EXPECT_EQ(ChooseExpression("special", "7", kWalking, kFit), e_FaceExpression::Happy);
}

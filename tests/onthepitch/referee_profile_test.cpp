// Tests for referee personalities and VAR review triggers described in
// SIMULATION_IMPROVEMENT_PROPOSAL.md section 4B.

#include <gtest/gtest.h>

#include "onthepitch/refereeprofile.hpp"

TEST(RefereeProfileParseTest, RecognizesProfileNamesCaseInsensitively) {
  EXPECT_EQ(RefereeProfile::Parse("strict"), RefereeProfile::e_Profile_Strict);
  EXPECT_EQ(RefereeProfile::Parse("LENIENT"), RefereeProfile::e_Profile_Lenient);
  EXPECT_EQ(RefereeProfile::Parse("Standard"), RefereeProfile::e_Profile_Standard);
}

TEST(RefereeProfileParseTest, UnknownNamesFallBackToStandard) {
  EXPECT_EQ(RefereeProfile::Parse(""), RefereeProfile::e_Profile_Standard);
  EXPECT_EQ(RefereeProfile::Parse("pierluigi"), RefereeProfile::e_Profile_Standard);
}

TEST(RefereeProfileParseTest, CanonicalNamesRoundTrip) {
  for (int i = 0; i < RefereeProfile::e_Profile_Count; i++) {
    const RefereeProfile::e_Profile profile = static_cast<RefereeProfile::e_Profile>(i);
    EXPECT_EQ(RefereeProfile::Parse(RefereeProfile::GetName(profile)), profile);
  }
}

// The standard profile must reproduce the thresholds that were hard-coded in
// Referee::TripNotice, so default matches referee exactly as before.
TEST(RefereeThresholdTest, StandardProfileMatchesTheHistoricalThresholds) {
  const RefereeProfile::Thresholds thresholds =
      RefereeProfile::GetThresholds(RefereeProfile::e_Profile_Standard);
  EXPECT_FLOAT_EQ(thresholds.foul, 1.0f);
  EXPECT_FLOAT_EQ(thresholds.yellow, 1.4f);
  EXPECT_FLOAT_EQ(thresholds.red, 2.0f);
}

TEST(RefereeThresholdTest, StrictWhistlesEarlierAndLenientLater) {
  const RefereeProfile::Thresholds strict =
      RefereeProfile::GetThresholds(RefereeProfile::e_Profile_Strict);
  const RefereeProfile::Thresholds standard =
      RefereeProfile::GetThresholds(RefereeProfile::e_Profile_Standard);
  const RefereeProfile::Thresholds lenient =
      RefereeProfile::GetThresholds(RefereeProfile::e_Profile_Lenient);

  EXPECT_LT(strict.foul, standard.foul);
  EXPECT_GT(lenient.foul, standard.foul);
  EXPECT_LT(strict.yellow, lenient.yellow);
  EXPECT_LT(strict.red, lenient.red);
}

TEST(RefereeThresholdTest, EveryProfileKeepsThresholdsOrdered) {
  for (int i = 0; i < RefereeProfile::e_Profile_Count; i++) {
    const RefereeProfile::Thresholds thresholds =
        RefereeProfile::GetThresholds(static_cast<RefereeProfile::e_Profile>(i));
    EXPECT_GT(thresholds.foul, 0.0f) << "profile " << i;
    EXPECT_LT(thresholds.foul, thresholds.yellow) << "profile " << i;
    EXPECT_LT(thresholds.yellow, thresholds.red) << "profile " << i;
  }
}

TEST(RefereeFoulTypeTest, MapsSeverityOntoTheFoulTypeLadder) {
  const RefereeProfile::e_Profile profile = RefereeProfile::e_Profile_Standard;
  EXPECT_EQ(RefereeProfile::GetFoulType(profile, 0.9f), 0);
  EXPECT_EQ(RefereeProfile::GetFoulType(profile, 1.2f), 1);
  EXPECT_EQ(RefereeProfile::GetFoulType(profile, 1.5f), 2);
  EXPECT_EQ(RefereeProfile::GetFoulType(profile, 2.5f), 3);
}

TEST(RefereeFoulTypeTest, TheSameTackleIsJudgedDifferentlyByDifferentReferees) {
  const float severity = 1.05f;
  EXPECT_EQ(RefereeProfile::GetFoulType(RefereeProfile::e_Profile_Lenient, severity), 0);
  EXPECT_GE(RefereeProfile::GetFoulType(RefereeProfile::e_Profile_Standard, severity), 1);
  EXPECT_GE(RefereeProfile::GetFoulType(RefereeProfile::e_Profile_Strict, severity), 1);
}

TEST(RefereeAdvantageTest, LenientRefereesLetPlayFlowLongerThanStrictOnes) {
  EXPECT_GT(RefereeProfile::GetAdvantageWindow_ms(RefereeProfile::e_Profile_Lenient),
            RefereeProfile::GetAdvantageWindow_ms(RefereeProfile::e_Profile_Strict));
  // The standard referee keeps the historical three-second advantage window.
  EXPECT_EQ(RefereeProfile::GetAdvantageWindow_ms(RefereeProfile::e_Profile_Standard), 3000UL);
}

TEST(VarOffsideTest, ReviewsOnlyTightOffsideDecisionsThatProducedAGoal) {
  EXPECT_TRUE(RefereeProfile::ShouldReviewOffside(0.2f, true));
  EXPECT_TRUE(RefereeProfile::ShouldReviewOffside(-0.2f, true));
  // A clear-cut margin needs no second look.
  EXPECT_FALSE(RefereeProfile::ShouldReviewOffside(3.0f, true));
  // Nothing to review if nothing came of it.
  EXPECT_FALSE(RefereeProfile::ShouldReviewOffside(0.2f, false));
}

TEST(VarPenaltyTest, ReviewsBorderlineContactInsideTheBox) {
  const RefereeProfile::e_Profile profile = RefereeProfile::e_Profile_Standard;
  const float threshold = RefereeProfile::GetThresholds(profile).foul;

  EXPECT_TRUE(RefereeProfile::ShouldReviewPenalty(profile, threshold + 0.05f, true));
  EXPECT_TRUE(RefereeProfile::ShouldReviewPenalty(profile, threshold - 0.05f, true));
  // A blatant hack does not need a review, and neither does contact outside the box.
  EXPECT_FALSE(RefereeProfile::ShouldReviewPenalty(profile, threshold + 1.5f, true));
  EXPECT_FALSE(RefereeProfile::ShouldReviewPenalty(profile, threshold + 0.05f, false));
}

TEST(VarPenaltyTest, ReviewWindowIsLongEnoughToShowACutscene) {
  EXPECT_GE(RefereeProfile::varReviewDuration_ms, 5000UL);
  EXPECT_LE(RefereeProfile::varReviewDuration_ms, 60000UL);
}

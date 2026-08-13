// Tests for the adaptive crowd described in SIMULATION_IMPROVEMENT_PROPOSAL.md
// section 5A. The existing crowd audio reacts to goals and to play near the
// goal; this adds the possession-driven roar of the home support.

#include <gtest/gtest.h>

#include "onthepitch/crowdmood.hpp"

// MatchData::GetPossessionFactor_60seconds() returns 0 when team 0 has had all
// the ball and 1 when team 1 has.
TEST(CrowdSupportTest, HomeSupportTracksTheHomeTeamShareOfTheBall) {
  EXPECT_FLOAT_EQ(CrowdMood::GetHomeSupportFactor(0.0f, 0), 1.0f);
  EXPECT_FLOAT_EQ(CrowdMood::GetHomeSupportFactor(1.0f, 0), 0.0f);
  EXPECT_FLOAT_EQ(CrowdMood::GetHomeSupportFactor(0.5f, 0), 0.5f);
}

TEST(CrowdSupportTest, WorksWhenTheHomeTeamIsTeamOne) {
  EXPECT_FLOAT_EQ(CrowdMood::GetHomeSupportFactor(1.0f, 1), 1.0f);
  EXPECT_FLOAT_EQ(CrowdMood::GetHomeSupportFactor(0.0f, 1), 0.0f);
}

TEST(CrowdSupportTest, ClampsNonsensicalPossessionValues) {
  EXPECT_FLOAT_EQ(CrowdMood::GetHomeSupportFactor(-1.0f, 0), 1.0f);
  EXPECT_FLOAT_EQ(CrowdMood::GetHomeSupportFactor(2.0f, 0), 0.0f);
}

TEST(CrowdExcitementTest, HomeDominanceLiftsTheCrowdAndAwayDominanceQuietensIt) {
  const float dominant = CrowdMood::GetPossessionExcitement(0.0f, 0);
  const float even = CrowdMood::GetPossessionExcitement(0.5f, 0);
  const float dominated = CrowdMood::GetPossessionExcitement(1.0f, 0);

  EXPECT_GT(dominant, even);
  EXPECT_GT(even, dominated);
  EXPECT_GE(dominated, 0.0f);
  EXPECT_LE(dominant, 1.0f);
}

TEST(CrowdExcitementTest, AnEvenGameLeavesTheCrowdNeitherRoaringNorSilent) {
  const float even = CrowdMood::GetPossessionExcitement(0.5f, 0);
  EXPECT_GT(even, 0.0f);
  EXPECT_LT(even, 0.5f);
}

TEST(CrowdBlendTest, NeverQuietensACrowdThatIsAlreadyLoud) {
  // A goal has the base excitement at full tilt; possession must not damp it.
  EXPECT_FLOAT_EQ(CrowdMood::Blend(1.0f, 0.0f), 1.0f);
  EXPECT_GE(CrowdMood::Blend(0.8f, 0.1f), 0.8f);
}

TEST(CrowdBlendTest, RaisesAQuietCrowdWhenTheHomeSideIsOnTop) {
  EXPECT_GT(CrowdMood::Blend(0.15f, 0.6f), 0.15f);
}

TEST(CrowdBlendTest, StaysWithinTheGainRange) {
  for (float base = 0.0f; base <= 1.0f; base += 0.25f) {
    for (float possession = 0.0f; possession <= 1.0f; possession += 0.25f) {
      const float blended = CrowdMood::Blend(base, possession);
      EXPECT_GE(blended, 0.0f);
      EXPECT_LE(blended, 1.0f);
    }
  }
}

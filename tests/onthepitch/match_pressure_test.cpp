// Tests for mental fatigue and pressure (clutch performers, panicking players)
// described in SIMULATION_IMPROVEMENT_PROPOSAL.md section 3B.

#include <gtest/gtest.h>

#include "onthepitch/matchpressure.hpp"

namespace {

constexpr unsigned long Minutes(float minutes) {
  return static_cast<unsigned long>(minutes * 60000.0f);
}

}  // namespace

TEST(MatchPressureSituationTest, ACloseGameIsWithinOneGoal) {
  EXPECT_TRUE(MatchPressure::IsCloseGame(0));
  EXPECT_TRUE(MatchPressure::IsCloseGame(1));
  EXPECT_TRUE(MatchPressure::IsCloseGame(-1));
  EXPECT_FALSE(MatchPressure::IsCloseGame(2));
  EXPECT_FALSE(MatchPressure::IsCloseGame(-3));
}

TEST(MatchPressureSituationTest, TheFinalTenMinutesStartAtEighty) {
  EXPECT_FALSE(MatchPressure::IsFinalTenMinutes(Minutes(79.9f)));
  EXPECT_TRUE(MatchPressure::IsFinalTenMinutes(Minutes(80)));
  EXPECT_TRUE(MatchPressure::IsFinalTenMinutes(Minutes(90)));
}

TEST(ClutchFactorTest, ResilientPlayersGainFivePercentLateInACloseGame) {
  EXPECT_FLOAT_EQ(MatchPressure::GetClutchTechnicalMultiplier(0.9f, 0, Minutes(85)), 1.05f);
  EXPECT_FLOAT_EQ(MatchPressure::GetClutchTechnicalMultiplier(0.9f, -1, Minutes(89)), 1.05f);
}

TEST(ClutchFactorTest, NoBonusBeforeTheFinalTenMinutes) {
  EXPECT_FLOAT_EQ(MatchPressure::GetClutchTechnicalMultiplier(0.9f, 0, Minutes(70)), 1.0f);
}

TEST(ClutchFactorTest, NoBonusWhenTheGameIsAlreadyDecided) {
  EXPECT_FLOAT_EQ(MatchPressure::GetClutchTechnicalMultiplier(0.9f, 3, Minutes(85)), 1.0f);
  EXPECT_FLOAT_EQ(MatchPressure::GetClutchTechnicalMultiplier(0.9f, -4, Minutes(85)), 1.0f);
}

TEST(ClutchFactorTest, OrdinaryTemperamentsGetNothing) {
  EXPECT_FLOAT_EQ(MatchPressure::GetClutchTechnicalMultiplier(0.5f, 0, Minutes(85)), 1.0f);
  EXPECT_FLOAT_EQ(MatchPressure::GetClutchTechnicalMultiplier(
                      MatchPressure::clutchResilienceThreshold - 0.01f, 0, Minutes(85)),
                  1.0f);
}

TEST(PanicTest, NoStumbleRiskUntilTwoOpponentsClose) {
  EXPECT_FLOAT_EQ(MatchPressure::GetStumbleChance(0.1f, 18, 0), 0.0f);
  EXPECT_FLOAT_EQ(MatchPressure::GetStumbleChance(0.1f, 18, 1), 0.0f);
  EXPECT_GT(MatchPressure::GetStumbleChance(0.1f, 18, 2), 0.0f);
}

TEST(PanicTest, CalmPlayersStumbleLessThanNervousOnes) {
  const float calm = MatchPressure::GetStumbleChance(0.95f, 28, 3);
  const float nervous = MatchPressure::GetStumbleChance(0.15f, 28, 3);
  EXPECT_LT(calm, nervous);
}

TEST(PanicTest, YoungPlayersStumbleMoreThanExperiencedEquals) {
  const float youngster = MatchPressure::GetStumbleChance(0.5f, 18, 2);
  const float veteran = MatchPressure::GetStumbleChance(0.5f, 30, 2);
  EXPECT_GT(youngster, veteran);
}

TEST(PanicTest, UnknownAgeIsTreatedAsExperienced) {
  EXPECT_FLOAT_EQ(MatchPressure::GetStumbleChance(0.5f, MatchPressure::unknownAge, 2),
                  MatchPressure::GetStumbleChance(0.5f, 30, 2));
}

TEST(PanicTest, MoreOpponentsMeanMorePressure) {
  EXPECT_GT(MatchPressure::GetStumbleChance(0.5f, 25, 4),
            MatchPressure::GetStumbleChance(0.5f, 25, 2));
}

TEST(PanicTest, StumbleChanceStaysAProbability) {
  for (int opponents = 0; opponents <= 6; opponents++) {
    for (float calmness = 0.0f; calmness <= 1.0f; calmness += 0.25f) {
      const float chance = MatchPressure::GetStumbleChance(calmness, 16, opponents);
      EXPECT_GE(chance, 0.0f);
      EXPECT_LE(chance, MatchPressure::maxStumbleChance);
    }
  }
}

TEST(PanicTest, TheStumbleRollComparesTheSampleAgainstTheChance) {
  EXPECT_TRUE(MatchPressure::ShouldStumble(0.4f, 0.39f));
  EXPECT_FALSE(MatchPressure::ShouldStumble(0.4f, 0.4f));
  EXPECT_FALSE(MatchPressure::ShouldStumble(0.0f, 0.0f));
}

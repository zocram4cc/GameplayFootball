// Tests for the reactive mentality shifts described in
// SIMULATION_IMPROVEMENT_PROPOSAL.md section 2B.

#include <gtest/gtest.h>

#include "onthepitch/matchmentality.hpp"

namespace {

constexpr unsigned long Minutes(float minutes) {
  return static_cast<unsigned long>(minutes * 60000.0f);
}

}  // namespace

TEST(MatchMentalityTest, StaysNormalDuringTheBulkOfTheMatch) {
  EXPECT_EQ(MatchMentality::Decide(-2, Minutes(20)), MatchMentality::e_Mentality_Normal);
  EXPECT_EQ(MatchMentality::Decide(0, Minutes(45)), MatchMentality::e_Mentality_Normal);
  EXPECT_EQ(MatchMentality::Decide(3, Minutes(79)), MatchMentality::e_Mentality_Normal);
}

TEST(MatchMentalityTest, TrailingAfterEightyMinutesTriggersDesperation) {
  EXPECT_EQ(MatchMentality::Decide(-1, Minutes(80)), MatchMentality::e_Mentality_Desperation);
  EXPECT_EQ(MatchMentality::Decide(-3, Minutes(88)), MatchMentality::e_Mentality_Desperation);
  // One minute early is still business as usual.
  EXPECT_EQ(MatchMentality::Decide(-1, Minutes(79.9f)), MatchMentality::e_Mentality_Normal);
}

TEST(MatchMentalityTest, LeadingAfterEightyFiveMinutesTriggersTimeWasting) {
  EXPECT_EQ(MatchMentality::Decide(1, Minutes(85)), MatchMentality::e_Mentality_TimeWasting);
  EXPECT_EQ(MatchMentality::Decide(2, Minutes(92)), MatchMentality::e_Mentality_TimeWasting);
  // Leading at 82 minutes is too early to start killing the game.
  EXPECT_EQ(MatchMentality::Decide(1, Minutes(82)), MatchMentality::e_Mentality_Normal);
}

TEST(MatchMentalityTest, LevelScoreLateNeverWastesTime) {
  EXPECT_EQ(MatchMentality::Decide(0, Minutes(88)), MatchMentality::e_Mentality_Normal);
  EXPECT_EQ(MatchMentality::Decide(0, Minutes(120)), MatchMentality::e_Mentality_Normal);
}

TEST(MatchMentalityTest, DesperationPersistsIntoExtraTime) {
  EXPECT_EQ(MatchMentality::Decide(-1, Minutes(105)), MatchMentality::e_Mentality_Desperation);
}

TEST(MatchMentalityShapeTest, OnlyDesperationOverridesTheFormation) {
  EXPECT_FALSE(MatchMentality::OverridesFormation(MatchMentality::e_Mentality_Normal));
  EXPECT_FALSE(MatchMentality::OverridesFormation(MatchMentality::e_Mentality_TimeWasting));
  EXPECT_TRUE(MatchMentality::OverridesFormation(MatchMentality::e_Mentality_Desperation));
}

TEST(MatchMentalityShapeTest, OneGoalDownGoesThreeFourThree) {
  const MatchMentality::FormationShape shape = MatchMentality::GetDesperationShape(-1);
  EXPECT_EQ(shape.defenders, 3);
  EXPECT_EQ(shape.midfielders, 4);
  EXPECT_EQ(shape.forwards, 3);
}

TEST(MatchMentalityShapeTest, TwoOrMoreGoalsDownGoesFourTwoFour) {
  for (int goalDifference = -2; goalDifference >= -5; goalDifference--) {
    const MatchMentality::FormationShape shape =
        MatchMentality::GetDesperationShape(goalDifference);
    EXPECT_EQ(shape.defenders, 4) << "goal difference " << goalDifference;
    EXPECT_EQ(shape.midfielders, 2) << "goal difference " << goalDifference;
    EXPECT_EQ(shape.forwards, 4) << "goal difference " << goalDifference;
  }
}

TEST(MatchMentalityShapeTest, EveryDesperationShapeFieldsTenOutfieldPlayers) {
  for (int goalDifference = -1; goalDifference >= -6; goalDifference--) {
    const MatchMentality::FormationShape shape =
        MatchMentality::GetDesperationShape(goalDifference);
    EXPECT_EQ(shape.defenders + shape.midfielders + shape.forwards, 10)
        << "goal difference " << goalDifference;
  }
}

TEST(MatchMentalityCornerTest, TimeWastingSendsThePossessionPlayerToTheCorner) {
  EXPECT_TRUE(MatchMentality::ShouldStayInCorner(MatchMentality::e_Mentality_TimeWasting, true));
  EXPECT_FALSE(MatchMentality::ShouldStayInCorner(MatchMentality::e_Mentality_TimeWasting, false));
  EXPECT_FALSE(MatchMentality::ShouldStayInCorner(MatchMentality::e_Mentality_Normal, true));
  EXPECT_FALSE(MatchMentality::ShouldStayInCorner(MatchMentality::e_Mentality_Desperation, true));
}

// The corner the ball is taken to must be in the opponent's half, on the side
// the player is already closest to, and inside the pitch markings.
TEST(MatchMentalityCornerTest, CornerTargetIsInTheOpponentHalfOnTheNearestFlank) {
  const MatchMentality::CornerTarget target = MatchMentality::GetCornerTarget(1, 12.0f);
  EXPECT_LT(target.x, 0.0f);  // team side +1 attacks -x
  EXPECT_GT(target.y, 0.0f);  // player is on the +y flank
  EXPECT_LT(std::abs(target.x), 55.0f);
  EXPECT_LT(std::abs(target.y), 36.0f);

  const MatchMentality::CornerTarget mirrored = MatchMentality::GetCornerTarget(-1, -12.0f);
  EXPECT_GT(mirrored.x, 0.0f);
  EXPECT_LT(mirrored.y, 0.0f);
}

// The late-game shift must not overrule the manager: it is added to whatever
// the user's tactics produced, as momentum.
TEST(MatchMentalityMomentumTest, NormalPlayAddsNoMomentum) {
  EXPECT_FLOAT_EQ(MatchMentality::GetOffensiveMomentum(MatchMentality::e_Mentality_Normal), 0.0f);
}

TEST(MatchMentalityMomentumTest, DesperationPushesForwardAndTimeWastingPullsBack) {
  EXPECT_GT(MatchMentality::GetOffensiveMomentum(MatchMentality::e_Mentality_Desperation), 0.0f);
  EXPECT_LT(MatchMentality::GetOffensiveMomentum(MatchMentality::e_Mentality_TimeWasting), 0.0f);
}

TEST(MatchMentalityMomentumTest, MomentumIsAnOffsetNotATakeover) {
  // Small enough that a defensive manager stays recognisably defensive.
  EXPECT_LE(std::abs(MatchMentality::GetOffensiveMomentum(MatchMentality::e_Mentality_Desperation)),
            0.4f);
  EXPECT_LE(std::abs(MatchMentality::GetOffensiveMomentum(MatchMentality::e_Mentality_TimeWasting)),
            0.4f);
}

TEST(MatchMentalityMomentumTest, ApplyingMomentumSumsOntoTheUsersValueAndStaysNormalized) {
  const float userValue = 0.5f;
  const float pushed =
      MatchMentality::ApplyMomentum(userValue, MatchMentality::e_Mentality_Desperation, 1.0f);
  const float held =
      MatchMentality::ApplyMomentum(userValue, MatchMentality::e_Mentality_TimeWasting, 1.0f);

  EXPECT_GT(pushed, userValue);
  EXPECT_LT(held, userValue);
  EXPECT_FLOAT_EQ(
      MatchMentality::ApplyMomentum(userValue, MatchMentality::e_Mentality_Normal, 1.0f),
      userValue);

  // Never escapes the slider range, whatever the user asked for.
  EXPECT_LE(MatchMentality::ApplyMomentum(1.0f, MatchMentality::e_Mentality_Desperation, 1.0f),
            1.0f);
  EXPECT_GE(MatchMentality::ApplyMomentum(0.0f, MatchMentality::e_Mentality_TimeWasting, 1.0f),
            0.0f);
}

TEST(MatchMentalityMomentumTest, WeightScalesHowMuchOfTheMomentumIsApplied) {
  const float full =
      MatchMentality::ApplyMomentum(0.5f, MatchMentality::e_Mentality_Desperation, 1.0f);
  const float half =
      MatchMentality::ApplyMomentum(0.5f, MatchMentality::e_Mentality_Desperation, 0.5f);

  EXPECT_GT(full, half);
  EXPECT_GT(half, 0.5f);
  EXPECT_FLOAT_EQ(
      MatchMentality::ApplyMomentum(0.5f, MatchMentality::e_Mentality_Desperation, 0.0f), 0.5f);
}

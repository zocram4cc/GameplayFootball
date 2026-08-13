// Tests for the match phase progression (TECHNICAL_ROADMAP.md checklist item 1:
// "Fix Match State: clean up extra time logic in Referee").

#include <gtest/gtest.h>

#include "onthepitch/matchprogression.hpp"

TEST(MatchProgressionTest, HalfTimeAlwaysLeadsToTheSecondHalf) {
  const MatchProgression::Outcome level = MatchProgression::GetNext(e_MatchPhase_1stHalf, true);
  const MatchProgression::Outcome ahead = MatchProgression::GetNext(e_MatchPhase_1stHalf, false);

  EXPECT_FALSE(level.gameOver);
  EXPECT_EQ(level.nextPhase, e_MatchPhase_2ndHalf);
  EXPECT_FALSE(ahead.gameOver);
  EXPECT_EQ(ahead.nextPhase, e_MatchPhase_2ndHalf);
}

TEST(MatchProgressionTest, ADecidedMatchEndsAtFullTime) {
  const MatchProgression::Outcome outcome = MatchProgression::GetNext(e_MatchPhase_2ndHalf, false);
  EXPECT_TRUE(outcome.gameOver);
}

TEST(MatchProgressionTest, ADrawAtFullTimeGoesToExtraTime) {
  const MatchProgression::Outcome outcome = MatchProgression::GetNext(e_MatchPhase_2ndHalf, true);
  EXPECT_FALSE(outcome.gameOver);
  EXPECT_EQ(outcome.nextPhase, e_MatchPhase_1stExtraTime);
}

// The break between the two extra-time periods is not an opportunity to end the
// match, whatever the score is: both periods are always played out.
TEST(MatchProgressionTest, TheExtraTimeBreakNeverEndsTheMatch) {
  for (bool level : {true, false}) {
    const MatchProgression::Outcome outcome =
        MatchProgression::GetNext(e_MatchPhase_1stExtraTime, level);
    EXPECT_FALSE(outcome.gameOver) << "level: " << level;
    EXPECT_EQ(outcome.nextPhase, e_MatchPhase_2ndExtraTime);
  }
}

TEST(MatchProgressionTest, AWinnerAfterExtraTimeEndsTheMatchWithoutPenalties) {
  const MatchProgression::Outcome outcome =
      MatchProgression::GetNext(e_MatchPhase_2ndExtraTime, false);
  EXPECT_TRUE(outcome.gameOver);
}

TEST(MatchProgressionTest, StillLevelAfterExtraTimeMeansPenalties) {
  const MatchProgression::Outcome outcome =
      MatchProgression::GetNext(e_MatchPhase_2ndExtraTime, true);
  EXPECT_FALSE(outcome.gameOver);
  EXPECT_EQ(outcome.nextPhase, e_MatchPhase_Penalties);
}

TEST(MatchProgressionTest, ThePenaltiesPhaseIsLeftToTheShootout) {
  const MatchProgression::Outcome outcome = MatchProgression::GetNext(e_MatchPhase_Penalties, true);
  EXPECT_FALSE(outcome.gameOver);
  EXPECT_EQ(outcome.nextPhase, e_MatchPhase_Penalties);
}

// --- Blowing for the end of a period ---
//
// The referee waits for a neutral moment, but a period can never run away: past
// the maximum stoppage the whistle goes whatever is happening.

TEST(PeriodEndTest, NeverEndsAPeriodEarly) {
  EXPECT_FALSE(MatchProgression::ShouldEndPeriod(2600000, 2700000, 0.0f));
  EXPECT_FALSE(MatchProgression::ShouldEndPeriod(2699999, 2700000, 0.0f));
}

TEST(PeriodEndTest, EndsThePeriodWhenPlayReachesTheMiddleOfThePitch) {
  EXPECT_TRUE(MatchProgression::ShouldEndPeriod(2700001, 2700000, 0.0f));
  EXPECT_TRUE(MatchProgression::ShouldEndPeriod(2700001, 2700000, 9.0f));
  EXPECT_TRUE(MatchProgression::ShouldEndPeriod(2700001, 2700000, -9.0f));
}

TEST(PeriodEndTest, WaitsWhileTheBallIsUpTheOtherEnd) {
  EXPECT_FALSE(MatchProgression::ShouldEndPeriod(2700001, 2700000, 40.0f));
  EXPECT_FALSE(MatchProgression::ShouldEndPeriod(2700001, 2700000, -40.0f));
}

TEST(PeriodEndTest, BlowsUpRegardlessOnceStoppageTimeIsExhausted) {
  const unsigned long wayOver = 2700000 + MatchProgression::maxStoppageTime_ms + 1;
  EXPECT_TRUE(MatchProgression::ShouldEndPeriod(wayOver, 2700000, 40.0f));
  EXPECT_TRUE(MatchProgression::ShouldEndPeriod(wayOver, 2700000, -50.0f));
}

TEST(PeriodEndTest, TheStoppageAllowanceIsARealisticFewMinutes) {
  EXPECT_GE(MatchProgression::maxStoppageTime_ms, 60UL * 1000UL);
  EXPECT_LE(MatchProgression::maxStoppageTime_ms, 6UL * 60UL * 1000UL);
}

TEST(PeriodEndTest, KnowsWhenEachPeriodIsDue) {
  EXPECT_EQ(MatchProgression::GetPeriodEndTime_ms(e_MatchPhase_1stHalf), 2700000UL);
  EXPECT_EQ(MatchProgression::GetPeriodEndTime_ms(e_MatchPhase_2ndHalf), 5400000UL);
  EXPECT_EQ(MatchProgression::GetPeriodEndTime_ms(e_MatchPhase_1stExtraTime), 6300000UL);
  EXPECT_EQ(MatchProgression::GetPeriodEndTime_ms(e_MatchPhase_2ndExtraTime), 7200000UL);
}

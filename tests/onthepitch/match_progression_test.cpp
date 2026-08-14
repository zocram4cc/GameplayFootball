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

// --- Added time (Law 7: allowance for time lost) ---
//
// The clock stops during every restart, so the allowance is accrued per
// discrete event: goals, substitutions, cards and injuries
// (docs/RULESET_AUDIT.md gap 5).

TEST(AddedTimeTest, EachKindOfTimeLossAccrues) {
  MatchProgression::Stoppage stoppage;
  EXPECT_EQ(stoppage.accrued_ms, 0UL);

  MatchProgression::AddStoppage(stoppage, MatchProgression::e_Stoppage_Goal);
  EXPECT_EQ(stoppage.accrued_ms, MatchProgression::stoppagePerGoal_ms);

  MatchProgression::AddStoppage(stoppage, MatchProgression::e_Stoppage_Substitution);
  MatchProgression::AddStoppage(stoppage, MatchProgression::e_Stoppage_Card);
  MatchProgression::AddStoppage(stoppage, MatchProgression::e_Stoppage_Injury);
  EXPECT_EQ(stoppage.accrued_ms, MatchProgression::stoppagePerGoal_ms +
                                     MatchProgression::stoppagePerSubstitution_ms +
                                     MatchProgression::stoppagePerCard_ms +
                                     MatchProgression::stoppagePerInjury_ms);
}

TEST(AddedTimeTest, TheAllowanceIsRealistic) {
  // A goal is worth about a minute of celebration; the others about half.
  EXPECT_EQ(MatchProgression::stoppagePerGoal_ms, 60000UL);
  EXPECT_EQ(MatchProgression::stoppagePerSubstitution_ms, 30000UL);
  EXPECT_EQ(MatchProgression::stoppagePerCard_ms, 30000UL);
  EXPECT_EQ(MatchProgression::stoppagePerInjury_ms, 30000UL);
}

TEST(AddedTimeTest, TheAllowanceIsCapped) {
  MatchProgression::Stoppage stoppage;
  for (int i = 0; i < 100; i++)
    MatchProgression::AddStoppage(stoppage, MatchProgression::e_Stoppage_Goal);
  EXPECT_EQ(stoppage.accrued_ms, MatchProgression::maxStoppageTime_ms);
}

TEST(AddedTimeTest, ThePeriodRunsLongerByExactlyTheAllowance) {
  MatchProgression::Stoppage stoppage;
  MatchProgression::AddStoppage(stoppage, MatchProgression::e_Stoppage_Goal);

  const unsigned long endWith =
      MatchProgression::GetPeriodEndTime_ms(e_MatchPhase_1stHalf, stoppage);
  EXPECT_EQ(endWith, 2700000UL + MatchProgression::stoppagePerGoal_ms);

  // The referee still waits for a neutral moment measured from the new end.
  EXPECT_FALSE(MatchProgression::ShouldEndPeriod(2700001, endWith, 0.0f));
  EXPECT_TRUE(MatchProgression::ShouldEndPeriod(endWith + 1, endWith, 0.0f));
}

TEST(AddedTimeTest, AnnouncedMinutesRoundUpAndNeverVanish) {
  MatchProgression::Stoppage stoppage;
  EXPECT_EQ(MatchProgression::GetAnnouncedAddedMinutes(stoppage), 0);

  stoppage.accrued_ms = 1000;  // any loss at all is announced as +1
  EXPECT_EQ(MatchProgression::GetAnnouncedAddedMinutes(stoppage), 1);

  stoppage.accrued_ms = 60000;
  EXPECT_EQ(MatchProgression::GetAnnouncedAddedMinutes(stoppage), 1);

  stoppage.accrued_ms = 61000;
  EXPECT_EQ(MatchProgression::GetAnnouncedAddedMinutes(stoppage), 2);
}

// --- The scoreboard clock ---

TEST(AddedTimeClockTest, RegulationTimeIsPlainMinutesAndSeconds) {
  EXPECT_EQ(MatchProgression::FormatClock(0, 2700000), "00:00");
  EXPECT_EQ(MatchProgression::FormatClock(65000, 2700000), "01:05");
  EXPECT_EQ(MatchProgression::FormatClock(2699999, 2700000), "44:59");
  EXPECT_EQ(MatchProgression::FormatClock(2700000, 2700000), "45:00");
}

TEST(AddedTimeClockTest, StoppageTimeShowsThePeriodEndPlusOvertime) {
  EXPECT_EQ(MatchProgression::FormatClock(2700001, 2700000), "45:00 +0:00");
  EXPECT_EQ(MatchProgression::FormatClock(2712000, 2700000), "45:00 +0:12");
  EXPECT_EQ(MatchProgression::FormatClock(5460500, 5400000), "90:00 +1:00");
}

TEST(AddedTimeClockTest, PhasesWithoutAScheduledEndTickPlainly) {
  // Pre-match and penalties report a scheduled end of 0.
  EXPECT_EQ(MatchProgression::FormatClock(7200000, 0), "120:00");
}

// Tests for the advanced statistics described in
// SIMULATION_IMPROVEMENT_PROPOSAL.md section 5B: expected goals from the shot
// context, and heatmaps accumulated from ball positions.

#include <gtest/gtest.h>

#include "data/matchanalytics.hpp"
#include "gamedefines.hpp"

using blunted::Vector3;

namespace {

MatchAnalytics::ShotContext CentralShot(float distance) {
  MatchAnalytics::ShotContext context;
  context.distance = distance;
  context.angleFactor = 1.0f;
  return context;
}

}  // namespace

TEST(ExpectedGoalsTest, CloseCentralShotsAreLikelyAndLongRangeEffortsAreNot) {
  EXPECT_GT(MatchAnalytics::CalculateExpectedGoals(CentralShot(6.0f)), 0.4f);
  EXPECT_LT(MatchAnalytics::CalculateExpectedGoals(CentralShot(30.0f)), 0.1f);
}

TEST(ExpectedGoalsTest, ChanceQualityFallsOffWithDistance) {
  float previous = 1.0f;
  for (float distance = 4.0f; distance <= 36.0f; distance += 4.0f) {
    const float xg = MatchAnalytics::CalculateExpectedGoals(CentralShot(distance));
    EXPECT_LT(xg, previous) << "distance " << distance;
    previous = xg;
  }
}

TEST(ExpectedGoalsTest, TightAnglesAreWorthLessThanShotsFromTheMiddle) {
  MatchAnalytics::ShotContext tight = CentralShot(12.0f);
  tight.angleFactor = 0.15f;
  EXPECT_LT(MatchAnalytics::CalculateExpectedGoals(tight),
            MatchAnalytics::CalculateExpectedGoals(CentralShot(12.0f)));
}

TEST(ExpectedGoalsTest, DefendersInTheWayReduceTheChance) {
  MatchAnalytics::ShotContext crowded = CentralShot(12.0f);
  crowded.defendersInPath = 2;

  MatchAnalytics::ShotContext blocked = CentralShot(12.0f);
  blocked.defendersInPath = 4;

  const float clear = MatchAnalytics::CalculateExpectedGoals(CentralShot(12.0f));
  EXPECT_LT(MatchAnalytics::CalculateExpectedGoals(crowded), clear);
  EXPECT_LT(MatchAnalytics::CalculateExpectedGoals(blocked),
            MatchAnalytics::CalculateExpectedGoals(crowded));
}

TEST(ExpectedGoalsTest, HeadersAreHarderThanShotsWithTheFoot) {
  MatchAnalytics::ShotContext header = CentralShot(8.0f);
  header.isHeader = true;
  EXPECT_LT(MatchAnalytics::CalculateExpectedGoals(header),
            MatchAnalytics::CalculateExpectedGoals(CentralShot(8.0f)));
}

TEST(ExpectedGoalsTest, AGoodSpotRatingIsWorthMoreThanAPoorOne) {
  MatchAnalytics::ShotContext good = CentralShot(14.0f);
  good.spotRating = 0.9f;

  MatchAnalytics::ShotContext poor = CentralShot(14.0f);
  poor.spotRating = 0.1f;

  EXPECT_GT(MatchAnalytics::CalculateExpectedGoals(good),
            MatchAnalytics::CalculateExpectedGoals(poor));
}

TEST(ExpectedGoalsTest, EveryShotIsAProbabilityShortOfCertainty) {
  for (float distance = 1.0f; distance <= 60.0f; distance += 3.0f) {
    for (int defenders = 0; defenders <= 5; defenders++) {
      MatchAnalytics::ShotContext context = CentralShot(distance);
      context.defendersInPath = defenders;
      const float xg = MatchAnalytics::CalculateExpectedGoals(context);
      EXPECT_GT(xg, 0.0f);
      EXPECT_LE(xg, MatchAnalytics::maxExpectedGoals);
    }
  }
}

TEST(ShotContextTest, DerivesDistanceAndAngleFromThePitchPosition) {
  // Team with side +1 attacks the -x goal.
  const MatchAnalytics::ShotContext straight =
      MatchAnalytics::MakeShotContext(Vector3(-pitchHalfW + 12.0f, 0.0f, 0.0f), 1, 0, false, 0.5f);
  EXPECT_NEAR(straight.distance, 12.0f, 0.01f);
  EXPECT_NEAR(straight.angleFactor, 1.0f, 0.01f);

  const MatchAnalytics::ShotContext fromTheByline =
      MatchAnalytics::MakeShotContext(Vector3(-pitchHalfW, 12.0f, 0.0f), 1, 0, false, 0.5f);
  EXPECT_NEAR(fromTheByline.distance, 12.0f, 0.01f);
  EXPECT_LT(fromTheByline.angleFactor, 0.1f);
}

TEST(ShotContextTest, MirrorsForTheTeamAttackingTheOtherWay) {
  const MatchAnalytics::ShotContext shot =
      MatchAnalytics::MakeShotContext(Vector3(pitchHalfW - 10.0f, 0.0f, 0.0f), -1, 1, true, 0.7f);
  EXPECT_NEAR(shot.distance, 10.0f, 0.01f);
  EXPECT_NEAR(shot.angleFactor, 1.0f, 0.01f);
  EXPECT_EQ(shot.defendersInPath, 1);
  EXPECT_TRUE(shot.isHeader);
  EXPECT_FLOAT_EQ(shot.spotRating, 0.7f);
}

TEST(ExpectedGoalsTallyTest, TracksExpectedGoalsAndShotsPerTeam) {
  MatchAnalytics::ShotTally tally;
  MatchAnalytics::AddShot(tally, 0, 0.4f);
  MatchAnalytics::AddShot(tally, 0, 0.1f);
  MatchAnalytics::AddShot(tally, 1, 0.25f);

  EXPECT_EQ(MatchAnalytics::GetShotCount(tally, 0), 2);
  EXPECT_EQ(MatchAnalytics::GetShotCount(tally, 1), 1);
  EXPECT_FLOAT_EQ(MatchAnalytics::GetExpectedGoals(tally, 0), 0.5f);
  EXPECT_FLOAT_EQ(MatchAnalytics::GetExpectedGoals(tally, 1), 0.25f);
}

TEST(ExpectedGoalsTallyTest, AFreshTallyIsEmpty) {
  const MatchAnalytics::ShotTally tally;
  EXPECT_EQ(MatchAnalytics::GetShotCount(tally, 0), 0);
  EXPECT_FLOAT_EQ(MatchAnalytics::GetExpectedGoals(tally, 0), 0.0f);
}

TEST(HeatmapTest, AFreshHeatmapIsEmpty) {
  const MatchAnalytics::Heatmap heatmap;
  EXPECT_EQ(heatmap.samples, 0);
  for (int y = 0; y < MatchAnalytics::Heatmap::cellsY; y++)
    for (int x = 0; x < MatchAnalytics::Heatmap::cellsX; x++)
      EXPECT_EQ(MatchAnalytics::GetCellCount(heatmap, x, y), 0);
}

TEST(HeatmapTest, TheCentreOfThePitchLandsInTheMiddleOfTheGrid) {
  MatchAnalytics::Heatmap heatmap;
  MatchAnalytics::AddSample(heatmap, Vector3(0.0f, 0.0f, 0.0f));

  EXPECT_EQ(heatmap.samples, 1);
  EXPECT_EQ(MatchAnalytics::GetCellCount(heatmap, MatchAnalytics::Heatmap::cellsX / 2,
                                         MatchAnalytics::Heatmap::cellsY / 2),
            1);
}

TEST(HeatmapTest, RepeatedVisitsAccumulateInTheSameCell) {
  MatchAnalytics::Heatmap heatmap;
  for (int i = 0; i < 3; i++)
    MatchAnalytics::AddSample(heatmap, Vector3(10.0f, 5.0f, 0.0f));

  EXPECT_EQ(heatmap.samples, 3);
  EXPECT_EQ(MatchAnalytics::GetCellCount(heatmap, MatchAnalytics::GetCellX(10.0f),
                                         MatchAnalytics::GetCellY(5.0f)),
            3);
}

TEST(HeatmapTest, OppositeCornersLandInOppositeCells) {
  EXPECT_EQ(MatchAnalytics::GetCellX(-pitchHalfW), 0);
  EXPECT_EQ(MatchAnalytics::GetCellX(pitchHalfW), MatchAnalytics::Heatmap::cellsX - 1);
  EXPECT_EQ(MatchAnalytics::GetCellY(-pitchHalfH), 0);
  EXPECT_EQ(MatchAnalytics::GetCellY(pitchHalfH), MatchAnalytics::Heatmap::cellsY - 1);
}

TEST(HeatmapTest, PositionsBeyondTheLinesClampIntoTheEdgeCells) {
  MatchAnalytics::Heatmap heatmap;
  MatchAnalytics::AddSample(heatmap, Vector3(-500.0f, -500.0f, 0.0f));
  MatchAnalytics::AddSample(heatmap, Vector3(500.0f, 500.0f, 0.0f));

  EXPECT_EQ(MatchAnalytics::GetCellCount(heatmap, 0, 0), 1);
  EXPECT_EQ(MatchAnalytics::GetCellCount(heatmap, MatchAnalytics::Heatmap::cellsX - 1,
                                         MatchAnalytics::Heatmap::cellsY - 1),
            1);
  EXPECT_EQ(heatmap.samples, 2);
}

TEST(HeatmapTest, NormalizedIntensityPeaksAtTheBusiestCell) {
  MatchAnalytics::Heatmap heatmap;
  MatchAnalytics::AddSample(heatmap, Vector3(0.0f, 0.0f, 0.0f));
  MatchAnalytics::AddSample(heatmap, Vector3(0.0f, 0.0f, 0.0f));
  MatchAnalytics::AddSample(heatmap, Vector3(-40.0f, -20.0f, 0.0f));

  const int busyX = MatchAnalytics::GetCellX(0.0f);
  const int busyY = MatchAnalytics::GetCellY(0.0f);
  EXPECT_FLOAT_EQ(MatchAnalytics::GetNormalizedIntensity(heatmap, busyX, busyY), 1.0f);
  EXPECT_FLOAT_EQ(MatchAnalytics::GetNormalizedIntensity(heatmap, MatchAnalytics::GetCellX(-40.0f),
                                                         MatchAnalytics::GetCellY(-20.0f)),
                  0.5f);
}

TEST(HeatmapTest, OutOfRangeCellQueriesAreSafe) {
  const MatchAnalytics::Heatmap heatmap;
  EXPECT_EQ(MatchAnalytics::GetCellCount(heatmap, -1, 0), 0);
  EXPECT_EQ(MatchAnalytics::GetCellCount(heatmap, MatchAnalytics::Heatmap::cellsX, 0), 0);
  EXPECT_FLOAT_EQ(MatchAnalytics::GetNormalizedIntensity(heatmap, -1, -1), 0.0f);
}

TEST(HeatmapTest, AnEmptyHeatmapHasNoIntensityAnywhere) {
  const MatchAnalytics::Heatmap heatmap;
  EXPECT_FLOAT_EQ(MatchAnalytics::GetNormalizedIntensity(heatmap, 0, 0), 0.0f);
}

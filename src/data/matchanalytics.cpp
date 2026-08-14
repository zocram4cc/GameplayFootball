#include "matchanalytics.hpp"

#include <algorithm>
#include <cmath>

#include "../gametypes.hpp"

namespace MatchAnalytics {

namespace {

float Clamp01(float value) {
  return std::max(0.0f, std::min(value, 1.0f));
}

// Distance at which a chance is still as good as it gets, and the scale over
// which it decays.
const float pointBlankDistance = 6.0f;
const float distanceDecay = 9.0f;
// A shot from the byline keeps this share of its value.
const float minAngleShare = 0.25f;
// Each body in the way costs a share of the chance.
const float defenderCost = 0.35f;
// Headers are harder to direct and to hit with power.
const float headerShare = 0.65f;
// A neutral spot rating (0.5) leaves the chance unchanged.
const float spotRatingSwing = 0.3f;
const float minExpectedGoals = 0.005f;

int ClampCell(int cell, int cellCount) {
  return std::max(0, std::min(cell, cellCount - 1));
}

int CellIndex(float coordinate, float halfExtent, int cellCount) {
  const float normalized = Clamp01((coordinate + halfExtent) / (2.0f * halfExtent));
  return ClampCell(static_cast<int>(normalized * static_cast<float>(cellCount)), cellCount);
}

bool IsInside(int cellX, int cellY) {
  return cellX >= 0 && cellX < Heatmap::cellsX && cellY >= 0 && cellY < Heatmap::cellsY;
}

int ValidTeam(int teamID) {
  return teamID == 1 ? 1 : 0;
}

}  // namespace

float CalculateExpectedGoals(const ShotContext& context) {
  const float distance = std::max(0.0f, context.distance);
  const float distanceTerm =
      std::min(std::exp(-(distance - pointBlankDistance) / distanceDecay), 1.0f);
  const float angleTerm = minAngleShare + (1.0f - minAngleShare) * Clamp01(context.angleFactor);
  const float defenderTerm =
      1.0f / (1.0f + defenderCost * static_cast<float>(std::max(0, context.defendersInPath)));
  const float techniqueTerm = context.isHeader ? headerShare : 1.0f;
  const float spotTerm = 1.0f + (Clamp01(context.spotRating) - 0.5f) * 2.0f * spotRatingSwing;

  const float expectedGoals =
      maxExpectedGoals * distanceTerm * angleTerm * defenderTerm * techniqueTerm * spotTerm;
  return std::max(minExpectedGoals, std::min(expectedGoals, maxExpectedGoals));
}

ShotContext MakeShotContext(const blunted::Vector3& shotPosition, int attackingSide,
                            int defendersInPath, bool isHeader, float spotRating) {
  const float side = static_cast<float>(attackingSide >= 0 ? 1 : -1);
  // The goal being attacked is at the far end from the team's own side.
  const blunted::Vector3 goalCentre(-side * pitchHalfW, 0.0f, 0.0f);
  const blunted::Vector3 toGoal = (goalCentre - shotPosition).Get2D();

  ShotContext context;
  context.distance = toGoal.GetLength();
  // cos of the angle between the shot and the straight-on line to goal.
  context.angleFactor =
      context.distance > 0.0f ? Clamp01(std::fabs(toGoal.coords[0]) / context.distance) : 1.0f;
  context.defendersInPath = std::max(0, defendersInPath);
  context.isHeader = isHeader;
  context.spotRating = Clamp01(spotRating);
  return context;
}

void AddShot(ShotTally& tally, int teamID, float expectedGoals) {
  const int team = ValidTeam(teamID);
  tally.shots[team]++;
  tally.expectedGoals[team] += expectedGoals;
}

int GetShotCount(const ShotTally& tally, int teamID) {
  return tally.shots[ValidTeam(teamID)];
}

float GetExpectedGoals(const ShotTally& tally, int teamID) {
  return tally.expectedGoals[ValidTeam(teamID)];
}

int GetCellX(float x) {
  return CellIndex(x, pitchHalfW, Heatmap::cellsX);
}

int GetCellY(float y) {
  return CellIndex(y, pitchHalfH, Heatmap::cellsY);
}

void AddSample(Heatmap& heatmap, const blunted::Vector3& position) {
  const int cellX = GetCellX(position.coords[0]);
  const int cellY = GetCellY(position.coords[1]);
  heatmap.cells[cellY * Heatmap::cellsX + cellX]++;
  heatmap.samples++;
}

int GetCellCount(const Heatmap& heatmap, int cellX, int cellY) {
  if (!IsInside(cellX, cellY))
    return 0;
  return heatmap.cells[cellY * Heatmap::cellsX + cellX];
}

float GetNormalizedIntensity(const Heatmap& heatmap, int cellX, int cellY) {
  if (!IsInside(cellX, cellY))
    return 0.0f;

  int busiest = 0;
  for (int i = 0; i < Heatmap::cellsX * Heatmap::cellsY; i++)
    busiest = std::max(busiest, heatmap.cells[i]);

  if (busiest == 0)
    return 0.0f;
  return static_cast<float>(GetCellCount(heatmap, cellX, cellY)) / static_cast<float>(busiest);
}

}  // namespace MatchAnalytics

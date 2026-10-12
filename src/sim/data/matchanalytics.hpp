// Advanced match statistics: expected goals (xG) derived from the context of a
// shot, and heatmaps accumulated from ball positions.
// See SIMULATION_IMPROVEMENT_PROPOSAL.md section 5B.

#ifndef _HPP_MATCH_ANALYTICS
#define _HPP_MATCH_ANALYTICS

#include "base/math/vector3.hpp"

namespace MatchAnalytics {

struct ShotContext {
  // Distance to the centre of the goal, in metres.
  float distance = 20.0f;
  // 1 straight in front of goal, 0 from the byline.
  float angleFactor = 1.0f;
  int defendersInPath = 0;
  bool isHeader = false;
  // The engine's own rating of the spot the shot was taken from.
  float spotRating = 0.5f;
};

// No chance is ever a certainty.
const float maxExpectedGoals = 0.95f;

float CalculateExpectedGoals(const ShotContext& context);

// Builds the context from a pitch position. `attackingSide` is the shooting
// team's side (-1 or 1); the goal being attacked sits at -attackingSide * pitchHalfW.
ShotContext MakeShotContext(const blunted::Vector3& shotPosition, int attackingSide,
                            int defendersInPath, bool isHeader, float spotRating);

struct ShotTally {
  float expectedGoals[2] = {0.0f, 0.0f};
  int shots[2] = {0, 0};
};

void AddShot(ShotTally& tally, int teamID, float expectedGoals);
int GetShotCount(const ShotTally& tally, int teamID);
float GetExpectedGoals(const ShotTally& tally, int teamID);

struct Heatmap {
  static const int cellsX = 16;
  static const int cellsY = 10;

  int cells[cellsX * cellsY] = {0};
  int samples = 0;
};

// Grid cell a pitch coordinate falls into; positions off the pitch clamp to the
// edge cells.
int GetCellX(float x);
int GetCellY(float y);

void AddSample(Heatmap& heatmap, const blunted::Vector3& position);
int GetCellCount(const Heatmap& heatmap, int cellX, int cellY);
// Cell count relative to the busiest cell, in [0, 1].
float GetNormalizedIntensity(const Heatmap& heatmap, int cellX, int cellY);

}  // namespace MatchAnalytics

#endif

// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");

#ifndef _HPP_GAMEPLAY_TUNING
#define _HPP_GAMEPLAY_TUNING

#include <algorithm>
#include <cmath>

#include "base/math/bluntmath.hpp"
#include "base/properties.hpp"

namespace GameplayTuning {

inline float Clamp01(float value) {
  return std::max(0.0f, std::min(value, 1.0f));
}

// Adds a small, explainable first-touch penalty under close pressure or when a
// fast incoming ball arrives from outside the player's field of view.
inline float GetFirstTouchContextPenalty(float opponentDistance, float calmness, float balance,
                                         float incomingBallSpeed, float incomingFacingAlignment) {
  const float pressure = Clamp01((1.6f - opponentDistance) / 1.2f);
  const float composure = Clamp01(calmness * 0.65f + balance * 0.35f);
  const float ballPace = Clamp01((incomingBallSpeed - 4.0f) / 10.0f);
  const float blindSide = Clamp01((0.25f - incomingFacingAlignment) / 1.25f);

  const float pressurePenalty = pressure * (1.0f - composure * 0.65f) * 0.08f;
  const float orientationPenalty = ballPace * blindSide * (1.0f - composure * 0.4f) * 0.08f;
  return std::min(pressurePenalty + orientationPenalty, 0.14f);
}

// How open the game is. The stock engine only let a player shoot inside a tight
// window near the goal, which produced three or four shots a match; these two
// knobs open that up and are tunable from the config.
inline float GetShootingRange(const blunted::Properties& config) {
  return blunted::clamp(config.GetReal("gameplay_shooting_range", 21.0f), 12.0f, 45.0f);
}

inline float GetShotAppetite(const blunted::Properties& config) {
  return blunted::clamp(config.GetReal("gameplay_shot_appetite", 1.9f), 0.5f, 2.5f);
}

// Whether a keeper goes for a shot at all.
//
// The stock engine always played the save animation and almost nothing went in,
// so this roll was introduced to beat him sometimes - but at 0.32 sharpness it
// meant an average keeper tried for fewer than one shot in four and stood
// watching the rest, which is not a keeper being beaten, it is a keeper not
// playing. He goes for nearly everything now; whether he *reaches* it is for the
// save itself to decide, since the animation search only accepts a save that can
// actually get to the ball. Reaction still separates keepers, by a shade rather
// than by whether they bother.
inline float GetKeeperSaveChance(const blunted::Properties& config, float reactionStat) {
  const float sharpness =
      blunted::clamp(config.GetReal("gameplay_keeper_sharpness", 0.88f), 0.2f, 1.0f);
  const float reaction = blunted::clamp(reactionStat, 0.0f, 1.0f);
  return blunted::clamp(sharpness * (0.80f + reaction * 0.20f), 0.05f, 0.99f);
}

// Distance remains the primary fatigue input. This workload factor makes
// jogging slightly cheaper and repeated sprinting slightly more expensive.
inline float GetFatigueWorkloadFactor(float movementSpeed, float maximumSpeed, bool carryingBall) {
  if (maximumSpeed <= 0.0f)
    return 1.0f;

  const float speedRatio = Clamp01(movementSpeed / maximumSpeed);
  const float sprintLoad = Clamp01((speedRatio - 0.55f) / 0.45f);
  float workload = 0.90f + sprintLoad * sprintLoad * 0.28f;
  if (carryingBall)
    workload += sprintLoad * 0.04f;
  return workload;
}

inline bool IsGoalMouthThreat(float lateralPosition, float ballHeight, float goalHalfWidth,
                              float goalHeight, float anticipationFactor) {
  const float anticipation = std::max(anticipationFactor, 1.0f);
  const float anticipatedHalfWidth = goalHalfWidth * anticipation;
  const float anticipatedHeight = goalHeight + 0.11f + (anticipation - 1.0f) * 0.25f;
  return std::fabs(lateralPosition) <= anticipatedHalfWidth && ballHeight >= 0.0f &&
         ballHeight <= anticipatedHeight;
}

}  // namespace GameplayTuning

#endif  // _HPP_GAMEPLAY_TUNING

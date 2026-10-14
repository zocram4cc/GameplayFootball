// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");

#ifndef _HPP_AI_TACTICS
#define _HPP_AI_TACTICS

#include <algorithm>

namespace AITactics {

inline float ClampSetting(float value) {
  return std::max(0.0f, std::min(value, 1.0f));
}

// A low setting waits for a clear break; a high setting encourages earlier
// supporting runs. The neutral value remains close to the previous threshold.
inline float GetAttackingRunThreshold(float counterAttack) {
  return 0.48f - ClampSetting(counterAttack) * 0.24f;
}

inline unsigned int GetAttackingRunDuration_ms(float counterAttack) {
  return 3400U + static_cast<unsigned int>(ClampSetting(counterAttack) * 2200.0f);
}

inline float GetDribbleForwardDrive(float dribbleOffensiveness, float roleMindset) {
  return 0.62f + ClampSetting(dribbleOffensiveness) * 0.28f + ClampSetting(roleMindset) * 0.12f;
}

// Neutral remains the historical 0.75 force-field scale. Lower values create
// shorter passing links; higher values spread support players a little wider.
inline float GetSupportWebScale(float supportDistance) {
  return 0.60f + ClampSetting(supportDistance) * 0.20f;
}

// Defenders still join possession play, but centre-backs retain more of their
// base shape than full-backs.
inline float GetDefenderSupportScale(float roleMindset) {
  return std::min(0.72f + ClampSetting(roleMindset) * 0.52f, 1.0f);
}

// Under pressure, permit a safe lateral or slightly backward pass when it
// creates clear breathing room. This complements, rather than replaces, the
// existing preference for progressive passes.
inline bool ShouldConsiderSupportPass(float currentTacticalRating, float currentSpaceRating,
                                      float teammateTacticalRating, float teammateSpaceRating,
                                      float longPossessionFactor) {
  const float retentionNeed =
      std::max(1.0f - ClampSetting(currentSpaceRating), ClampSetting(longPossessionFactor));
  return retentionNeed >= 0.55f && teammateSpaceRating >= currentSpaceRating + 0.12f &&
         teammateTacticalRating >= currentTacticalRating - 0.15f;
}

inline float GetSupportPassBonus(float currentSpaceRating, float teammateSpaceRating,
                                 float longPossessionFactor) {
  const float spaceGain = ClampSetting((teammateSpaceRating - currentSpaceRating) / 0.4f);
  const float retentionNeed =
      std::max(1.0f - ClampSetting(currentSpaceRating), ClampSetting(longPossessionFactor));
  return spaceGain * retentionNeed * 0.35f;
}

}  // namespace AITactics

#endif

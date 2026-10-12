#include "crowdmood.hpp"

#include <algorithm>

namespace CrowdMood {

namespace {

float Clamp01(float value) {
  return std::max(0.0f, std::min(value, 1.0f));
}

// Sustained possession lifts the floor of the noise rather than swamping the
// goal and near-miss reactions.
const float possessionBlendWeight = 0.5f;

}  // namespace

float GetHomeSupportFactor(float possessionFactor60s, int homeTeamID) {
  const float teamOneShare = Clamp01(possessionFactor60s);
  return homeTeamID == 1 ? teamOneShare : 1.0f - teamOneShare;
}

float GetPossessionExcitement(float possessionFactor60s, int homeTeamID) {
  const float support = GetHomeSupportFactor(possessionFactor60s, homeTeamID);
  const float dominance = (support - 0.5f) * 2.0f;  // -1 dominated .. 1 dominant
  return Clamp01(evenGameExcitement + dominance * (maxPossessionExcitement - evenGameExcitement));
}

float Blend(float baseExcitement, float possessionExcitement) {
  const float base = Clamp01(baseExcitement);
  const float possession = Clamp01(possessionExcitement);
  return Clamp01(base + (1.0f - base) * possession * possessionBlendWeight);
}

}  // namespace CrowdMood

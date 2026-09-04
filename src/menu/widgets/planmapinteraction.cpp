#include "planmapinteraction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using blunted::Vector3;

namespace PlanMapInteraction {

namespace {

// Outfield players have their tactical depth compressed toward mid-pitch so
// the back line does not sit flush against the goal box the keeper owns
// alone - the exact factors Gui2PlanMap applied before this module existed.
constexpr float kOutfieldDepthScale = 0.9f;
constexpr float kOutfieldDepthOffset = 0.05f;
constexpr float kWidthSpan = 0.42f;

float CompressedX(float databaseX, e_PlayerRole role) {
  if (role == e_PlayerRole_GK) return databaseX;
  return databaseX * kOutfieldDepthScale + kOutfieldDepthOffset;
}

float UncompressedX(float compressedX, e_PlayerRole role) {
  if (role == e_PlayerRole_GK) return compressedX;
  return (compressedX - kOutfieldDepthOffset) / kOutfieldDepthScale;
}

}  // namespace

PitchPoint DatabaseToPitch(const Vector3& databasePosition, e_PlayerRole role) {
  const float x = CompressedX(databasePosition.coords[0], role);
  const float y = databasePosition.coords[1];
  PitchPoint point;
  point.xPercent = (y * kWidthSpan + 0.5f) * 100.0f;
  point.yPercent = (-x * kWidthSpan + 0.5f) * 100.0f;
  return point;
}

Vector3 PitchToDatabase(const PitchPoint& point, e_PlayerRole role) {
  const float y = (point.xPercent / 100.0f - 0.5f) / kWidthSpan;
  const float compressedX = -(point.yPercent / 100.0f - 0.5f) / kWidthSpan;
  return Vector3(UncompressedX(compressedX, role), y, 0.0f);
}

PitchPoint ClampToPitch(PitchPoint point, float marginPercent) {
  point.xPercent = std::clamp(point.xPercent, marginPercent, 100.0f - marginPercent);
  point.yPercent = std::clamp(point.yPercent, marginPercent, 100.0f - marginPercent);
  return point;
}

namespace {

float Distance(const PitchPoint& a, const PitchPoint& b) {
  const float dx = a.xPercent - b.xPercent;
  const float dy = a.yPercent - b.yPercent;
  return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

int NearestCardWithinRadius(const PitchPoint& at, const std::vector<PitchPoint>& cards,
                            int excludeIndex, float radiusPercent) {
  int best = -1;
  float bestDistance = std::numeric_limits<float>::max();
  for (int i = 0; i < static_cast<int>(cards.size()); i++) {
    if (i == excludeIndex) continue;
    const float d = Distance(at, cards.at(i));
    if (d <= radiusPercent && d < bestDistance) {
      bestDistance = d;
      best = i;
    }
  }
  return best;
}

int NextSelectionInDirection(const std::vector<PitchPoint>& cards, int currentIndex,
                             const Vector3& direction) {
  if (currentIndex < 0 || currentIndex >= static_cast<int>(cards.size())) return currentIndex;
  const PitchPoint& from = cards.at(currentIndex);
  // Screen y grows downward; direction.coords[1] follows the same convention
  // WindowingEvent already uses (SDLK_DOWN -> (0,1,0), see guitask.cpp).
  const float dirX = direction.coords[0];
  const float dirY = direction.coords[1];
  const float dirLength = std::sqrt(dirX * dirX + dirY * dirY);
  if (dirLength < 1e-4f) return currentIndex;

  int best = currentIndex;
  float bestScore = std::numeric_limits<float>::max();
  for (int i = 0; i < static_cast<int>(cards.size()); i++) {
    if (i == currentIndex) continue;
    const float dx = cards.at(i).xPercent - from.xPercent;
    const float dy = cards.at(i).yPercent - from.yPercent;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance < 1e-4f) continue;
    // Projection onto the pressed direction must be positive - the
    // candidate actually lies that way, not behind the cursor.
    const float projection = (dx * dirX + dy * dirY) / distance;
    if (projection <= 0.05f) continue;
    // Candidates close to the direction's axis and near the source win; a
    // card almost due right beats one further right but also far off-axis.
    const float lateral = std::sqrt(std::max(0.0f, distance * distance -
                                              (projection * distance) * (projection * distance)));
    const float score = distance + lateral * 2.0f;
    if (score < bestScore) {
      bestScore = score;
      best = i;
    }
  }
  return best;
}

}  // namespace PlanMapInteraction

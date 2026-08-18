#include "staginganchor.hpp"

#include <cmath>

namespace StagingAnchor {

namespace {
// How far outside the line the cast starts. Enough to read as coming in from
// somewhere, short enough that PES's own ten metres finish well inside.
constexpr float kStartOutside = 4.0f;

float BringToTheLine(float start, float half) {
  if (std::fabs(start) <= half) return 0.0f;  // already on the pitch on this axis
  const float wanted = (start < 0.0f ? -1.0f : 1.0f) * (half + kStartOutside);
  return wanted - start;
}
}  // namespace

blunted::Vector3 WalkOnOffset(const blunted::Vector3& startCentre, float pitchHalfX,
                              float pitchHalfY) {
  blunted::Vector3 offset(0.0f, 0.0f, 0.0f);
  if (pitchHalfX <= 1.0f || pitchHalfY <= 1.0f) return offset;
  offset.coords[0] = BringToTheLine(startCentre.coords[0], pitchHalfX);
  offset.coords[1] = BringToTheLine(startCentre.coords[1], pitchHalfY);
  return offset;
}

}  // namespace StagingAnchor

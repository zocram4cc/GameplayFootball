#include "staginganchor.hpp"

#include <cmath>

namespace StagingAnchor {

blunted::Vector3 OnPitchOffset(const blunted::Vector3& finishCentre, float pitchHalfX,
                               float pitchHalfY) {
  blunted::Vector3 offset(0.0f, 0.0f, 0.0f);
  if (pitchHalfX <= 1.0f || pitchHalfY <= 1.0f) return offset;
  if (std::fabs(finishCentre.coords[0]) <= pitchHalfX &&
      std::fabs(finishCentre.coords[1]) <= pitchHalfY)
    return offset;

  // Onto the centre spot, keeping the choreography's own height.
  offset.coords[0] = -finishCentre.coords[0];
  offset.coords[1] = -finishCentre.coords[1];
  return offset;
}

}  // namespace StagingAnchor

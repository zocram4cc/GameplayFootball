#include "camerastandoff.hpp"

#include <algorithm>
#include <cmath>

namespace CameraStandoff {

float PushBack(const std::vector<blunted::Vector3>& cast, const blunted::Vector3& eye,
               const blunted::Vector3& forward, float clearance) {
  blunted::Vector3 aim = forward;
  const float aimLength = aim.GetLength();
  if (aimLength < 1e-4f || clearance <= 0.0f) return 0.0f;
  aim /= aimLength;

  float nearest = clearance;
  for (const blunted::Vector3& groundPosition : cast) {
    // Measured to the body rather than the feet: the cast's positions are on the
    // ground and the lens is at head height.
    blunted::Vector3 body = groundPosition;
    body.coords[2] += 0.9f;

    const blunted::Vector3 toBody = body - eye;
    const float along = toBody.GetDotProduct(aim);
    if (along <= 0.0f) continue;  // behind the lens

    const blunted::Vector3 offAxis = toBody - aim * along;
    if (offAxis.GetLength() > clearance) continue;  // beside it, not in the way

    nearest = std::min(nearest, toBody.GetLength());
  }
  return std::max(0.0f, clearance - nearest);
}

}  // namespace CameraStandoff

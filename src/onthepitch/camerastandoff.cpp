#include "camerastandoff.hpp"

#include <algorithm>
#include <cmath>

namespace CameraStandoff {

float PushBack(const std::vector<Body>& cast, const blunted::Vector3& eye,
               const blunted::Vector3& forward, float clearance) {
  blunted::Vector3 aim = forward;
  const float aimLength = aim.GetLength();
  if (aimLength < 1e-4f || clearance <= 0.0f) return 0.0f;
  aim /= aimLength;

  float push = 0.0f;
  for (const Body& body : cast) {
    // Measured to the body rather than the feet: the cast's positions are on the
    // ground and the lens is at head height.
    blunted::Vector3 centre = body.position;
    centre.coords[2] += 0.9f;

    const blunted::Vector3 toBody = centre - eye;
    const float along = toBody.GetDotProduct(aim);
    // Behind the lens once his mesh is, not once his centre is: a broad body
    // still covers the glass with his centre half a metre behind it.
    if (along <= -std::max(0.0f, body.radius)) continue;

    // The lens must be outside a sphere of this radius about him.
    const float keepOut = clearance + std::max(0.0f, body.radius);
    const float distance = toBody.GetLength();
    if (distance >= keepOut) continue;

    // Dolly back along -aim until the eye leaves that sphere: the exact exit
    // point, not clearance minus distance, which for a body off the axis
    // over-shot by the whole of his offset.
    const float offAxis2 = distance * distance - along * along;
    const float exit = std::sqrt(std::max(0.0f, keepOut * keepOut - offAxis2)) - along;
    push = std::max(push, exit);
  }
  return push;
}

}  // namespace CameraStandoff

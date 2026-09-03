#include "onthepitch/player/humanoid/skinning.hpp"

namespace Skinning {

JointTransform MakeJointTransform(const blunted::Quaternion& orientation,
                                  const blunted::Vector3& origPos,
                                  const blunted::Vector3& position, float zMultiplier) {
  // The rotation matrix of the same quaternion Vector3::Rotate applies. That
  // rotates by v + 2w(q x v) + 2q x (q x v), which for a unit quaternion is
  // exactly this matrix - so building it once a frame costs nothing in fidelity.
  const float x = orientation.elements[0];
  const float y = orientation.elements[1];
  const float z = orientation.elements[2];
  const float w = orientation.elements[3];
  const float xx = x * x, yy = y * y, zz = z * z;
  const float xy = x * y, xz = x * z, yz = y * z;
  const float wx = w * x, wy = w * y, wz = w * z;

  JointTransform transform;
  transform.rotation[0] = 1.0f - 2.0f * (yy + zz);
  transform.rotation[1] = 2.0f * (xy - wz);
  transform.rotation[2] = 2.0f * (xz + wy);
  transform.rotation[3] = 2.0f * (xy + wz);
  transform.rotation[4] = 1.0f - 2.0f * (xx + zz);
  transform.rotation[5] = 2.0f * (yz - wx);
  transform.rotation[6] = 2.0f * (xz - wy);
  transform.rotation[7] = 2.0f * (yz + wx);
  transform.rotation[8] = 1.0f - 2.0f * (xx + yy);

  // R * (v - bind) + posed, gathered into a single translation.
  const float bind[3] = {(float)origPos.coords[0] * zMultiplier,
                         (float)origPos.coords[1] * zMultiplier,
                         (float)origPos.coords[2] * zMultiplier};
  for (int row = 0; row < 3; row++) {
    transform.translation[row] =
        (float)position.coords[row] * zMultiplier -
        (transform.rotation[row * 3 + 0] * bind[0] + transform.rotation[row * 3 + 1] * bind[1] +
         transform.rotation[row * 3 + 2] * bind[2]);
  }
  return transform;
}





int BatchSize(int bodyCount, int workerCount) {
  if (workerCount < 1) return bodyCount > 0 ? bodyCount : 1;  // empty pool: one inline batch
  if (bodyCount < 1) return 1;                                // never a batch of zero to loop on
  return (bodyCount + workerCount - 1) / workerCount;         // round up, so nothing is left over
}

bool BodyNeedsSkinning(bool distantFromAction, bool halveDistantRate, int phase,
                       int phaseOffset) {
  if (!halveDistantRate || !distantFromAction) return true;
  // The offset is per body, so half the squad takes each frame rather than all
  // the distant bodies landing on the same one.
  return phase == 1 - phaseOffset;
}

}  // namespace Skinning

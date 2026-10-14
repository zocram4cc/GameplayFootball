// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "strategy.hpp"

bool Strategy::RequestOffBallTarget(Vector3& direction, float& velocity) {
  Vector3 target;
  if (!team->GetController()->GetOffBallTarget(CastPlayer(), &target)) return false;
  const Vector3 delta = (target - player->GetPosition()).Get2D();
  direction = delta.GetNormalized(player->GetDirectionVec());
  velocity = clamp(delta.GetLength() * distanceToVelocityMultiplier, 0, sprintVelocity);
  return true;
}

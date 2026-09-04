// Keeping an imported shot out of the players it is filming.
//
// PES composes its entrance camerawork around PES's own players, and several of
// its cuts are deliberately tight. Played over a 4cc cast - characters with
// hats, wings and props PES never allowed for - those cuts put the lens inside a
// chest, and the frame fills with one shirt.
//
// The shot is kept: it is dollied straight back along its own view axis until
// the nearest body it is looking at clears the lens. The framing, the lens and
// the move stay PES's; only the distance changes, and only when it has to.

#ifndef _HPP_ONTHEPITCH_CAMERASTANDOFF
#define _HPP_ONTHEPITCH_CAMERASTANDOFF

#include <vector>

#include "base/math/vector3.hpp"

namespace CameraStandoff {

// Somebody in shot: where he stands (on the ground) and how far his mesh reaches
// from that point. PES's men are half a metre across; a 4cc Wario is 2.4 m, so
// his centre can sit outside a fixed clearance while his glove is over the lens.
struct Body {
  blunted::Vector3 position;
  float radius = 0.4f;
};

// How far to move the camera back along -forward so that nothing in `cast` is
// nearer than `clearance` beyond its own radius. Only what is in front of the
// lens counts, and only what is actually in the way: somebody standing beside
// the camera is not. The camera's own height is accounted for.
float PushBack(const std::vector<Body>& cast, const blunted::Vector3& eye,
               const blunted::Vector3& forward, float clearance);

}  // namespace CameraStandoff

#endif

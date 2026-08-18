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

// How far to move the camera back along -forward so nothing in `cast` is nearer
// than `clearance`. Only what is in front of the lens counts, and only what is
// roughly in shot: somebody standing beside the camera is not in the way.
// `cast` positions are on the ground; the camera's own height is accounted for.
float PushBack(const std::vector<blunted::Vector3>& cast, const blunted::Vector3& eye,
               const blunted::Vector3& forward, float clearance);

}  // namespace CameraStandoff

#endif

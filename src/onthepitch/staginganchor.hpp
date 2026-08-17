// Putting an imported staging where our pitch is.
//
// PES authors its entrance choreography in its own stadium's coordinates, and
// its walk-on packs start outside the field because that is where its tunnel
// mouth is: ent_009_st000 walks its cast from y -48 to y -38, and this pitch
// ends at y 36, so the whole walk happens out on the terrain past the
// touchline. The motion is worth keeping and only its
// placement is wrong, so a staging that finishes off the playing area is
// translated until it finishes on the centre spot. One that finishes on the
// pitch already - the anthem line-up, the team picture - is left exactly where
// its author put it.

#ifndef _HPP_ONTHEPITCH_STAGINGANCHOR
#define _HPP_ONTHEPITCH_STAGINGANCHOR

#include "base/math/vector3.hpp"

namespace StagingAnchor {

// The translation to apply to every position in a staging, given where its cast
// finishes. Zero when that is already inside the playing area, or when the pitch
// measurements are not usable.
blunted::Vector3 OnPitchOffset(const blunted::Vector3& finishCentre, float pitchHalfX,
                               float pitchHalfY);

}  // namespace StagingAnchor

#endif

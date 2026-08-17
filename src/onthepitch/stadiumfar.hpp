// How far the camera has to see for a given stadium.
//
// The gameplay far plane is 200-250 m. That is generous for a football stadium
// and useless for a pack whose surroundings are the view: Planet Namek's sky dome
// is 1154 m across and reaches 625 m, so it fell entirely outside the frustum and
// the engine's fallback gradient showed instead. Match already floors the far
// plane at 500 m when a separate skydome_object is loaded; a stadium carrying its
// sky in its own mesh needs the same, and only the converter knows how far its
// geometry reaches - so it writes the distance beside the .object as farplane.txt.

#ifndef _HPP_ONTHEPITCH_STADIUMFAR
#define _HPP_ONTHEPITCH_STADIUMFAR

#include <string>

namespace StadiumFar {

// Beyond this, depth precision costs more than the scenery is worth.
extern const float kMaxFarCap;

// farplane.txt beside the given stadium object, or "" when there is no stadium.
std::string SidecarPath(const std::string& stadiumObjectPath);

// A distance from the sidecar's contents; 0 for anything that is not one.
float ParseDistance(const std::string& text);

// The far plane to use: the gameplay cap unless the stadium reaches past it.
float ChooseFarCap(float configuredCap, float stadiumNeeds);

}  // namespace StadiumFar

#endif

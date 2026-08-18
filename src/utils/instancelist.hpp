// Where the many copies of one model stand.
//
// PES authors its crowd as one spectator and its 3D turf as one tuft, and then
// places them thousands of times: a stadium pack's audi/audiarea.bin gives the
// stands and their row spacing, and st041 alone works out to about 13,800 seats.
// Merged into static geometry that is 1.8 million vertices in a text ASE, which
// the loader spends minutes on; drawing one mesh many times costs the GPU almost
// nothing.
//
// So the model stays a normal .ase and its placements sit beside it in a list, one
// line each - x, y, z and the yaw it faces - which the .object points at with
// <instances>. Plain text, like everything else in the pack, so a stand can be
// edited by hand.

#ifndef _HPP_UTILS_INSTANCELIST
#define _HPP_UTILS_INSTANCELIST

#include <string>
#include <vector>

namespace InstanceList {

struct Placement {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float yaw = 0.0f;
};

struct Bounds {
  float low[3] = {0.0f, 0.0f, 0.0f};
  float high[3] = {0.0f, 0.0f, 0.0f};
  bool valid = false;
};

// One placement per line: "x y z [yaw]". Comments start with #; a line that is
// not a placement is skipped rather than dropped at the origin, where it would
// stand a spectator on the centre spot.
std::vector<Placement> Parse(const std::string& text);

// The same from a file; an unreadable file is an empty list.
std::vector<Placement> Load(const std::string& path);

// How far the copies spread, for culling: without this a crowd whose model sits
// at the origin is culled the moment the origin leaves the frustum.
Bounds Extent(const std::vector<Placement>& places);

}  // namespace InstanceList

#endif

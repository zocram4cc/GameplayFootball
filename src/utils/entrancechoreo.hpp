// Loader for the .chor text format: imported PES match-entrance player
// choreography (tools/pes21_import/entrance_pl.py). One file per PES _pl
// pack; per actor slot it carries the clip to play (an in-place .anim in the
// sibling anims/ directory), a phase offset into that clip, and a baked
// world-space root track (position + yaw over one clip cycle) already in
// engine space (metres, Z up, pitch centre origin, 10 ms frames):
//
//   chor 1
//   source ent_020_order01_pl.fdc
//   slot <n> anims/<clip>.anim phase <frames> loop <0|1>
//   k <frame> <x> <y> <yaw-rad>
//   ...
//
// Slots 0-10 are the home XI (0 = keeper), 11-21 the away XI, 22+ officials.
// This class is pure data: the clip .anim files it names are loaded by the
// caller (Match), keeping the parser free of engine dependencies.

#ifndef _HPP_UTILS_ENTRANCECHOREO
#define _HPP_UTILS_ENTRANCECHOREO

#include <istream>
#include <string>
#include <vector>

#include "base/math/bluntmath.hpp"
#include "base/math/vector3.hpp"

namespace blunted {

struct ChoreoKey {
  int frame = 0;
  float x = 0.0f;
  float y = 0.0f;
  float yaw = 0.0f;  // radians, unwrapped; a player at yaw a faces (sin a, -cos a)
};

struct ChoreoSlot {
  int slot = -1;
  std::string animFile;  // relative to the .chor's own directory
  int phaseFrames = 0;
  bool loop = true;
  std::vector<ChoreoKey> keys;  // one clip cycle on the 10 ms frame grid
  int cycleFrames = 0;          // keys.back().frame
};

class EntranceChoreo {
public:
  bool Load(std::istream& in);

  bool IsLoaded() const { return !slots.empty(); }
  const std::vector<ChoreoSlot>& GetSlots() const { return slots; }
  const ChoreoSlot* GetSlot(int slot) const;

  // World transform and clip frame of a slot at the given entrance time
  // (10 ms frames since the entrance began). Loops over the clip cycle.
  void Sample(const ChoreoSlot& slot, float elapsedFrame, Vector3& position,
              radian& yaw, int& animFrame) const;

private:
  std::vector<ChoreoSlot> slots;
};

}  // namespace blunted

#endif

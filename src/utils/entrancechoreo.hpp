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

// Who a staged actor is meant to be. PES names the parts in the clip itself
// (a "_judge" clip is the official) and puts them in team-coded slots, so a
// cutscene can be cast with the people the incident actually involved rather
// than whoever happens to stand nearby.
enum e_ChoreoRole {
  e_ChoreoRole_Extra,      // any spare player
  e_ChoreoRole_Primary,    // the incident's subject: the booked player, scorer
  e_ChoreoRole_Opponent,   // his counterpart: the fouled player, the victim
  e_ChoreoRole_Official,   // referee or assistant
};

struct ChoreoSlot {
  int slot = -1;
  std::string animFile;  // relative to the .chor's own directory
  e_ChoreoRole role = e_ChoreoRole_Extra;
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

  // The pack's own name - the .chor's stem - so a goal celebration chosen by
  // name (celebrations.txt) can find the choreography PES shot it with.
  void SetName(const std::string& value) { name = value; }
  const std::string& GetName() const { return name; }

  // The last frame any slot is still performing: the slowest slot's entrance plus
  // one cycle of its clip, on the 10 ms grid. What a cutscene built on this
  // choreography runs for.
  int GetLastFrame() const;

private:
  std::vector<ChoreoSlot> slots;
  std::string name;
};

}  // namespace blunted

#endif

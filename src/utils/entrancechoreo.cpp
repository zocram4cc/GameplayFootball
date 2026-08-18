// .chor loader: PES match-entrance player choreography. See the header and
// docs/PES21_CAMERA_TRACE.md (the _pl pack format this is exported from).

#include "entrancechoreo.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace blunted {

bool EntranceChoreo::Load(std::istream& in) {
  slots.clear();

  std::vector<ChoreoSlot> parsed;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream tokens(line);
    std::string word;
    tokens >> word;
    if (word == "slot") {
      ChoreoSlot slot;
      tokens >> slot.slot >> slot.animFile;
      std::string key;
      while (tokens >> key) {
        if (key == "phase")
          tokens >> slot.phaseFrames;
        else if (key == "loop") {
          int loop = 1;
          tokens >> loop;
          slot.loop = loop != 0;
        } else if (key == "role") {
          std::string role;
          tokens >> role;
          if (role == "primary")
            slot.role = e_ChoreoRole_Primary;
          else if (role == "opponent")
            slot.role = e_ChoreoRole_Opponent;
          else if (role == "official")
            slot.role = e_ChoreoRole_Official;
        }
      }
      parsed.push_back(slot);
    } else if (word == "k" && !parsed.empty()) {
      ChoreoKey keyframe;
      tokens >> keyframe.frame >> keyframe.x >> keyframe.y >> keyframe.yaw;
      parsed.back().keys.push_back(keyframe);
    }
  }

  for (auto& slot : parsed) {
    if (slot.keys.empty() || slot.animFile.empty()) continue;
    slot.cycleFrames = std::max(slot.keys.back().frame, 1);
    slots.push_back(std::move(slot));
  }
  return IsLoaded();
}

const ChoreoSlot* EntranceChoreo::GetSlot(int slotIndex) const {
  for (const auto& slot : slots)
    if (slot.slot == slotIndex) return &slot;
  return nullptr;
}

void EntranceChoreo::Sample(const ChoreoSlot& slot, float elapsedFrame,
                            Vector3& position, radian& yaw,
                            int& animFrame) const {
  // The path plays once and then holds, whatever the slot's loop flag says. PES
  // flags its walk-on slots looping, and wrapping the path teleports the actor
  // back to the tunnel mouth to walk in again - in plain view, because a
  // walk-on beat runs 22 seconds over a 13-second path.
  float cycleTime = std::max(0.0f, std::min(elapsedFrame, (float)slot.cycleFrames));

  // The clip is a different length from the path and loops on its own; the
  // caller wraps this against it (see Match::UpdateEntranceChoreo), so it keeps
  // counting and an actor who has arrived marks time instead of freezing.
  animFrame = slot.phaseFrames + (int)std::max(0.0f, elapsedFrame);

  const auto& keys = slot.keys;
  if (keys.size() == 1) {
    position = Vector3(keys[0].x, keys[0].y, 0.0f);
    yaw = keys[0].yaw;
    return;
  }

  size_t hi = 1;
  while (hi < keys.size() - 1 && keys[hi].frame < cycleTime) hi++;
  const ChoreoKey& a = keys[hi - 1];
  const ChoreoKey& b = keys[hi];
  const float span = (float)(b.frame - a.frame);
  float bias = span > 0.0f ? (cycleTime - a.frame) / span : 0.0f;
  bias = std::max(0.0f, std::min(1.0f, bias));

  position = Vector3(a.x + (b.x - a.x) * bias, a.y + (b.y - a.y) * bias, 0.0f);
  yaw = a.yaw + (b.yaw - a.yaw) * bias;
}

}  // namespace blunted

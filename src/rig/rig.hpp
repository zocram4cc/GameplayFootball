#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pes/skl.hpp"
#include "pes/types.hpp"

namespace pes { class PesVfs; }

namespace pesrig {

using pes::Quat;
using pes::Vec3;

// One joint of the PES animation rig, in Fox space (Y up, metres).
struct RigBone {
  std::string gfName;   // GameplayFootball node name ("body", "hip", "left_thigh", ...)
  std::string pesBone;  // PES bone name ("sk_root_hip", ...)
  int parent = -1;      // joint ID of the parent, -1 for the root
  Vec3 animLocalPos;    // anim (T-pose) bind, relative to parent
  Vec3 animGlobalPos;   // anim bind, model space
  Vec3 renderGlobalPos; // render (.skl) bind, model space
  Quat align;           // W: world rotation carrying render pose to anim pose
};

// Bones in joint-ID order (20 body nodes, then 38 finger nodes).
struct Rig {
  std::vector<RigBone> bones;
};

std::optional<Rig> BuildRig(pes::PesVfs& vfs, std::string* error);

// Joint ID for a PES bone name; falls back to the nearest render-bind joint
// when the name is unknown and renderPos is given.
std::optional<int> ResolveBone(const Rig& rig, std::string_view pesBone, const Vec3* renderPos);

// Which of PES's two skeletons a part pack's own .skl was authored on: its
// bones sit at the rig's render positions or at its anim positions. A part
// authored on the anim pose needs no render -> anim alignment.
enum class PartPose { Render, Anim, Unknown };
PartPose ClassifyPartPose(const Rig& rig, const std::vector<pes::SklBone>& partSkeleton);

}  // namespace pesrig

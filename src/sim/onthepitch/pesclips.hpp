#pragma once

// PES body clips played by the simulation, converted from the user's install
// at match start (nothing PES-derived is committed). The conversion is the
// verbatim one of AGENTS.md's skeleton section: the engine's skeleton IS the
// PES anim rig, so each joint's local rotation is carried across with the
// Fox -> engine basis map (x, y, z) -> (x, -z, y) and nothing else; the body
// joint takes RIG_ROOT o mover, and the player line the world translation
// (rig/clip_pose.hpp samples both, at PES_FRAME_S and the gameplay scale).
//
// Clips join the AnimCollection as `special` animations keyed by
// (specialvar1 = family, specialvar2 = the clip's motion number), so a
// command naming the pair plays exactly that clip.

#include <optional>
#include <string>
#include <vector>

#include "pes/exe.hpp"
#include "pes/gani.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"

namespace pes {

// The specialvar1 families imported clips carry. Celebrations and cards use
// 1..3 (gamedefines.hpp), so imported families start well clear of them.
enum class PesClipFamily : int { kDodge = 100, kTackle = 101 };

struct PesClip {
  std::string name;
  int motionNo = -1;  // index in the exe's motion table (pes::MotionNames)
  PesClipFamily family = PesClipFamily::kDodge;
  Gani gani;
};

struct PesClipSet {
  std::vector<PesClip> clips;
};

// The clips of every family, by name prefix in the exe's motion table:
// family kDodge = the `dodge_`, `avoidjump_`, `avoidslide_`, `goalnet_`,
// `rapidstop_` and `rapid_3_2_dodge_` motions the Dodge action selects
// (docs/re/pes-re-contact-port.md pass 4 stage B);
// family kTackle = the `tacklefoot_` motions the Tackle action's sub-kind
// group tables select (stage C).
std::optional<PesClipSet> LoadPesClips(const PeImage& exe, const PesVfs& vfs, std::string* error);

// One clip as engine .anim text (the format Animation::LoadText reads), at the
// engine's 10 ms frame.
std::string PesClipAnimText(const PesClip& clip, const pesrig::Rig& rig);

}  // namespace pes

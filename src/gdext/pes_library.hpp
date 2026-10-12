#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <optional>

#include "pes/editsave.hpp"
#include "pes/handseq.hpp"
#include "rig/clip_pose.hpp"
#include "pes/vfs.hpp"
#include "rig/rig.hpp"

namespace godot {

// The user's PES 2021 install: its file system and the animation rig built from it.
class PesLibrary : public RefCounted {
  GDCLASS(PesLibrary, RefCounted)

 public:
  Error open(const String& root);
  String mount_report() const;
  PackedStringArray list(const String& prefix) const;
  // The user's EDIT00000000: each player's appearance as the game shows it.
  // Without it, players fall back to PlayerAppearance.bin, as in the game.
  Error load_edit_save(const String& path);

  const pes::PesVfs& vfs() const { return vfs_; }
  const pesrig::Rig* rig() const { return rig_ ? &*rig_ : nullptr; }
  // Save over PlayerAppearance.bin, by player id.
  const pes::AppearanceTable& appearances() const { return appearances_; }
  // Hand rig: motionNo -> clip name (from PES2021.exe), per-clip hand timelines
  // and the 162 hand poses. Null when the install lacks them; hands then rest.
  const std::vector<std::string>* motion_names() const { return motionNames_ ? &*motionNames_ : nullptr; }
  const pes::HandSequences* hand_sequences() const { return handSequences_ ? &*handSequences_ : nullptr; }
  const pesrig::HandPoseLibrary* hand_poses() const { return handPoses_ ? &*handPoses_ : nullptr; }

 protected:
  static void _bind_methods();

 private:
  pes::PesVfs vfs_;
  std::optional<pesrig::Rig> rig_;
  pes::AppearanceTable appearances_;
  std::optional<std::vector<std::string>> motionNames_;
  std::optional<pes::HandSequences> handSequences_;
  std::optional<pesrig::HandPoseLibrary> handPoses_;
};

inline String ToGd(const std::string& s) { return String::utf8(s.c_str()); }
inline std::string FromGd(const String& s) { return s.utf8().get_data(); }

}  // namespace godot

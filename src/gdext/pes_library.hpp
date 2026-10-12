#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <optional>

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

  const pes::PesVfs& vfs() const { return vfs_; }
  const pesrig::Rig* rig() const { return rig_ ? &*rig_ : nullptr; }

 protected:
  static void _bind_methods();

 private:
  pes::PesVfs vfs_;
  std::optional<pesrig::Rig> rig_;
};

inline String ToGd(const std::string& s) { return String::utf8(s.c_str()); }
inline std::string FromGd(const String& s) { return s.utf8().get_data(); }

}  // namespace godot

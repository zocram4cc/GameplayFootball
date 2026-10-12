#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>

#include "gdext/pes_library.hpp"

namespace godot {

// Builds a skinned character from a PES face pack, on the PES animation rig.
class PesCharacterBuilder : public RefCounted {
  GDCLASS(PesCharacterBuilder, RefCounted)

 public:
  // Node3D "Character" -> Skeleton3D "Skeleton" -> one MeshInstance3D per kept
  // mesh. Null (with an error pushed) when the pack is missing or unreadable.
  Node3D* build_face_model(const Ref<PesLibrary>& library, int face_id);

 protected:
  static void _bind_methods();
};

}  // namespace godot

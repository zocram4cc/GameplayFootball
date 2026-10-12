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
  // A player as PES assembles one: kit config for team_id's kit (0 = 1st),
  // the uniform and body parts it selects, the face pack, the kit atlas.
  // number < 1: no number; name: UTF-8 print name ("" = none).
  Node3D* build_player(const Ref<PesLibrary>& library, int team_id, int kit_index, int face_id, int number = -1,
                       const String& name = String());

 protected:
  static void _bind_methods();
};

}  // namespace godot

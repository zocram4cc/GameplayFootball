#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>

#include "gdext/pes_library.hpp"

namespace godot {

// Static PES models (ball, goals, corner flags, stadium scenes, pitch): every
// .fmdl of a pack, or only `member`, as MeshInstance3Ds in Fox space with
// their techniques' shaders.
class PesModelBuilder : public RefCounted {
  GDCLASS(PesModelBuilder, RefCounted)

 public:
  Node3D* build_model(const Ref<PesLibrary>& library, const String& fpk_path, const String& member) const;
  // One stadium as PES places it: every model of its scene, its pitch, both
  // goals with their nets, and the corner flags, each built with build_model
  // under a Node3D carrying the fox2 transform. Null when the install has no
  // such stadium.
  Node3D* build_stadium(const Ref<PesLibrary>& library, int stadium_id) const;

 protected:
  static void _bind_methods();
};

}  // namespace godot

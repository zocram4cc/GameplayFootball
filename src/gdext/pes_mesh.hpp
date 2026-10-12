#pragma once

#include <godot_cpp/variant/array.hpp>

#include "pes/fmdl.hpp"

namespace godot {

// Mesh::ARRAY_MAX arrays of one fmdl mesh: positions, normals, tangents, UV0,
// UV1 (as UV2) and LOD-0 indices; no skinning. Fox and Godot agree on
// front-face winding, so indices pass through as stored.
Array FmdlSurfaceArrays(const pes::FmdlMesh& mesh);

}  // namespace godot

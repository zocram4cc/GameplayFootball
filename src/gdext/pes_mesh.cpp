#include "gdext/pes_mesh.hpp"

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

namespace godot {

Array FmdlSurfaceArrays(const pes::FmdlMesh& mesh) {
  const int64_t n = int64_t(mesh.pos.size());
  PackedVector3Array pos, nrm;
  PackedVector2Array uv, uv2;
  PackedFloat32Array tan;
  PackedInt32Array indices;
  pos.resize(n);
  for (int64_t i = 0; i < n; ++i) pos.set(i, Vector3(mesh.pos[i].x, mesh.pos[i].y, mesh.pos[i].z));
  if (mesh.nrm.size() == size_t(n)) {
    nrm.resize(n);
    for (int64_t i = 0; i < n; ++i) nrm.set(i, Vector3(mesh.nrm[i].x, mesh.nrm[i].y, mesh.nrm[i].z).normalized());
  }
  if (mesh.tan.size() == size_t(n) && !nrm.is_empty()) {
    tan.resize(n * 4);
    for (int64_t i = 0; i < n; ++i) {
      const Vector3 t = Vector3(mesh.tan[i].x, mesh.tan[i].y, mesh.tan[i].z).normalized();
      tan.set(i * 4, t.x), tan.set(i * 4 + 1, t.y), tan.set(i * 4 + 2, t.z);
      tan.set(i * 4 + 3, mesh.tan[i].w < 0 ? -1.0f : 1.0f);
    }
  }
  if (mesh.uv0.size() == size_t(n)) {
    uv.resize(n);
    for (int64_t i = 0; i < n; ++i) uv.set(i, Vector2(mesh.uv0[i].x, mesh.uv0[i].y));
  }
  if (mesh.uv1.size() == size_t(n)) {  // PES UV1: uniform weave and wrinkle space
    uv2.resize(n);
    for (int64_t i = 0; i < n; ++i) uv2.set(i, Vector2(mesh.uv1[i].x, mesh.uv1[i].y));
  }
  indices.resize(int64_t(mesh.indices.size()));
  for (size_t k = 0; k < mesh.indices.size(); ++k) indices.set(int64_t(k), int32_t(mesh.indices[k]));
  Array arrays;
  arrays.resize(Mesh::ARRAY_MAX);
  arrays[Mesh::ARRAY_VERTEX] = pos;
  if (!nrm.is_empty()) arrays[Mesh::ARRAY_NORMAL] = nrm;
  if (!tan.is_empty()) arrays[Mesh::ARRAY_TANGENT] = tan;
  if (!uv.is_empty()) arrays[Mesh::ARRAY_TEX_UV] = uv;
  if (!uv2.is_empty()) arrays[Mesh::ARRAY_TEX_UV2] = uv2;
  arrays[Mesh::ARRAY_INDEX] = indices;
  return arrays;
}

}  // namespace godot

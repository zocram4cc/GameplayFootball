// Clean-room FMDL 2.03 reader. Block layouts follow FMDL-Studio-v2 (MIT,
// see NOTICE); no code was copied.
#include "pes/fmdl.hpp"

#include <cmath>
#include <cstring>
#include <unordered_map>

namespace pes {
namespace {

constexpr uint32_t FMDL_VERSION_2_03 = 0x4001eb85;  // f32 2.03 as stored

// Header field offsets.
constexpr size_t HDR_SECTION_INFO = 8, HDR_S0_COUNT = 32, HDR_S1_COUNT = 36, HDR_S0_OFFSET = 40, HDR_S1_OFFSET = 48;
constexpr size_t S0_INFO_BYTES = 8, S1_INFO_BYTES = 12;

// Section 0 block types and record sizes.
enum Block0 : uint16_t {
  BONES = 0, MESH_INFO = 3, MATERIAL_INSTANCES = 4, BONE_GROUPS = 5, TEXTURES = 6, MATERIAL_PARAMS = 7,
  MATERIALS = 8, MESH_FORMAT_INFO = 9, MESH_FORMATS = 10, VERTEX_FORMATS = 11, STRING_INFO = 12, BUFFER_OFFSETS = 14,
};
enum Block1 : uint32_t { PARAM_VECTORS = 0, BUFFER = 2, STRINGS = 3 };
constexpr size_t BONE_BYTES = 48, BONE_LOCAL = 16, BONE_WORLD = 32;
constexpr size_t MESH_INFO_BYTES = 48;
constexpr size_t MATERIAL_INSTANCE_BYTES = 16;
constexpr size_t BONE_GROUP_BYTES = 0x44;
constexpr size_t PAIR_BYTES = 4;  // textures, materials, material parameters: two u16
constexpr size_t MESH_FORMAT_INFO_BYTES = 8, MESH_FORMAT_BYTES = 8, VERTEX_FORMAT_BYTES = 4;
constexpr size_t STRING_INFO_BYTES = 8, BUFFER_OFFSET_BYTES = 16;
constexpr size_t VEC4_BYTES = 16;

// Vertex attribute types.
enum Attr : uint8_t { POSITION = 0, BONE_WEIGHTS = 1, NORMAL = 2, BONE_INDICES = 7, UV0 = 8, TANGENT = 0xE };
constexpr float UNORM8_MAX = 255.0f;
constexpr uint8_t INDEX_BUFFER = 2;  // buffer-offset slot holding the u16 triangle list

float Half(uint16_t h) {
  const uint32_t sign = (h >> 15) & 1, exp = (h >> 10) & 0x1F, mant = h & 0x3FF;
  float v;
  if (exp == 0) v = std::ldexp(float(mant), -24);
  else if (exp == 31) v = mant ? NAN : INFINITY;
  else v = std::ldexp(float(mant | 0x400), int(exp) - 25);
  return sign ? -v : v;
}

class Reader {
 public:
  explicit Reader(std::span<const uint8_t> b) : b_(b) {}
  bool ok = true;
  template <typename T>
  T At(size_t off) {
    T v{};
    if (off + sizeof(T) > b_.size()) { ok = false; return v; }
    std::memcpy(&v, b_.data() + off, sizeof(T));
    return v;
  }
  Vec4 V4(size_t off) { return {At<float>(off), At<float>(off + 4), At<float>(off + 8), At<float>(off + 12)}; }

 private:
  std::span<const uint8_t> b_;
};

}  // namespace

std::optional<Fmdl> ParseFmdl(std::span<const uint8_t> blob, std::string* error) {
  Reader r(blob);
  if (blob.size() < 56 || std::memcmp(blob.data(), "FMDL", 4) != 0) {
    *error = "not an FMDL";
    return std::nullopt;
  }
  if (r.At<uint32_t>(4) != FMDL_VERSION_2_03) {
    *error = "unsupported FMDL version";
    return std::nullopt;
  }
  const size_t infoAt = r.At<uint64_t>(HDR_SECTION_INFO);
  const uint32_t s0Count = r.At<uint32_t>(HDR_S0_COUNT), s1Count = r.At<uint32_t>(HDR_S1_COUNT);
  const size_t s0 = r.At<uint32_t>(HDR_S0_OFFSET), s1 = r.At<uint32_t>(HDR_S1_OFFSET);

  struct Block { size_t offset = 0, count = 0; bool present = false; };
  std::unordered_map<uint32_t, Block> b0, b1;
  for (uint32_t i = 0; i < s0Count; ++i) {
    const size_t at = infoAt + i * S0_INFO_BYTES;
    b0[r.At<uint16_t>(at)] = {s0 + r.At<uint32_t>(at + 4), r.At<uint16_t>(at + 2), true};
  }
  for (uint32_t i = 0; i < s1Count; ++i) {
    const size_t at = infoAt + s0Count * S0_INFO_BYTES + i * S1_INFO_BYTES;
    b1[r.At<uint32_t>(at)] = {s1 + r.At<uint32_t>(at + 4), r.At<uint32_t>(at + 8), true};  // count = byte length
  }
  if (!r.ok) { *error = "FMDL section table out of bounds"; return std::nullopt; }
  // A model without mesh info is valid and empty (4cc packs ship placeholder fmdls).
  const bool hasMeshes = b0[MESH_INFO].present && b0[MESH_INFO].count > 0;
  for (uint32_t need : {MESH_FORMAT_INFO, MESH_FORMATS, VERTEX_FORMATS, BUFFER_OFFSETS}) {
    if (hasMeshes && !b0[need].present) { *error = "FMDL lacks section-0 block " + std::to_string(need); return std::nullopt; }
  }
  if (hasMeshes && !b1[BUFFER].present) { *error = "FMDL lacks a vertex buffer"; return std::nullopt; }

  std::vector<std::string> strings;
  if (b0[STRING_INFO].present && b1[STRINGS].present) {
    for (size_t i = 0; i < b0[STRING_INFO].count; ++i) {
      const size_t at = b0[STRING_INFO].offset + i * STRING_INFO_BYTES;
      const uint16_t len = r.At<uint16_t>(at + 2);
      const size_t off = b1[STRINGS].offset + r.At<uint32_t>(at + 4);
      if (off + len > blob.size()) { r.ok = false; break; }
      strings.emplace_back(reinterpret_cast<const char*>(blob.data() + off), len);
    }
  }
  auto str = [&](uint16_t i) { return i < strings.size() ? strings[i] : std::string(); };

  Fmdl m;
  for (size_t i = 0; i < b0[BONES].count; ++i) {
    const size_t at = b0[BONES].offset + i * BONE_BYTES;
    const Vec4 l = r.V4(at + BONE_LOCAL), w = r.V4(at + BONE_WORLD);
    m.bones.push_back({str(r.At<uint16_t>(at)), r.At<int16_t>(at + 2), {l.x, l.y, l.z}, {w.x, w.y, w.z}});
  }
  for (size_t i = 0; i < b0[TEXTURES].count; ++i) {
    const size_t at = b0[TEXTURES].offset + i * PAIR_BYTES;
    m.textures.push_back({str(r.At<uint16_t>(at + 2)), str(r.At<uint16_t>(at))});
  }
  std::vector<Vec4> vectors;
  for (size_t i = 0; b1[PARAM_VECTORS].present && i < b1[PARAM_VECTORS].count / VEC4_BYTES; ++i)
    vectors.push_back(r.V4(b1[PARAM_VECTORS].offset + i * VEC4_BYTES));
  auto pairAt = [&](uint16_t block, size_t i) {
    const size_t at = b0[block].offset + i * PAIR_BYTES;
    return std::pair<uint16_t, uint16_t>{r.At<uint16_t>(at), r.At<uint16_t>(at + 2)};
  };
  for (size_t i = 0; i < b0[MATERIAL_INSTANCES].count; ++i) {
    const size_t at = b0[MATERIAL_INSTANCES].offset + i * MATERIAL_INSTANCE_BYTES;
    FmdlMaterial mat;
    mat.name = str(r.At<uint16_t>(at));
    const auto [shader, technique] = pairAt(MATERIALS, r.At<uint16_t>(at + 4));
    mat.shader = str(shader);
    mat.technique = str(technique);
    const uint8_t texCount = r.At<uint8_t>(at + 6), paramCount = r.At<uint8_t>(at + 7);
    const uint16_t firstTex = r.At<uint16_t>(at + 8), firstParam = r.At<uint16_t>(at + 10);
    for (uint8_t t = 0; t < texCount; ++t) {
      const auto [role, ref] = pairAt(MATERIAL_PARAMS, firstTex + t);
      mat.textures.emplace_back(str(role), ref);
    }
    for (uint8_t p = 0; p < paramCount; ++p) {
      const auto [name, ref] = pairAt(MATERIAL_PARAMS, firstParam + p);
      mat.params.emplace_back(str(name), ref < vectors.size() ? vectors[ref] : Vec4{});
    }
    m.materials.push_back(std::move(mat));
  }

  const size_t buffer = b1[BUFFER].offset;
  auto bufferOffset = [&](uint8_t i) { return r.At<uint32_t>(b0[BUFFER_OFFSETS].offset + i * BUFFER_OFFSET_BYTES + 8); };
  for (size_t i = 0; i < b0[MESH_INFO].count && r.ok; ++i) {
    const size_t info = b0[MESH_INFO].offset + i * MESH_INFO_BYTES;
    FmdlMesh mesh;
    mesh.materialIndex = r.At<uint16_t>(info + 4);
    const uint16_t boneGroup = r.At<uint16_t>(info + 6);
    const uint16_t vertexCount = r.At<uint16_t>(info + 10);
    const uint32_t firstIndex = r.At<uint32_t>(info + 16), indexCount = r.At<uint32_t>(info + 20);
    if (b0[BONE_GROUPS].present && boneGroup < b0[BONE_GROUPS].count) {
      const size_t at = b0[BONE_GROUPS].offset + boneGroup * BONE_GROUP_BYTES;
      for (uint16_t k = 0, n = r.At<uint16_t>(at + 2); k < n; ++k) mesh.boneGroup.push_back(r.At<uint16_t>(at + 4 + 2 * k));
    }

    // Each mesh format is one interleaved stream; its vertex formats follow in order.
    const size_t fi = b0[MESH_FORMAT_INFO].offset + i * MESH_FORMAT_INFO_BYTES;
    const uint8_t formatCount = r.At<uint8_t>(fi);
    const uint16_t firstFormat = r.At<uint16_t>(fi + 4);
    uint16_t vf = r.At<uint16_t>(fi + 6);
    for (uint8_t f = 0; f < formatCount; ++f) {
      const size_t mf = b0[MESH_FORMATS].offset + size_t(firstFormat + f) * MESH_FORMAT_BYTES;
      const uint8_t attrCount = r.At<uint8_t>(mf + 1), stride = r.At<uint8_t>(mf + 2);
      const size_t base = buffer + bufferOffset(r.At<uint8_t>(mf)) + r.At<uint32_t>(mf + 4);
      for (uint8_t a = 0; a < attrCount; ++a, ++vf) {
        const size_t vfAt = b0[VERTEX_FORMATS].offset + size_t(vf) * VERTEX_FORMAT_BYTES;
        const uint8_t type = r.At<uint8_t>(vfAt);
        const uint16_t attrOff = r.At<uint16_t>(vfAt + 2);
        for (uint16_t v = 0; v < vertexCount; ++v) {
          const size_t p = base + size_t(v) * stride + attrOff;
          auto h = [&](int k) { return Half(r.At<uint16_t>(p + 2 * k)); };
          auto u8 = [&](int k) { return r.At<uint8_t>(p + k); };
          switch (type) {
            case POSITION: mesh.pos.push_back({r.At<float>(p), r.At<float>(p + 4), r.At<float>(p + 8)}); break;
            case NORMAL: mesh.nrm.push_back({h(0), h(1), h(2)}); break;
            case TANGENT: mesh.tan.push_back({h(0), h(1), h(2), h(3)}); break;
            case UV0: mesh.uv0.push_back({h(0), h(1)}); break;
            case BONE_WEIGHTS:
              mesh.weights.push_back({u8(0) / UNORM8_MAX, u8(1) / UNORM8_MAX, u8(2) / UNORM8_MAX, u8(3) / UNORM8_MAX});
              break;
            case BONE_INDICES: mesh.joints.push_back({u8(0), u8(1), u8(2), u8(3)}); break;
            default: break;  // colours, extra UV sets: not used
          }
        }
      }
    }
    const size_t faces = buffer + bufferOffset(INDEX_BUFFER) + size_t(firstIndex) * 2;
    for (uint32_t k = 0; k < indexCount; ++k) mesh.indices.push_back(r.At<uint16_t>(faces + 2 * k));
    m.meshes.push_back(std::move(mesh));
  }
  if (!r.ok) {
    *error = "FMDL data out of bounds";
    return std::nullopt;
  }
  return m;
}

}  // namespace pes

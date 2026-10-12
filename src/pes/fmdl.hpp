#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "pes/types.hpp"

namespace pes {

struct FmdlBone {
  std::string name;
  int parent = -1;
  Vec3 localPos, worldPos;  // render (skin) bind, Fox space
};

struct FmdlTexture {
  std::string directory, filename;  // as stored
};

struct FmdlMaterial {
  std::string name, technique, shader;
  std::vector<std::pair<std::string, int>> textures;  // role -> texture index
  std::vector<std::pair<std::string, Vec4>> params;
};

struct FmdlMesh {
  int materialIndex = -1;
  std::vector<int> boneGroup;  // local joint index -> fmdl bone index
  std::vector<Vec3> pos, nrm;
  std::vector<Vec4> tan;
  std::vector<Vec2> uv0;
  std::vector<std::array<uint16_t, 4>> joints;  // indices into boneGroup
  std::vector<Vec4> weights;
  std::vector<uint32_t> indices;  // LOD 0 only
};

struct Fmdl {
  std::vector<FmdlBone> bones;
  std::vector<FmdlTexture> textures;
  std::vector<FmdlMaterial> materials;
  std::vector<FmdlMesh> meshes;
};

// Fox model, version 2.03 ("FMDL"). Layout knowledge from FMDL-Studio-v2 (MIT).
std::optional<Fmdl> ParseFmdl(std::span<const uint8_t> blob, std::string* error);

}  // namespace pes

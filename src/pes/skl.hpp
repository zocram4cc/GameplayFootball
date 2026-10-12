#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "pes/types.hpp"

namespace pes {

struct SklBone {
  std::string name;
  int parent = -1;
  std::array<float, 12> matrix{};  // row-major 3x4 GLOBAL bind (rotation | translation)
  Vec3 Position() const { return {matrix[3], matrix[7], matrix[11]}; }
};

// Fox render skeleton (.skl): u32 version (12), u32 count, u32 recordSize
// (56), then records {u32 nameOffset (absolute), i32 parent, f32[12]}, then names.
std::optional<std::vector<SklBone>> ParseSkl(std::span<const uint8_t> blob, std::string* error);

}  // namespace pes

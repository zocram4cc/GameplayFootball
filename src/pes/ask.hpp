#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "pes/types.hpp"

namespace pes {

struct AskBone {
  std::string name;
  int parent = -1;
  Vec3 local;  // translation relative to parent
  Quat rotation;
};

// Fox animation skeleton (.ask): u32 version (1), u32 count, then 44-byte
// records {u32 pad, i32 nameOffset (relative to that field), i32 parent,
// f32 tx,ty,tz,0, f32 qx,qy,qz,qw}, then the NUL-separated name table.
std::optional<std::vector<AskBone>> ParseAsk(std::span<const uint8_t> blob, std::string* error);

}  // namespace pes

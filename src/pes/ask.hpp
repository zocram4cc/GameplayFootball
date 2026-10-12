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

// Fox animation skeleton (.ask), then a NUL-separated name table:
//   version 1: u32 1, u32 count, 44-byte records {u32 pad, i32 nameOffset
//     (relative to that field), i32 parent, f32 tx,ty,tz,0, f32 qx,qy,qz,qw};
//   version 2: u32 2, u32 count, 12 zero bytes, 28-byte records {i32
//     nameOffset, i32 parent, f32 tx,ty,tz, 0, 0} (positions only).
std::optional<std::vector<AskBone>> ParseAsk(std::span<const uint8_t> blob, std::string* error);

}  // namespace pes

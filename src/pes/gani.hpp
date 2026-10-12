#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "pes/types.hpp"

namespace pes {

enum GaniSegmentType : uint8_t {
  GANI_QUAT = 0, GANI_FLOAT = 1, GANI_VECTOR2 = 2, GANI_VECTOR3 = 3, GANI_VECTOR4 = 4,
  GANI_QUAT_DIFF = 5, GANI_VECTOR_DIFF = 6,
};
enum GaniUnitFlags : uint8_t { GANI_LOOP = 1, GANI_HERMITE = 2, GANI_STATIC = 4 };

struct GaniSegment {
  uint8_t type = GANI_QUAT;
  uint8_t componentBits = 0;
  std::vector<uint32_t> frames;  // absolute frame of each key
  std::vector<Quat> quats;       // QUAT / QUAT_DIFF
  std::vector<Vec4> vecs;        // other types; unused components zero
  std::vector<Vec4> tans;        // hermite tangents (tans[0] zero)
};

struct GaniUnit {
  uint32_t nameHash = 0;
  uint8_t flags = 0;
  std::vector<GaniSegment> segments;
};

// The clip's name is known only from its mtar entry: the u32 after the MOTION
// tag is the same constant (0xC6E937B9) in all 4,389 body clips.
struct Gani {
  uint32_t frameCount = 0;
  int frameScale = 1;
  std::vector<GaniUnit> units;
};

// Fox animation (.gani), GZ generation: full curve decode (GF gani.py).
std::optional<Gani> ParseGani(std::span<const uint8_t> blob, std::string* error);
// Problems found (keys end early, non-unit quats, quat jumps, implausible vectors).
std::vector<std::string> ValidateGani(const Gani& gani);

float AnimHalf(uint16_t bits);
Quat DequantQuat(uint32_t a, uint32_t b, uint32_t c, uint32_t signs, int bitSize);

}  // namespace pes

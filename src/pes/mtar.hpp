#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pes {

struct MtarEntry {
  uint32_t nameHash = 0;  // StrCode32 of the animation name
  uint32_t index = 0;
  uint32_t offset = 0;    // absolute offset of the .gani blob
  uint32_t size = 0;
};

struct Mtar {
  std::vector<MtarEntry> entries;
};

// PES Motion ARchive: magic 0x0BFFA89C, u32 count, 16-byte entries from 0x20.
std::optional<Mtar> ParseMtar(std::span<const uint8_t> blob, std::string* error);

}  // namespace pes

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pes {

struct FpkEntry {
  std::string path;  // '/'-separated, drive prefix and leading '/' stripped
  uint32_t offset = 0, size = 0;
};

// Fox .fpk / .fpkd pack, parsed over an in-memory blob.
struct FpkArchive {
  std::vector<FpkEntry> entries;
  static std::optional<FpkArchive> Parse(std::span<const uint8_t> blob, std::string* error);
};

}  // namespace pes

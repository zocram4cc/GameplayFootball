#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pes {

struct FtexImage {
  int pixelFormat = 0, width = 0, height = 0, mipCount = 0;
  bool cube = false;
  std::vector<std::vector<uint8_t>> mips;  // per face, largest first (cube: 6 x mipCount)
};

// Fox .ftex (PES 2019-2021 single-file variant, data inline).
std::optional<FtexImage> ParseFtex(std::span<const uint8_t> blob, std::string* error);

}  // namespace pes

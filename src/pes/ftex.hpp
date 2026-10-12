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
  int layers = 1;  // array slices (textureType & 0x10)
  std::vector<std::vector<uint8_t>> mips;  // image-major, largest first: cube 6 x mipCount, array layers x mipCount
};

// Fox .ftex (PES 2019-2021 single-file variant, data inline).
std::optional<FtexImage> ParseFtex(std::span<const uint8_t> blob, std::string* error);

}  // namespace pes

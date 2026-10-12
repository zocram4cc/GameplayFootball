#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pes/vfs.hpp"

namespace pes {

// "0x%08x" parses as a hash; anything else is a clip name, hashed with StrCode32.
uint32_t ClipId(std::string_view nameOrHash);

// The .gani bytes of the body clip with this StrCode32, searched across the
// dt13 body archives (body_anime_file0..9.mtar).
std::optional<std::vector<uint8_t>> FindBodyClip(const PesVfs& vfs, uint32_t hash, std::string* error);

}  // namespace pes

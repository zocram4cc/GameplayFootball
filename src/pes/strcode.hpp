#pragma once

#include <cstdint>
#include <string_view>

namespace pes {

// Fox Engine StrCode: CityHash 1.0.x CityHash64WithSeeds over the text plus
// its NUL terminator, seed0 = K2, seed1 = (int8)text[0] * 0x10000 + length.
uint64_t StrCode64(std::string_view text);
// Low 32 bits: gani chunk, track and bone names.
inline uint32_t StrCode32(std::string_view text) { return static_cast<uint32_t>(StrCode64(text)); }

}  // namespace pes

// Port of GameplayFootball tools/pes21_import/mtar.py.
#include "pes/mtar.hpp"

#include <cstring>

namespace pes {
namespace {

constexpr uint32_t MTAR_MAGIC = 0x0BFFA89C;
constexpr size_t MTAR_COUNT = 4;
constexpr size_t MTAR_TABLE = 0x20;
constexpr size_t MTAR_ENTRY_BYTES = 16;

uint32_t Le32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

}  // namespace

std::optional<Mtar> ParseMtar(std::span<const uint8_t> blob, std::string* error) {
  if (blob.size() < MTAR_TABLE || Le32(blob.data()) != MTAR_MAGIC) {
    *error = "not an mtar";
    return std::nullopt;
  }
  const uint32_t count = Le32(blob.data() + MTAR_COUNT);
  if (MTAR_TABLE + size_t(count) * MTAR_ENTRY_BYTES > blob.size()) {
    *error = "mtar entry table truncated";
    return std::nullopt;
  }
  Mtar m;
  m.entries.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = blob.data() + MTAR_TABLE + size_t(i) * MTAR_ENTRY_BYTES;
    MtarEntry entry{Le32(e), Le32(e + 4), Le32(e + 8), Le32(e + 12)};
    if (size_t(entry.offset) + entry.size > blob.size()) {
      *error = "mtar entry past end of archive";
      return std::nullopt;
    }
    m.entries.push_back(entry);
  }
  return m;
}

}  // namespace pes

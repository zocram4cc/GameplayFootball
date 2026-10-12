#include "pes/clips.hpp"

#include <charconv>

#include "pes/mtar.hpp"
#include "pes/strcode.hpp"

namespace pes {
namespace {

constexpr int BODY_ARCHIVE_COUNT = 10;  // dt13 body_anime_file0..9.mtar
constexpr const char* BODY_ARCHIVE = "common/anime/FoxAnim/Body/body_anime_file";
constexpr const char* BODY_ARCHIVE_EXT = ".mtar";
constexpr int HEX_BASE = 16;

}  // namespace

uint32_t ClipId(std::string_view s) {
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    uint32_t v = 0;
    const auto [end, ec] = std::from_chars(s.data() + 2, s.data() + s.size(), v, HEX_BASE);
    if (ec == std::errc() && end == s.data() + s.size()) return v;
  }
  return StrCode32(s);
}

std::optional<std::vector<uint8_t>> FindBodyClip(const PesVfs& vfs, uint32_t hash, std::string* error) {
  for (int i = 0; i < BODY_ARCHIVE_COUNT; ++i) {
    auto bytes = vfs.Read(BODY_ARCHIVE + std::to_string(i) + BODY_ARCHIVE_EXT, error);
    if (!bytes) return std::nullopt;
    auto mtar = ParseMtar(*bytes, error);
    if (!mtar) return std::nullopt;
    for (const auto& e : mtar->entries)
      if (e.nameHash == hash) return std::vector<uint8_t>(bytes->begin() + e.offset, bytes->begin() + e.offset + e.size);
  }
  *error = "no body clip with that hash";
  return std::nullopt;
}

}  // namespace pes

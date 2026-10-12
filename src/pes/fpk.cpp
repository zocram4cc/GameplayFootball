// Port of GameplayFootball tools/pes21_import/fpk.py (GzsTool-compatible layout).
#include "pes/fpk.hpp"

#include <cstring>

namespace pes {
namespace {

constexpr size_t FPK_COUNTS_OFFSET = 0x24;  // u32 fileCount, u32 refCount
constexpr size_t FPK_ENTRIES_OFFSET = 0x30;
constexpr size_t FPK_ENTRY_BYTES = 48;      // offset,pad,size,pad, strOffset,pad,strLength,pad, md5[16]
constexpr size_t FPK_ENTRY_STRING = 16;     // string struct inside an entry

uint32_t Le32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

}  // namespace

std::optional<FpkArchive> FpkArchive::Parse(std::span<const uint8_t> blob, std::string* error) {
  if (blob.size() < FPK_ENTRIES_OFFSET || std::memcmp(blob.data(), "foxfpk", 6) != 0) {
    *error = "not an fpk";
    return std::nullopt;
  }
  const uint32_t count = Le32(blob.data() + FPK_COUNTS_OFFSET);
  if (FPK_ENTRIES_OFFSET + size_t(count) * FPK_ENTRY_BYTES > blob.size()) {
    *error = "fpk entry table truncated";
    return std::nullopt;
  }
  FpkArchive fpk;
  fpk.entries.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = blob.data() + FPK_ENTRIES_OFFSET + size_t(i) * FPK_ENTRY_BYTES;
    FpkEntry entry{{}, Le32(e), Le32(e + 8)};
    const uint32_t strOff = Le32(e + FPK_ENTRY_STRING), strLen = Le32(e + FPK_ENTRY_STRING + 8);
    if (size_t(strOff) + strLen > blob.size() || size_t(entry.offset) + entry.size > blob.size()) {
      *error = "fpk entry out of bounds";
      return std::nullopt;
    }
    std::string path(reinterpret_cast<const char*>(blob.data() + strOff), strLen);
    for (char& c : path) if (c == '\\') c = '/';
    if (auto colon = path.find(':'); colon != std::string::npos) path.erase(0, colon + 1);  // drive prefix
    path.erase(0, path.find_first_not_of('/'));
    entry.path = std::move(path);
    fpk.entries.push_back(std::move(entry));
  }
  return fpk;
}

}  // namespace pes

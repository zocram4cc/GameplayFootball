#pragma once

#include <zlib.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pes {

// Appends the inflated zlib stream to *out; false on a corrupt or short stream.
inline bool Inflate(const uint8_t* src, size_t n, std::vector<uint8_t>* out) {
  z_stream z{};
  if (inflateInit(&z) != Z_OK) return false;
  z.next_in = const_cast<Bytef*>(src);
  z.avail_in = uInt(n);
  uint8_t buf[1 << 16];
  int rc;
  do {
    z.next_out = buf;
    z.avail_out = sizeof buf;
    rc = inflate(&z, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) { inflateEnd(&z); return false; }
    out->insert(out->end(), buf, buf + (sizeof buf - z.avail_out));
  } while (rc != Z_STREAM_END && z.avail_in > 0);
  inflateEnd(&z);
  return rc == Z_STREAM_END;
}

// WESYS: u8 type, 0x10, u8 flags, "WESYS" at +3, u32 zlib/raw sizes, zlib
// from +0x10 (vanilla UniformParameter "\x05\x10\xd1WESYS", constants "\xff\x10\x51WESYS").
constexpr char WESYS_TAG[] = {'W', 'E', 'S', 'Y', 'S'};
constexpr size_t WESYS_TAG_OFFSET = 3;
constexpr size_t WESYS_ZLIB_START = 0x10;

// Vanilla WESYS containers (UniformParameter.bin, BootsList.bin, the constant
// bins, ...) hold the raw payload zlib-inflated; mods ship that payload raw.
// Unwrap only when the tag is at +3, so a raw payload is never touched.
inline std::optional<std::vector<uint8_t>> UnwrapWesys(std::span<const uint8_t> blob,
                                                      const char* what, std::string* error) {
  if (blob.size() < WESYS_TAG_OFFSET + sizeof WESYS_TAG ||
      std::memcmp(blob.data() + WESYS_TAG_OFFSET, WESYS_TAG, sizeof WESYS_TAG) != 0)
    return std::vector<uint8_t>(blob.begin(), blob.end());
  if (blob.size() < WESYS_ZLIB_START) {
    *error = std::string(what) + ": truncated WESYS header";
    return std::nullopt;
  }
  std::vector<uint8_t> data;
  if (!Inflate(blob.data() + WESYS_ZLIB_START, blob.size() - WESYS_ZLIB_START, &data)) {
    *error = std::string(what) + ": corrupt WESYS zlib stream";
    return std::nullopt;
  }
  return data;
}

}  // namespace pes

#pragma once

#include <zlib.h>

#include <cstdint>
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

}  // namespace pes

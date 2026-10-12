// Port of GameplayFootball tools/pes21_import/ftex.py (parse + _read_mip).
#include "pes/ftex.hpp"

#include "pes/inflate.hpp"

#include <algorithm>
#include <cstring>

namespace pes {
namespace {

constexpr size_t FTEX_HEADER_BYTES = 64;
constexpr size_t FTEX_PIXEL_FORMAT = 8, FTEX_WIDTH = 10, FTEX_HEIGHT = 12, FTEX_MIP_COUNT = 16;
// textureType is the u32 at +0x1C (the u32 at +0x18 is 0 in every file); an
// array's slice count is the u16 at +0x22 (docs/re/pes-re-ftex.md 1).
constexpr size_t FTEX_TEXTURE_TYPE = 0x1C, FTEX_FTEXS_COUNT = 0x20, FTEX_ARRAY_SLICES = 0x22;
constexpr uint32_t TEXTURE_TYPE_CUBE = 4, TEXTURE_TYPE_ARRAY = 0x10;
constexpr size_t CHUNK_SIZE_ZERO_MEANS = 65536;  // a u16 size field of 0
constexpr int CUBE_FACES = 6;
constexpr size_t MIP_INFO_BYTES = 16;
constexpr size_t CHUNK_INFO_BYTES = 8;
constexpr uint32_t CHUNK_STORED_RAW = 0x80000000;

template <typename T>
T Load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

}  // namespace

std::optional<FtexImage> ParseFtex(std::span<const uint8_t> blob, std::string* error) {
  if (blob.size() < FTEX_HEADER_BYTES || std::memcmp(blob.data(), "FTEX", 4) != 0) {
    *error = "not an ftex";
    return std::nullopt;
  }
  if (blob[FTEX_FTEXS_COUNT] > 0) {
    *error = "ftexs sidecar files not supported";
    return std::nullopt;
  }
  FtexImage img;
  img.pixelFormat = Load<uint16_t>(&blob[FTEX_PIXEL_FORMAT]);
  img.width = Load<uint16_t>(&blob[FTEX_WIDTH]);  // width first: swapped, non-square textures shear
  img.height = Load<uint16_t>(&blob[FTEX_HEIGHT]);
  img.mipCount = blob[FTEX_MIP_COUNT];
  const uint32_t type = Load<uint32_t>(&blob[FTEX_TEXTURE_TYPE]);
  img.cube = type & TEXTURE_TYPE_CUBE;
  if (type & TEXTURE_TYPE_ARRAY) img.layers = std::max<int>(1, Load<uint16_t>(&blob[FTEX_ARRAY_SLICES]));

  size_t at = FTEX_HEADER_BYTES;
  const int images = img.cube ? CUBE_FACES : img.layers;
  for (int i = 0; i < images * img.mipCount; ++i, at += MIP_INFO_BYTES) {
    if (at + MIP_INFO_BYTES > blob.size()) { *error = "ftex mip table truncated"; return std::nullopt; }
    const uint32_t offset = Load<uint32_t>(&blob[at]), uncomp = Load<uint32_t>(&blob[at + 4]);
    const uint32_t comp = Load<uint32_t>(&blob[at + 8]);
    const uint16_t chunks = Load<uint16_t>(&blob[at + 14]);
    std::vector<uint8_t> mip;
    if (chunks == 0) {
      const size_t n = comp ? comp : uncomp;
      if (size_t(offset) + n > blob.size()) { *error = "ftex mip out of bounds"; return std::nullopt; }
      if (!comp) mip.assign(blob.begin() + offset, blob.begin() + offset + n);
      else if (!Inflate(&blob[offset], n, &mip)) { *error = "ftex zlib error"; return std::nullopt; }
    } else {
      for (uint16_t c = 0; c < chunks; ++c) {
        const size_t ci = size_t(offset) + c * CHUNK_INFO_BYTES;
        if (ci + CHUNK_INFO_BYTES > blob.size()) { *error = "ftex chunk table truncated"; return std::nullopt; }
        auto size = [&](size_t f) { const uint16_t v = Load<uint16_t>(&blob[ci + f]); return v ? size_t(v) : CHUNK_SIZE_ZERO_MEANS; };
        const size_t cs = size(0), us = size(2);
        uint32_t rel = Load<uint32_t>(&blob[ci + 4]);
        // Only the flag marks a stored chunk: official unflagged chunks are zlib
        // even when the sizes are equal. Inflate failing with equal sizes is a
        // mod tool's stored chunk; take it raw (pes-re-ftex.md 2.2).
        const bool raw = rel & CHUNK_STORED_RAW;
        rel &= ~CHUNK_STORED_RAW;
        const size_t src = size_t(offset) + rel;
        if (src + cs > blob.size()) { *error = "ftex chunk out of bounds"; return std::nullopt; }
        std::vector<uint8_t> data;
        if (!raw && !Inflate(&blob[src], cs, &data)) {
          if (cs != us) { *error = "ftex zlib error"; return std::nullopt; }
          data.clear();
        }
        if (raw || data.empty()) data.assign(blob.begin() + src, blob.begin() + src + cs);
        mip.insert(mip.end(), data.begin(), data.end());
      }
    }
    img.mips.push_back(std::move(mip));
  }
  return img;
}

}  // namespace pes

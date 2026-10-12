#include "gdext/pes_texture.hpp"

#include <godot_cpp/classes/cubemap.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pes/ftex.hpp"

namespace godot {
namespace {

constexpr int CUBE_FACE_COUNT = 6;

// ftex pixelFormat values (GF ftex.py FORMATS table).
enum PesPixelFormat {
  PF_RGBA8 = 0, PF_R8 = 1, PF_DXT1 = 2, PF_DXT3 = 3, PF_DXT5 = 4, PF_BC4 = 8, PF_BC5 = 9, PF_BC6H = 10,
  PF_BC7 = 11, PF_RGBA16F = 12, PF_RGBA32F = 13, PF_R10G10B10A2 = 14, PF_R11G11B10F = 15,
};
constexpr int BLOCK_DIM = 4;
constexpr int BC_SMALL_BLOCK_BYTES = 8;   // DXT1, BC4
constexpr int BC_LARGE_BLOCK_BYTES = 16;  // DXT3/5, BC5, BC6H, BC7

struct FormatInfo {
  Image::Format format;
  int blockBytes;  // 0 = uncompressed
  int pixelBytes;
};

bool Lookup(int pf, FormatInfo* out) {
  switch (pf) {
    case PF_RGBA8: *out = {Image::FORMAT_RGBA8, 0, 4}; return true;
    case PF_R8: *out = {Image::FORMAT_R8, 0, 1}; return true;
    case PF_DXT1: *out = {Image::FORMAT_DXT1, BC_SMALL_BLOCK_BYTES, 0}; return true;
    case PF_DXT3: *out = {Image::FORMAT_DXT3, BC_LARGE_BLOCK_BYTES, 0}; return true;
    case PF_DXT5: *out = {Image::FORMAT_DXT5, BC_LARGE_BLOCK_BYTES, 0}; return true;
    case PF_BC4: *out = {Image::FORMAT_RGTC_R, BC_SMALL_BLOCK_BYTES, 0}; return true;
    case PF_BC5: *out = {Image::FORMAT_RGTC_RG, BC_LARGE_BLOCK_BYTES, 0}; return true;
    case PF_BC6H: *out = {Image::FORMAT_BPTC_RGBFU, BC_LARGE_BLOCK_BYTES, 0}; return true;
    case PF_BC7: *out = {Image::FORMAT_BPTC_RGBA, BC_LARGE_BLOCK_BYTES, 0}; return true;
    case PF_RGBA16F: *out = {Image::FORMAT_RGBAH, 0, 8}; return true;
    case PF_RGBA32F: *out = {Image::FORMAT_RGBAF, 0, 16}; return true;
    case PF_R10G10B10A2: case PF_R11G11B10F: *out = {Image::FORMAT_RGBAF, 0, 16}; return true;  // CPU-converted
    default: return false;
  }
}

size_t MipBytes(const FormatInfo& f, int w, int h) {
  if (f.blockBytes) return size_t((w + BLOCK_DIM - 1) / BLOCK_DIM) * ((h + BLOCK_DIM - 1) / BLOCK_DIM) * f.blockBytes;
  return size_t(w) * h * f.pixelBytes;
}

// Unsigned 11/10-bit floats (no sign; 5-bit exponent, bias 15) -> float.
float SmallFloat(uint32_t bits, int mantissaBits) {
  const uint32_t exp = bits >> mantissaBits, mant = bits & ((1u << mantissaBits) - 1);
  constexpr int EXP_BIAS = 15;
  constexpr uint32_t EXP_MAX = 31;
  if (exp == 0) return std::ldexp(float(mant), 1 - EXP_BIAS - mantissaBits);
  if (exp == EXP_MAX) return mant ? NAN : INFINITY;
  return std::ldexp(float(mant | (1u << mantissaBits)), int(exp) - EXP_BIAS - mantissaBits);
}

// R10G10B10A2_UNORM and R11G11B10_FLOAT have no Godot format: expand to RGBA32F.
std::vector<uint8_t> ExpandPacked(int pf, const std::vector<uint8_t>& src) {
  constexpr float UNORM10_MAX = 1023.0f, UNORM2_MAX = 3.0f;
  std::vector<uint8_t> out(src.size() / 4 * 16);
  for (size_t i = 0; i + 4 <= src.size(); i += 4) {
    uint32_t v;
    std::memcpy(&v, &src[i], 4);
    float px[4];
    if (pf == PF_R10G10B10A2) {
      px[0] = (v & 0x3FF) / UNORM10_MAX;
      px[1] = ((v >> 10) & 0x3FF) / UNORM10_MAX;
      px[2] = ((v >> 20) & 0x3FF) / UNORM10_MAX;
      px[3] = (v >> 30) / UNORM2_MAX;
    } else {
      px[0] = SmallFloat(v & 0x7FF, 6);
      px[1] = SmallFloat((v >> 11) & 0x7FF, 6);
      px[2] = SmallFloat(v >> 22, 5);
      px[3] = 1.0f;
    }
    std::memcpy(&out[i * 4], px, sizeof px);
  }
  return out;
}

}  // namespace

// One Godot image per ftex image (2D: one; cube: 6 faces; array: one per slice).
std::vector<Ref<Image>> LoadPesImages(const pes::PesVfs& vfs, const std::string& path, std::string* error,
                                      PesTextureKind* kind) {
  auto bytes = vfs.Read(path, error);
  if (!bytes) return {};
  auto ftex = pes::ParseFtex(*bytes, error);
  if (!ftex) return {};
  FormatInfo f;
  if (!Lookup(ftex->pixelFormat, &f)) { *error = "unhandled ftex pixel format " + std::to_string(ftex->pixelFormat); return {}; }
  if (kind) *kind = ftex->cube ? PesTextureKind::Cube : ftex->layers > 1 ? PesTextureKind::Array : PesTextureKind::Flat;
  const int images = ftex->cube ? CUBE_FACE_COUNT : ftex->layers;

  // Godot wants a full chain down to 1x1 or none at all; layered textures share
  // one chain length across layers.
  const int fullChain = int(std::floor(std::log2(std::max(ftex->width, ftex->height)))) + 1;
  // PES often stores the chain down to ~4 texels only (shirts_normal: 10 of 12
  // levels). Missing tail levels are completed below; without mipmaps the
  // normal arrays alias into moire.
  int mips = ftex->mipCount > 1 ? fullChain : 1;
  std::vector<std::vector<uint8_t>> data(images);
  for (int i = 0; i < images; ++i) {
    std::vector<uint8_t> lastLevel;  // the previous level, decoded
    for (int m = 0, w = ftex->width, h = ftex->height; m < mips; ++m, w = std::max(1, w / 2), h = std::max(1, h / 2)) {
      if (m >= ftex->mipCount) {
        // Below the stored chain: repeat the start of the last level as decoded.
        // For block formats these levels are one block each, sampled only at a
        // few pixels' size. ponytail: not a true box filter.
        const size_t n = MipBytes(f, w, h);
        for (size_t k = 0; k < n; ++k) data[i].push_back(lastLevel[k % lastLevel.size()]);
        continue;
      }
      std::vector<uint8_t> mip = ftex->mips[size_t(i) * ftex->mipCount + m];
      if (ftex->pixelFormat == PF_R10G10B10A2 || ftex->pixelFormat == PF_R11G11B10F) mip = ExpandPacked(ftex->pixelFormat, mip);
      if (mip.size() != MipBytes(f, w, h)) {
        if (m == 0) { *error = "ftex mip 0 size mismatch: " + path; return {}; }
        mips = 1;  // inconsistent chain: keep mip 0 only, for every layer
        for (auto& d : data) d.resize(std::min(d.size(), MipBytes(f, ftex->width, ftex->height)));
        break;
      }
      if (ftex->pixelFormat == PF_RGBA8)
        for (size_t k = 0; k + 3 < mip.size(); k += 4) std::swap(mip[k], mip[k + 2]);  // stored BGRA
      data[i].insert(data[i].end(), mip.begin(), mip.end());
      lastLevel = std::move(mip);
    }
  }
  std::vector<Ref<Image>> out;
  for (auto& d : data) {
    d.resize(mips > 1 ? d.size() : MipBytes(f, ftex->width, ftex->height));
    PackedByteArray pba;
    pba.resize(int64_t(d.size()));
    std::memcpy(pba.ptrw(), d.data(), d.size());
    Ref<Image> img = Image::create_from_data(ftex->width, ftex->height, mips > 1, f.format, pba);
    if (img.is_null() || img->is_empty()) { *error = "Godot rejected image data: " + path; return {}; }
    out.push_back(img);
  }
  return out;
}

Ref<Image> LoadPesImage(const pes::PesVfs& vfs, const std::string& path, std::string* error) {
  PesTextureKind kind;
  auto images = LoadPesImages(vfs, path, error, &kind);
  if (images.empty()) return {};
  if (kind != PesTextureKind::Flat) { *error = "layered texture, not a 2D image: " + path; return {}; }
  return images[0];
}

Ref<Texture> LoadPesTexture(const pes::PesVfs& vfs, const std::string& path, std::string* error) {
  PesTextureKind kind;
  auto images = LoadPesImages(vfs, path, error, &kind);
  if (images.empty()) return {};
  if (kind == PesTextureKind::Flat) {
    Ref<ImageTexture> t = ImageTexture::create_from_image(images[0]);
    return t;
  }
  TypedArray<Image> layers;
  for (auto& img : images) layers.push_back(img);
  if (kind == PesTextureKind::Cube) {
    Ref<Cubemap> cube;
    cube.instantiate();
    if (cube->create_from_images(layers) != OK) { *error = "Godot rejected cube faces: " + path; return {}; }
    return cube;
  }
  Ref<Texture2DArray> array;
  array.instantiate();
  if (array->create_from_images(layers) != OK) { *error = "Godot rejected array slices: " + path; return {}; }
  return array;
}

}  // namespace godot

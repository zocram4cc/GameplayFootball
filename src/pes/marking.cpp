// Marking compositor geometry: docs/re/pes-re-markings.md 2.1-2.2 (digits,
// FUN_1419e4a90 / FUN_1419e2c10) and 3.1-3.3 (names, FUN_1419e1df0).
#include "pes/marking.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pes/fpk.hpp"

namespace pes {
namespace {

template <typename T>
T Load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

// Digit strips: horizontal cells of 0.09765625 (200/2048) pitch, digits >= 2
// shifted left by 60/2048; "1" is narrower and centred by adj.
constexpr float H_PITCH = 0.09765625f, H_SHIFT = 0.029296875f, H_ONE_W = 0.048828125f, H_W = 0.078125f;
constexpr float H_ONE_ADJ = 0.1875f;
constexpr float V_PITCH = 0.099609375f, V_ONE_W = 0.75f, V_ONE_ADJ = 0.15625f;

// Two/three-digit offsets, fractions of the region width.
constexpr float A = 0.0520833f, B = 0.1041667f, A2 = 0.0520867f;
constexpr float SPACING_STEP = 0.08f;  // f = 0.08 (s - 1)

// Name space and curvature.
constexpr float NAME_SPACE = 512.0f, NAME_CENTRE = 256.0f, NAME_ROW_Y = 64.0f;
constexpr float GLYPH_HALF_HEIGHT = 62.0f * 0.98f;
constexpr float CURVED_GAP = 6.0f;
constexpr float CURVE_RADIUS[] = {0.0f, 672.0f, 336.0f, 292.0f};  // by NameShape
constexpr float CURVE_ARC = 0.8f, CURVE_CENTRE = 0.89f, CURVE_WIDTH = 0.95f;

constexpr uint64_t NAME_GLYPH_TABLE_VA = 0x142a96ce0;
constexpr size_t NAME_GLYPHS = 78, NAME_GLYPH_BYTES = 16;
constexpr const char* COMMON_PACKAGE = "Asset/model/character/#Win/common_package.fpk";
constexpr const char* NAME_FONT_MEMBER = "/info_name_font.bin";
constexpr size_t NAME_FONT_RECORD = 12;

}  // namespace

DigitUv DigitCell(int d, bool vertical) {
  if (vertical) return {0.0f, d * V_PITCH, d == 1 ? V_ONE_W : 1.0f, V_PITCH, d == 1 ? V_ONE_ADJ : 0.0f};
  return {d * H_PITCH - (d > 1 ? H_SHIFT : 0.0f), 0.0f, d == 1 ? H_ONE_W : H_W, 1.0f, d == 1 ? H_ONE_ADJ : 0.0f};
}

std::vector<PlacedDigit> NumberLayout(int n, float s, bool zeroPad) {
  constexpr int MAX_NUMBER = 1000;
  if (n < 1 || n >= MAX_NUMBER) return {};
  const float f = SPACING_STEP * (s - 1.0f);
  auto adj = [](int d) { return DigitCell(d, false).adj; };
  if (n < 10 && !zeroPad) return {{n, 0.25f + adj(n) / 2, 0.5f - adj(n)}};
  if (n < 100) {
    const int t = n / 10, u = n % 10;
    float oT = -f, oU = f;
    if (n == 11) oT = B - f, oU = f;
    else if (u == 1) oT = A - f, oU = f + A;
    else if (t == 1) oT = A - f, oU = f - A;
    return {{t, oT, 0.5f - adj(t)}, {u, 0.5f + oU, 0.5f - adj(u)}};
  }
  const int h = n / 100, t = n / 10 % 10, u = n % 10;
  const bool h1 = h == 1;
  float oH, oT, oU;
  if (t == 1 && u == 1) oH = B - f, oT = h1 ? 0 : A, oU = h1 ? f - B : f - A;
  else if (t == 1) oH = A - f, oT = h1 ? -A : 0, oU = h1 ? f - B : f - A;
  else if (u == 1) oH = A - f, oT = h1 ? 0 : A2, oU = h1 ? f - A : f;
  else oH = -f, oT = h1 ? -A : 0, oU = h1 ? f - A : f;
  constexpr float THIRD = 1.0f / 3.0f;
  return {{h, adj(h) / 2 + oH, THIRD - adj(h)},
          {t, THIRD + adj(t) / 2 + oT, THIRD - adj(t)},
          {u, 2 * THIRD + adj(u) / 2 + oU, THIRD - adj(u)}};
}

std::vector<GlyphQuad> NameLayout(const std::vector<float>& widths, int shape) {
  std::vector<GlyphQuad> out(widths.size());
  if (widths.empty()) return out;
  float total = 0;
  for (float w : widths) total += w;
  float N = std::floor(total / 2);
  const int m = shape & 3;
  float gap = 0;
  if (m != 0 && N < NAME_SPACE) gap = CURVED_GAP, N += CURVED_GAP * float(widths.size() - 1);
  const float scale = std::min(1.0f, NAME_SPACE / N);
  N = std::min(N, NAME_SPACE);
  // PES's glyph loop walks the cursor right to left and places each glyph at
  // cursor - gw/2 (FUN_1419e1df0 6.3), so the last code point is written first
  // and the first code point ends up at the LEFT of the band - already reading
  // order. NameBlitOf then mirrors the band for the shirt back's UV direction.
  float cursor = NAME_CENTRE + N / 2;
  for (size_t k = widths.size(); k-- > 0;) {
    const float gw = widths[k] * scale / 2, centre = cursor - gw / 2;
    GlyphQuad& q = out[k];
    q.w = gw, q.h = GLYPH_HALF_HEIGHT;
    if (m == 0) {
      q.cx = centre, q.cy = NAME_ROW_Y;
    } else {
      const float R = CURVE_RADIUS[m], th = (centre - NAME_CENTRE) / R;
      q.cx = NAME_CENTRE + CURVE_ARC * R * std::sin(th);
      q.cy = CURVE_CENTRE * R - CURVE_ARC * R * std::cos(th);
      q.angle = th;
      q.w *= scale * CURVE_WIDTH;
    }
    cursor = centre - gap - gw / 2;
  }
  return out;
}

NameBlit NameBlitOf(const GlyphQuad& q, const NameGlyph& g, int canvasW, int canvasH, float regionW,
                    float nameSpaceW, float nameSpaceH) {
  NameBlit out;
  // name space -> canvas px, x and y both scaled by the region's x ratio (x2
  // for the 1600x256 canvas), y flipped (FUN_1419e1df0 6.5).
  const float sx = regionW * canvasW / nameSpaceW, sy = regionW * canvasW / nameSpaceW;
  // The shirt back's UV.x runs opposite to the front's, so the name region is
  // mirrored here as well: together with the y flip each glyph lands turned 180
  // degrees and the band keeps the viewer's reading order. The garment shader's
  // name scales are both positive, which un-does both flips on the shirt.
  out.cx = (nameSpaceW - q.cx) * sx;
  out.cy = (nameSpaceH - q.cy) * sy;
  out.w = q.w * sx, out.h = q.h * sy;
  out.angle = -q.angle;
  // The glyph cell in the page's UV; page 1 is 1024x512, everything else 1024x256.
  const float pw = 1024.0f, ph = g.page == 1 ? 512.0f : 256.0f;
  out.u0 = g.x / pw, out.v0 = g.y / ph, out.uw = g.w / pw, out.vh = g.h / ph;
  return out;
}

std::optional<std::vector<NameGlyph>> ReadNameGlyphs(const PeImage& exe, std::string* error) {
  uint8_t table[NAME_GLYPHS * NAME_GLYPH_BYTES];
  if (!exe.ReadVa(NAME_GLYPH_TABLE_VA, sizeof table, table)) {
    *error = "name glyph table unreadable: not the PES 2021 executable this table was found in";
    return std::nullopt;
  }
  std::vector<NameGlyph> out;
  for (size_t i = 0; i < NAME_GLYPHS; ++i) {
    const uint8_t* r = &table[i * NAME_GLYPH_BYTES];
    out.push_back({Load<uint32_t>(r), Load<int32_t>(r + 4), Load<uint16_t>(r + 8), Load<uint16_t>(r + 10),
                   Load<uint16_t>(r + 12), Load<uint16_t>(r + 14)});
  }
  return out;
}

const NameGlyph* FindGlyph(const std::vector<NameGlyph>& glyphs, uint32_t code) {
  for (const auto& g : glyphs)
    if (g.code == code) return &g;
  return nullptr;
}

std::optional<NameFontMap> LoadNameFontMap(const PesVfs& vfs, std::string* error) {
  auto pack = vfs.Read(COMMON_PACKAGE, error);
  auto fpk = pack ? FpkArchive::Parse(*pack, error) : std::nullopt;
  if (!fpk) return std::nullopt;
  for (const auto& e : fpk->entries)
    if (e.path.ends_with(NAME_FONT_MEMBER)) {
      NameFontMap map;
      for (size_t at = 0; at + NAME_FONT_RECORD <= e.size; at += NAME_FONT_RECORD) {
        const uint8_t* r = pack->data() + e.offset + at;
        map[Load<uint32_t>(r)] = Load<uint32_t>(r + 8);
      }
      return map;
    }
  *error = "common_package.fpk has no info_name_font.bin";
  return std::nullopt;
}

std::vector<uint32_t> NameCodes(const NameFontMap& map, const std::string& s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    // One UTF-8 code point, its bytes packed big-endian (0xC389 for 'É').
    const uint8_t lead = uint8_t(s[i]);
    const size_t n = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    uint32_t code = 0;
    for (size_t k = 0; k < n && i + k < s.size(); ++k) code = code << 8 | uint8_t(s[i + k]);
    i += n;
    auto it = map.find(code);
    if (it == map.end()) continue;  // not in the map: dropped
    for (int shift = 24; shift >= 0; shift -= 8)
      if (uint32_t c = (it->second >> shift) & 0xFF) out.push_back(c);
  }
  return out;
}

}  // namespace pes

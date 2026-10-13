// CPU raster of PES's marking textures. Geometry: pes/marking (FUN_1419e2c10,
// FUN_1419e1df0); regions and canvas: docs/re/pes-re-markings.md 2.3, 3.3, 3.5.
#include "gdext/pes_marking.hpp"

#include <algorithm>
#include <cmath>

#include "gdext/pes_texture.hpp"

namespace godot {
namespace {

constexpr int MARKING_W = 1600, MARKING_H = 256, LEG_SIZE = 128;
// Regions (x0, y0, w, h) in canvas fractions and their vertical padding (px).
struct Region { float x0, y0, w, h, padPx; };
constexpr Region BACK_REGION = {0.64f, 0.0f, 0.24f, 1.0f, 0.0f};
constexpr Region CHEST_REGION = {0.88f, 0.0f, 0.12f, 0.5f, 26.0f / 0.5f};
constexpr Region LEG_REGION = {0.0f, 0.0f, 1.0f, 1.0f, 26.0f};
constexpr float NAME_REGION_W = 0.64f;     // name area: u in [0, 0.64)
constexpr float NAME_SPACE_W = 512.0f, NAME_SPACE_H = 128.0f;
constexpr float SPACING_STEPS = 14.0f;     // BackNumberSpacing/14 (FUN_141e58c80)
constexpr const char* KIT_TEXTURE_DIR = "Asset/model/character/uniform/texture/#windx11/";
constexpr const char* NAME_EX_SUFFIX = "_ex";

// A decoded RGBA8 source with bilinear sampling in UV.
struct Rgba {
  int w = 0, h = 0;
  PackedByteArray px;
  bool ok() const { return w > 0; }
  Color At(float u, float v) const {
    const float x = std::clamp(u * w - 0.5f, 0.0f, float(w - 1)), y = std::clamp(v * h - 0.5f, 0.0f, float(h - 1));
    const int x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float fx = x - x0, fy = y - y0;
    auto c = [&](int xx, int yy) {
      const int64_t i = (int64_t(yy) * w + xx) * 4;
      return Color(px[i] / 255.0f, px[i + 1] / 255.0f, px[i + 2] / 255.0f, px[i + 3] / 255.0f);
    };
    return c(x0, y0).lerp(c(x1, y0), fx).lerp(c(x0, y1).lerp(c(x1, y1), fx), fy);
  }
};

Rgba Decode(const pes::PesVfs& vfs, const std::string& file) {
  Rgba out;
  if (file.empty()) return out;
  std::string err;
  Ref<Image> img = LoadPesImage(vfs, std::string(KIT_TEXTURE_DIR) + file + ".ftex", &err);
  if (img.is_null()) return out;
  img->decompress();
  img->clear_mipmaps();
  img->convert(Image::FORMAT_RGBA8);
  out.w = img->get_width(), out.h = img->get_height(), out.px = img->get_data();
  return out;
}

// Draws src[u0..u0+uw, v0..v0+vh] into the canvas rect, rotated by `angle`
// about its centre, optionally mirrored in x and/or y; source-over.
void Blit(PackedByteArray& dst, int dw, int dh, const Rgba& src, float u0, float v0, float uw, float vh,
          float cx, float cy, float w, float h, float angle, bool mirrorX, bool mirrorY = false) {
  const float c = std::cos(angle), s = std::sin(angle), reach = 0.5f * std::sqrt(w * w + h * h);
  const int xmin = std::max(0, int(cx - reach)), xmax = std::min(dw - 1, int(cx + reach) + 1);
  const int ymin = std::max(0, int(cy - reach)), ymax = std::min(dh - 1, int(cy + reach) + 1);
  for (int y = ymin; y <= ymax; ++y)
    for (int x = xmin; x <= xmax; ++x) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      const float lx = c * dx + s * dy, ly = -s * dx + c * dy;  // into the quad's frame
      if (std::fabs(lx) > w / 2 || std::fabs(ly) > h / 2) continue;
      float fu = lx / w + 0.5f;
      if (mirrorX) fu = 1.0f - fu;
      float fv = ly / h + 0.5f;
      if (mirrorY) fv = 1.0f - fv;
      const Color sc = src.At(u0 + fu * uw, v0 + fv * vh);
      if (sc.a <= 0) continue;
      const int64_t i = (int64_t(y) * dw + x) * 4;
      const float da = dst[i + 3] / 255.0f, oa = sc.a + da * (1 - sc.a);
      auto mix = [&](int k, float sv) {
        const float dv = dst[i + k] / 255.0f;
        return uint8_t(std::lround(255 * (oa > 0 ? (sv * sc.a + dv * da * (1 - sc.a)) / oa : sv)));
      };
      dst.set(i, mix(0, sc.r)), dst.set(i + 1, mix(1, sc.g)), dst.set(i + 2, mix(2, sc.b));
      dst.set(i + 3, uint8_t(std::lround(255 * oa)));
    }
}

PackedByteArray Canvas(int w, int h) {
  PackedByteArray px;  // clear colour 0x00FFFFFF: white, alpha 0
  px.resize(int64_t(w) * h * 4);
  for (int64_t i = 0; i < px.size(); i += 4) px.set(i, 255), px.set(i + 1, 255), px.set(i + 2, 255), px.set(i + 3, 0);
  return px;
}

// One number into its region (FUN_1419e2c10); rotate draws along v (leg).
bool DrawNumber(PackedByteArray& dst, int dw, int dh, const Rgba& strip, const Region& r, int number,
                const pes::KitConfig& kit, bool rotate) {
  if (!strip.ok()) return false;
  const bool vertical = strip.w < strip.h;
  const auto digits = pes::NumberLayout(number, kit.backNumberSpacing / SPACING_STEPS, kit.backNumberType != 0);
  const float rx = r.x0 * dw, ry = r.y0 * dh, rw = r.w * dw, rh = r.h * dh;
  // Digits sit pad/2/H down the region, (H - pad)/H of it tall (H = canvas height).
  const float y0 = r.padPx / 2 / dh * rh, hh = (dh - r.padPx) / float(dh) * rh;
  for (const auto& d : digits) {
    const auto cell = pes::DigitCell(d.digit, vertical);
    const float u0 = cell.u, uw = cell.w, v0 = cell.v, vh = cell.h;
    if (!rotate) {
      Blit(dst, dw, dh, strip, u0, v0, uw, vh, rx + (d.x + d.w / 2) * rw, ry + y0 + hh / 2, d.w * rw, hh, 0, false);
    } else {
      // ponytail: leg digits run along v, rotated 90 degrees; the direction is
      // INF until a live leg texture is decoded.
      Blit(dst, dw, dh, strip, u0, v0, uw, vh, rx + y0 + hh / 2, ry + (d.x + d.w / 2) * rh, d.w * rh, hh,
           float(M_PI / 2), false);
    }
  }
  return !digits.empty();
}

Ref<Image> Finish(PackedByteArray px, int w, int h) {
  Ref<Image> img = Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, px);
  img->generate_mipmaps();
  return img;
}

}  // namespace

Ref<Image> ComposeMarking(const pes::PesVfs& vfs, const pes::KitConfig& kit, const MarkingFonts& fonts, int number,
                          const std::string& name, float* nameWidthSum) {
  PackedByteArray px = Canvas(MARKING_W, MARKING_H);
  bool drew = DrawNumber(px, MARKING_W, MARKING_H, Decode(vfs, kit.backNumbersFile), BACK_REGION, number, kit, false);
  drew |= DrawNumber(px, MARKING_W, MARKING_H, Decode(vfs, kit.chestNumbersFile), CHEST_REGION, number, kit, false);

  // Name (FUN_1419e1df0): page 0 = _name, page 1 = _name_ex (v over 512 px).
  *nameWidthSum = 0;
  const Rgba page0 = Decode(vfs, kit.nameFontFile);
  const Rgba page1 = kit.nameFontFile.empty() ? Rgba{} : Decode(vfs, kit.nameFontFile + NAME_EX_SUFFIX);
  if (page0.ok() && !name.empty()) {
    // Without a _name_ex page every code folds through info_name_font.bin.
    std::vector<uint32_t> codes = pes::NameCodes(fonts.nameFont, name);
    std::vector<const pes::NameGlyph*> glyphs;
    std::vector<float> widths;
    for (uint32_t code : codes)
      if (const auto* g = pes::FindGlyph(fonts.glyphs, code); g && (g->page == 0 || page1.ok()))
        glyphs.push_back(g), widths.push_back(float(g->w)), *nameWidthSum += float(g->w);
    const auto quads = pes::NameLayout(widths, kit.nameShape);
    for (size_t i = 0; i < quads.size(); ++i) {
      const auto b = pes::NameBlitOf(quads[i], *glyphs[i], MARKING_W, MARKING_H, NAME_REGION_W, NAME_SPACE_W,
                                     NAME_SPACE_H);
      const Rgba& page = glyphs[i]->page == 0 ? page0 : page1;
      // The canvas holds the glyph page turned 180 degrees: the exe emits the
      // quad mirrored in x and flips y on the canvas write (FUN_1419e1df0
      // 6.5), and the garment shader's name scales are both positive.
      Blit(px, MARKING_W, MARKING_H, page, b.u0, b.v0, b.uw, b.vh, b.cx, b.cy, b.w, b.h, b.angle, b.mirrorU,
           b.mirrorV);
      drew = true;
    }
  }
  return drew ? Finish(px, MARKING_W, MARKING_H) : Ref<Image>();
}

Ref<Image> ComposeLegNumber(const pes::PesVfs& vfs, const pes::KitConfig& kit, int number) {
  PackedByteArray px = Canvas(LEG_SIZE, LEG_SIZE);
  if (!DrawNumber(px, LEG_SIZE, LEG_SIZE, Decode(vfs, kit.legNumbersFile), LEG_REGION, number, kit, true)) return {};
  return Finish(px, LEG_SIZE, LEG_SIZE);
}

}  // namespace godot

#pragma once

// Layout rules of PES 2021's per-player marking texture: shirt numbers and
// print names (docs/re/pes-re-markings.md 2-3). Rasterising is done by the
// Godot side; this is the geometry.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace pes {

// One digit's cell in a _back/_chest/_leg strip, in UV; adj narrows "1".
struct DigitUv { float u = 0, v = 0, w = 0, h = 0, adj = 0; };
DigitUv DigitCell(int digit, bool vertical);  // FUN_1419e4a90

// A digit placed in its number region: x and w are fractions of the region width.
struct PlacedDigit { int digit = 0; float x = 0, w = 0; };
// FUN_1419e2c10. spacing = BackNumberSpacing/14 converted; n < 1 or >= 1000: none.
std::vector<PlacedDigit> NumberLayout(int number, float spacing, bool zeroPad);

// A name glyph quad in name space (512 x 128 units), x mirrored when drawn.
struct GlyphQuad { float cx = 0, cy = 0, w = 0, h = 0, angle = 0; };
// FUN_1419e1df0. widths: glyph-table px per glyph; shape: NameShape 0..3.
std::vector<GlyphQuad> NameLayout(const std::vector<float>& widths, int shape);

struct NameGlyph { uint32_t code = 0; int page = 0; int x = 0, y = 0, w = 0, h = 0; };
// The game's 78-entry glyph table (DAT_142a96ce0), read from the user's exe.
std::optional<std::vector<NameGlyph>> ReadNameGlyphs(const PeImage& exe, std::string* error);
const NameGlyph* FindGlyph(const std::vector<NameGlyph>& glyphs, uint32_t code);

// Where one glyph lands on the marking canvas, and which corner of its glyph
// cell the canvas takes. PES emits the quad mirrored in x and flips y on the
// canvas write (FUN_1419e1df0 §6.5), so the finished texture holds the glyph
// page rotated 180 degrees: the garment shader's positive name scales then
// read it right way up.
struct NameBlit {
  float cx = 0, cy = 0, w = 0, h = 0, angle = 0;  // canvas px, quad frame
  float u0 = 0, v0 = 0, uw = 0, vh = 0;           // the glyph cell in page UV
  bool mirrorU = true, mirrorV = true;            // per-glyph page flips
};
// canvas: the 1600x256 marking texture; nameSpaceW/H: 512/128.
NameBlit NameBlitOf(const GlyphQuad& q, const NameGlyph& g, int canvasW, int canvasH, float regionW,
                    float nameSpaceW, float nameSpaceH);

// info_name_font.bin (common_package.fpk): code -> ASCII replacement.
using NameFontMap = std::unordered_map<uint32_t, uint32_t>;
std::optional<NameFontMap> LoadNameFontMap(const PesVfs& vfs, std::string* error);
// UTF-8 print name -> glyph codes through the map (FUN_1419e4760, no _name_ex).
std::vector<uint32_t> NameCodes(const NameFontMap& map, const std::string& utf8);

}  // namespace pes

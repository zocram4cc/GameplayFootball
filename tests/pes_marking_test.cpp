#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "pes/exe.hpp"
#include "pes/marking.hpp"
#include "pes/vfs.hpp"

// Layout rules of the marking compositor (docs/re/pes-re-markings.md 2-3).

TEST(DigitCell, HorizontalStripCells) {
  // In 2048-px units: 0 -> [0,160), 1 -> [200,300), 2 -> [340,500).
  constexpr float STRIP_PX = 2048.0f;
  auto c0 = pes::DigitCell(0, false), c1 = pes::DigitCell(1, false), c2 = pes::DigitCell(2, false);
  EXPECT_NEAR(c0.u * STRIP_PX, 0.0f, 0.5f);
  EXPECT_NEAR(c0.w * STRIP_PX, 160.0f, 0.5f);
  EXPECT_NEAR(c1.u * STRIP_PX, 200.0f, 0.5f);
  EXPECT_NEAR(c1.w * STRIP_PX, 100.0f, 0.5f);
  EXPECT_NEAR(c2.u * STRIP_PX, 340.0f, 0.5f);
  EXPECT_FLOAT_EQ(c1.adj, 0.1875f);
  EXPECT_FLOAT_EQ(c2.adj, 0.0f);
}

TEST(DigitCell, VerticalStripCells) {
  auto c3 = pes::DigitCell(3, true), c1 = pes::DigitCell(1, true);
  EXPECT_FLOAT_EQ(c3.v, 3 * 0.099609375f);
  EXPECT_FLOAT_EQ(c3.h, 0.099609375f);
  EXPECT_FLOAT_EQ(c3.w, 1.0f);
  EXPECT_FLOAT_EQ(c1.w, 0.75f);
  EXPECT_FLOAT_EQ(c1.adj, 0.15625f);
}

TEST(NumberLayout, OneDigitIsCentred) {
  auto g = pes::NumberLayout(7, 1.0f, false);
  ASSERT_EQ(g.size(), 1u);
  EXPECT_EQ(g[0].digit, 7);
  EXPECT_FLOAT_EQ(g[0].x, 0.25f);
  EXPECT_FLOAT_EQ(g[0].w, 0.5f);
}

TEST(NumberLayout, TwoDigitsFollowTheOffsetTable) {
  constexpr float A = 0.0520833f, SPACING = 2.0f, F = 0.08f * (SPACING - 1.0f);
  auto g = pes::NumberLayout(10, SPACING, false);  // tens == 1 row: oT = a - f, oU = f - a
  ASSERT_EQ(g.size(), 2u);
  auto c1 = pes::DigitCell(1, false), c0 = pes::DigitCell(0, false);
  EXPECT_NEAR(g[0].x, A - F, 1e-5f);
  EXPECT_NEAR(g[0].w, 0.5f - c1.adj, 1e-6f);
  EXPECT_NEAR(g[1].x, 0.5f + F - A, 1e-5f);
  EXPECT_NEAR(g[1].w, 0.5f - c0.adj, 1e-6f);
  auto zp = pes::NumberLayout(5, 1.0f, true);  // zero padding draws "05"
  ASSERT_EQ(zp.size(), 2u);
  EXPECT_EQ(zp[0].digit, 0);
}

TEST(NumberLayout, OutOfRangeDrawsNothing) {
  EXPECT_TRUE(pes::NumberLayout(0, 1.0f, false).empty());
  EXPECT_TRUE(pes::NumberLayout(1000, 1.0f, false).empty());
  EXPECT_EQ(pes::NumberLayout(123, 1.0f, false).size(), 3u);
}

// Flat names: cursor starts at 256 + N/2, glyphs placed right to left, each
// centred, width w*scale/2, height 62*0.98 at y 64.
TEST(NameLayout, FlatNameIsCentredRightToLeft) {
  const std::vector<float> widths = {72, 60};  // "AB" in glyph-table px
  auto q = pes::NameLayout(widths, 0);
  ASSERT_EQ(q.size(), 2u);
  // N = (72 + 60) / 2 = 66; the last glyph is placed first, at the right.
  EXPECT_NEAR(q[1].cx, 256 + 33 - 15, 1e-4f);
  EXPECT_NEAR(q[0].cx, 256 + 33 - 30 - 18, 1e-4f);
  EXPECT_NEAR(q[0].w, 36, 1e-4f);
  EXPECT_NEAR(q[0].cy, 64, 1e-4f);
  EXPECT_NEAR(q[0].h, 62 * 0.98f, 1e-4f);
  EXPECT_FLOAT_EQ(q[0].angle, 0.0f);
}

TEST(NameLayout, LongNamesScaleToFit) {
  const std::vector<float> widths(20, 72);  // N = 720 > 512
  auto q = pes::NameLayout(widths, 0);
  EXPECT_NEAR(q[0].w, 36 * 512.0f / 720.0f, 1e-3f);
}

// The 78-entry glyph table lives in the exe (DAT_142a96ce0).
TEST(GlyphTableInstall, ReadsTheGamesNameGlyphs) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto pe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(pe) << err;
  auto glyphs = pes::ReadNameGlyphs(*pe, &err);
  ASSERT_TRUE(glyphs) << err;
  EXPECT_EQ(glyphs->size(), 78u);
  const auto* a = pes::FindGlyph(*glyphs, 'A');
  ASSERT_TRUE(a);
  EXPECT_EQ(a->page, 0);
  EXPECT_EQ(a->x, 8);
  EXPECT_EQ(a->y, 2);
  EXPECT_EQ(a->w, 72);
  EXPECT_EQ(a->h, 124);  // all glyphs are 124 px tall
  const auto* w = pes::FindGlyph(*glyphs, 'W');
  ASSERT_TRUE(w);
  EXPECT_EQ(w->w, 96);
}

// info_name_font.bin maps a print name to drawable codes when the kit has no
// _name_ex page: lower to upper, Æ to AE, ' dropped.
TEST(NameCodesInstall, FoldsThroughInfoNameFont) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto map = pes::LoadNameFontMap(vfs, &err);
  ASSERT_TRUE(map) << err;
  EXPECT_EQ(pes::NameCodes(*map, "o'neil"), (std::vector<uint32_t>{'O', 'N', 'E', 'I', 'L'}));
  EXPECT_EQ(pes::NameCodes(*map, "\xC3\x86on"), (std::vector<uint32_t>{'A', 'E', 'O', 'N'}));  // Æon
}

// The finished marking texture holds the glyph page rotated 180 degrees: the
// quad is emitted mirrored in x and the canvas write flips y (FUN_1419e1df0
// 6.5), so the garment shader's positive name scales read it right way up.
// A blit without both flips rotates the glyphs; laying the band out right to
// left instead of left to right renders the name backwards (RELLUM).
TEST(NameBlit, GlyphIsTurned180InTheCanvas) {
  const pes::NameGlyph r{0x52, 0, 256, 130, 64, 124};  // 'R', page 0
  auto quads = pes::NameLayout({64.0f}, 0);            // one glyph: N = 32, no scale
  ASSERT_EQ(quads.size(), 1u);
  const auto b = pes::NameBlitOf(quads[0], r, 1600, 256, 0.64f, 512.0f, 128.0f);
  EXPECT_TRUE(b.mirrorU);
  EXPECT_TRUE(b.mirrorV);
  // centre = 256 units either way (symmetric for one glyph): 256 * 2 canvas px.
  EXPECT_NEAR(b.cx, 512.0f, 1e-3f);
  EXPECT_NEAR(b.cy, 128.0f, 1e-3f);
  EXPECT_NEAR(b.w, 64.0f, 1e-3f);   // gw = 64/2 name units * 2
  EXPECT_NEAR(b.h, 62.0f * 0.98f * 2.0f, 1e-3f);
  // The source window is the whole glyph cell of the page.
  EXPECT_NEAR(b.u0, 256.0f / 1024.0f, 1e-6f);
  EXPECT_NEAR(b.v0, 130.0f / 256.0f, 1e-6f);
  EXPECT_NEAR(b.uw, 64.0f / 1024.0f, 1e-6f);
  EXPECT_NEAR(b.vh, 124.0f / 256.0f, 1e-6f);
}

// An asymmetric two-glyph name pins both the order and the orientation. The
// quad loop places the first code point at the right (FUN_1419e1df0 6.3) and the
// canvas mirrors the band, so the first code point ends up at the canvas LEFT -
// the orientation the shirt back samples into reading order. Reversed: RELLUM.
constexpr float NAME_CENTRE_REF = 256.0f;  // name space centre, FUN_1419e1df0 6.2

TEST(NameBlit, TwoGlyphsKeepOrderAndTurn) {
  const pes::NameGlyph m{0x4D, 0, 850, 2, 84, 124};   // 'M' (wide)
  const pes::NameGlyph u{0x55, 0, 484, 130, 64, 124}; // 'U' (narrow)
  const auto widths = std::vector<float>{84.0f, 64.0f};
  auto quads = pes::NameLayout(widths, 0);
  ASSERT_EQ(quads.size(), 2u);
  // N = (84 + 64)/2 = 74; the loop runs right to left, so the last code point is
  // placed first at the right and the first code point ends up at the left.
  EXPECT_LT(quads[0].cx, quads[1].cx);
  EXPECT_NEAR(quads[0].w, 42.0f, 1e-4f);  // gw = 84/2
  EXPECT_NEAR(quads[1].w, 32.0f, 1e-4f);  // gw = 64/2
  const auto bm = pes::NameBlitOf(quads[0], m, 1600, 256, 0.64f, 512.0f, 128.0f);
  const auto bu = pes::NameBlitOf(quads[1], u, 1600, 256, 0.64f, 512.0f, 128.0f);
  // The canvas mirrors the band, so 'M' is right of 'U' on the canvas: the shirt
  // back samples the name region from the other side and reads "MU".
  EXPECT_GT(bm.cx, bu.cx);
  EXPECT_NEAR(bm.cx - bm.w / 2 - (bu.cx + bu.w / 2), 0.0f, 1e-3f);  // no gap
  EXPECT_NEAR((bm.cx + bm.w / 2 + bu.cx - bu.w / 2) / 2, NAME_CENTRE_REF * 2.0f, 1e-3f);  // centred
  EXPECT_TRUE(bm.mirrorU);
  EXPECT_TRUE(bm.mirrorV);
  EXPECT_TRUE(bu.mirrorU);
  EXPECT_TRUE(bu.mirrorV);
}

// Page 1 glyphs live on the 1024x512 page: v is divided by 512, not 256.
TEST(NameBlit, PageOneGlyphsUseTheHalfHeightPage) {
  const pes::NameGlyph ue{0xC39C, 1, 8, 2, 72, 124};  // 'Ü', page 1
  auto quads = pes::NameLayout({72.0f}, 0);
  const auto b = pes::NameBlitOf(quads[0], ue, 1600, 256, 0.64f, 512.0f, 128.0f);
  EXPECT_NEAR(b.v0, 2.0f / 512.0f, 1e-6f);
  EXPECT_NEAR(b.vh, 124.0f / 512.0f, 1e-6f);
  EXPECT_NEAR(b.u0, 8.0f / 1024.0f, 1e-6f);
}

// The canvas write flips y (FUN_1419e1df0 6.5): a name-space y of 64 maps to
// canvas row 128 of 256, and a glyph above the row lands higher in the texture.
TEST(NameBlit, CanvasFlipsY) {
  auto quads = pes::NameLayout({64.0f}, 0);
  const pes::NameGlyph k{0x4B, 0, 710, 2, 64, 124};  // 'K'
  const auto b = pes::NameBlitOf(quads[0], k, 1600, 256, 0.64f, 512.0f, 128.0f);
  EXPECT_NEAR(b.cy, 128.0f, 1e-3f);  // (128 - 64) * 2
  // A curved name put above the row (y > 64) must come out BELOW canvas row 128.
  auto curved = pes::NameLayout(std::vector<float>(6, 84.0f), 1);
  ASSERT_EQ(curved.size(), 6u);
  // The arc rises towards its ends: the ends sit above the row in name space and
  // below row 128 on the canvas; the middle sits the other way round.
  const auto first = pes::NameBlitOf(curved[0], k, 1600, 256, 0.64f, 512.0f, 128.0f);
  const auto last = pes::NameBlitOf(curved[5], k, 1600, 256, 0.64f, 512.0f, 128.0f);
  const auto middle = pes::NameBlitOf(curved[2], k, 1600, 256, 0.64f, 512.0f, 128.0f);
  EXPECT_GT(curved[0].cy, 64.0f);
  EXPECT_GT(curved[5].cy, 64.0f);
  EXPECT_LT(first.cy, 128.0f);
  EXPECT_LT(last.cy, 128.0f);
  EXPECT_LT(curved[2].cy, 64.0f);
  EXPECT_GT(middle.cy, 128.0f);
  // The mirrored canvas keeps the arc rising towards both ends.
  EXPECT_LT(first.cy, middle.cy);
  EXPECT_LT(last.cy, middle.cy);
}

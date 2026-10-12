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

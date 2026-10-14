// PES's Animetable (docs/re/pes-re-animselect.md 2.3): synthetic bytes check
// the cell layout, the empty-cell rule, the exe's one patched cell and the
// column picker; one install test reads the user's table and its row labels.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

#include "pes/animetable.hpp"

namespace {

pes::AnimetableLayout Layout() {
  pes::AnimetableLayout l;
  l.rows = 3;
  l.cols = 18;
  l.motionMask = 0x1fff;
  l.mirrorShift = 13;
  l.patchRow = 1;
  l.patchCol = 2;
  l.patchMotion = 77;
  l.columnStep_deg = 22.5f;
  l.columnOrigin_deg = 180.0f;
  l.zeroLeft_deg = -0.001f;
  l.zeroRight_deg = 0.001f;
  l.noRow = 3;
  return l;
}

std::vector<uint8_t> Bytes(const std::vector<uint32_t>& cells) {
  std::vector<uint8_t> b(cells.size() * 4);
  std::memcpy(b.data(), cells.data(), b.size());
  return b;
}

TEST(PesAnimetable, CellsCarryMotionPlusOneAndTheMirrorBit) {
  std::vector<uint32_t> cells(3 * 18, 0);
  cells[0 * 18 + 4] = (41 + 1) | (1u << 13);
  cells[2 * 18 + 17] = 9 + 1;
  auto t = pes::ParseAnimetable(Bytes(cells), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->Cell(0, 4).motionNo, 41);
  EXPECT_TRUE(t->Cell(0, 4).mirror);
  EXPECT_EQ(t->Cell(2, 17).motionNo, 9);
  EXPECT_FALSE(t->Cell(2, 17).mirror);
  EXPECT_EQ(t->Cell(1, 0).motionNo, -1);  // empty
  EXPECT_EQ(t->Cell(9, 0).motionNo, -1);  // out of range
  // FUN_1406abb30's one hard-coded cell, mirror forced off.
  EXPECT_EQ(t->Cell(1, 2).motionNo, 77);
  EXPECT_FALSE(t->Cell(1, 2).mirror);
}

TEST(PesAnimetable, RejectsATableOfTheWrongSize) {
  std::string err;
  EXPECT_FALSE(pes::ParseAnimetable(std::vector<uint8_t>(10), Layout(), &err).has_value());
  EXPECT_FALSE(err.empty());
}

TEST(PesAnimetable, ColumnIsTheNearestPopulatedAngleBucket) {
  std::vector<uint32_t> cells(3 * 18, 0);
  for (int c : {0, 4, 8, 9, 13}) cells[0 * 18 + c] = 1;
  auto t = pes::ParseAnimetable(Bytes(cells), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  // Centres: c<9 -> c*22.5 - 180, else (c-1)*22.5 - 180; 0 deg split at 8 / 9.
  EXPECT_EQ(t->Column(0, -170.0f), 0);
  EXPECT_EQ(t->Column(0, -95.0f), 4);    // -90
  EXPECT_EQ(t->Column(0, -1.0f), 8);     // -0
  EXPECT_EQ(t->Column(0, 1.0f), 9);      // +0
  EXPECT_EQ(t->Column(0, 120.0f), 13);   // +90 is the nearest populated
  // An empty row falls back to the middle column; the "no row" id tests all.
  EXPECT_EQ(t->Column(2, 45.0f), 9);
  EXPECT_EQ(t->Column(3, 45.0f), 11);
}

TEST(PesAnimetableInstall, TheUsersTableAndLabels) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto t = pes::LoadAnimetable(*exe, vfs, &err);
  ASSERT_TRUE(t.has_value()) << err;
  EXPECT_EQ(t->Layout().cols, 18);
  EXPECT_GT(t->Layout().rows, 1000);
  EXPECT_EQ(int(t->Labels().size()), t->Layout().rows);
  int populated = 0;
  for (int r = 0; r < t->Layout().rows; ++r)
    for (int c = 0; c < t->Layout().cols; ++c) populated += t->Cell(r, c).motionNo >= 0;
  EXPECT_GT(populated, t->Layout().rows);
  // Labels are situation names, every one non-empty.
  for (const auto& l : t->Labels()) EXPECT_FALSE(l.empty());
  EXPECT_GE(t->Row("riseup_faceup_0_3"), 0);
}

}  // namespace

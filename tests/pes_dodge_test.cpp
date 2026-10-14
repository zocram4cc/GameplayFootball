// PES 2021's dodge selectors (onthepitch/pesdodge.hpp): synthetic tables check
// each layout the exe references; one install test reads the user's exe, the
// Animetable and the Mbinfo bins and checks the rows resolve to the dodge
// clip families.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

#include "onthepitch/pesdodge.hpp"
#include "pes/mbinfo.hpp"

namespace {

pes::AnimetableLayout Layout() {
  pes::AnimetableLayout l;
  l.rows = 900;
  l.cols = 18;
  l.motionMask = 0x1fff;
  l.mirrorShift = 13;
  l.columnStep_deg = 22.5f;
  l.columnOrigin_deg = 180.0f;
  l.zeroLeft_deg = -0.001f;
  l.zeroRight_deg = 0.001f;
  l.noRow = 900;
  return l;
}

// A synthetic exe is not available (the constants are read from the real
// executable), so the selector tests build the constants by hand and only the
// install test exercises LoadDodgeConstants.
pes::DodgeConstants Constants() {
  pes::DodgeConstants k;
  // The install's kind -> row table (0x14344efb0).
  const int rows[8][3] = {{753, 754, 755}, {757, 757, 757}, {758, 758, 758},
                          {763, 763, 764}, {765, 765, 766}, {760, 760, 762},
                          {771, 771, 771}, {769, 767, 767}};
  for (int i = 0; i < 8; ++i)
    for (int s = 0; s < 3; ++s) k.kindRow[i][s] = rows[i][s];
  // The install's ball grid (0x1425e6650).
  const int grid[4][3] = {{757, 757, 757}, {775, 779, 779}, {776, 780, 780}, {777, 781, 781}};
  for (int r = 0; r < 4; ++r)
    for (int s = 0; s < 3; ++s) k.ballGrid[r][s] = grid[r][s];
  k.ballRow4A = 763, k.ballRow4B = 763, k.ballRow4C = 764;
  // The install's band table (0x14264866c).
  const float bands[12] = {0.335f, 0.56f, 0.785f, 1.01f, 1.235f, 1.46f,
                           1.685f, 1.91f, 2.135f, 2.36f, 2.585f, 3.08f};
  for (int i = 0; i < 12; ++i) k.ballHeightBands[i] = bands[i];
  k.netRowDefault = 0x310;
  k.netRowFast = 0x30e;
  for (int i = 0; i < 9; ++i) k.kindColumn[i] = (i < 3 ? 9 : i < 6 ? 5 : i == 6 ? 7 : i == 7 ? 8 : 11);
  return k;
}

// A table with the dodge rows populated at their real columns.
std::vector<uint8_t> Table() {
  std::vector<uint32_t> cells(900 * 18, 0);
  auto put = [&](int row, int col, int motion) { cells[row * 18 + col] = uint32_t(motion + 1); };
  for (int s : {0, 1, 2, 3, 4, 5, 6, 7}) {
    put(753 + s, 8, 18 + s);  // avoidjump family rows, col 8
    put(753 + s, 9, 18 + s);  // and col 9
  }
  put(763, 8, 24);
  put(763, 9, 24);
  put(0x310, 9, 2068);  // goalnet_stop_slow
  put(0x30e, 9, 2067);  // goalnet_stop_rapid
  put(0x2f5, 9, 19);    // avoidjump_3_3
  put(0x2f1, 9, 18);    // avoidjump_2_2_low
  std::vector<uint8_t> b(cells.size() * 4);
  std::memcpy(b.data(), cells.data(), b.size());
  return b;
}

TEST(PesDodge, BallSelectorReadsTheForcedRowAtColumn9) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  std::vector<int> forced = {757, 757, 757};
  auto c = pes::BallSelector(*t, Constants(), 0, forced);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->motionNo, 19);  // avoidjump_3_3
  EXPECT_EQ(c->col, 9);
  EXPECT_EQ(c->row, 757);
}

TEST(PesDodge, BallSelectorRefusesAnEmptyRow) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  std::vector<int> forced = {757, 757, 757};
  // Speed class 2 maps to a row that is not populated in the synthetic table.
  forced[2] = 500;
  EXPECT_FALSE(pes::BallSelector(*t, Constants(), 2, forced).has_value());
}

TEST(PesDodge, NetSelectorPicksTheSlowAndFastRows) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  auto slow = pes::NetSelector(*t, Constants(), false);
  ASSERT_TRUE(slow.has_value());
  EXPECT_EQ(slow->motionNo, 2068);  // goalnet_stop_slow
  auto fast = pes::NetSelector(*t, Constants(), true);
  ASSERT_TRUE(fast.has_value());
  EXPECT_EQ(fast->motionNo, 2067);  // goalnet_stop_rapid
}

TEST(PesDodge, KindColumnFollowsTheJumpTable) {
  const pes::DodgeConstants k = Constants();
  EXPECT_EQ(pes::KindColumn(k, 0), 9);
  EXPECT_EQ(pes::KindColumn(k, 1), 9);
  EXPECT_EQ(pes::KindColumn(k, 2), 9);
  EXPECT_EQ(pes::KindColumn(k, 3), 5);
  EXPECT_EQ(pes::KindColumn(k, 4), 5);
  EXPECT_EQ(pes::KindColumn(k, 5), 5);
  EXPECT_EQ(pes::KindColumn(k, 6), 7);
  EXPECT_EQ(pes::KindColumn(k, 7), 8);
  EXPECT_EQ(pes::KindColumn(k, 8), 11);  // the default
}

TEST(PesDodge, SpeedClassFollowsFUN_140736d30) {
  EXPECT_EQ(pes::SpeedClass(5.0f, 0.0f), 0);   // below the first bound
  EXPECT_EQ(pes::SpeedClass(15.0f, 0.0f), 1);  // between
  EXPECT_EQ(pes::SpeedClass(25.0f, 0.0f), 2);  // above the second
  EXPECT_EQ(pes::SpeedClass(25.0f, 90.0f), 0);  // heading off by more than 45°
}

TEST(PesDodge, BallBandFollowsFUN_140a6d380) {
  const pes::DodgeConstants k = Constants();
  EXPECT_EQ(pes::BallBand(k, 0.1f, 1.0f), 0);
  EXPECT_EQ(pes::BallBand(k, 0.5f, 1.0f), 1);
  EXPECT_EQ(pes::BallBand(k, 1.0f, 1.0f), 3);
  EXPECT_EQ(pes::BallBand(k, 3.0f, 1.0f), 11);  // above the last threshold
  EXPECT_EQ(pes::BallBand(k, 0.4f, 0.8f), 1);   // scaled by the body
}

TEST(PesDodge, KindRowStepsDownUntilTheColumnIsPopulated) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  // Kind 1 is row 757 at every speed class; the column-9 fit passes without a
  // ball owner.
  EXPECT_EQ(pes::KindRow(*t, pes::Mbinfo{}, Constants(), 1, 25.0f, false, 0.0f), 757);
  EXPECT_EQ(pes::KindRow(*t, pes::Mbinfo{}, Constants(), 1, 15.0f, false, 0.0f), 757);
  EXPECT_EQ(pes::KindRow(*t, pes::Mbinfo{}, Constants(), 1, 5.0f, false, 0.0f), 757);
  // Kind 8 is out of range.
  EXPECT_EQ(pes::KindRow(*t, pes::Mbinfo{}, Constants(), 8, 15.0f, false, 0.0f), -1);
}

TEST(PesDodge, KindRowFallsThroughAnUnpopulatedClass) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  // A kind whose fast class is empty steps down to the slow class.
  std::vector<uint32_t> cells(900 * 18, 0);
  cells[763 * 18 + 8] = 24 + 1;  // only the slow row of kind 3 populated
  cells[763 * 18 + 9] = 24 + 1;
  std::vector<uint8_t> b(cells.size() * 4);
  std::memcpy(b.data(), cells.data(), b.size());
  auto t2 = pes::ParseAnimetable(b, Layout(), nullptr);
  ASSERT_TRUE(t2.has_value());
  // kind 3 rows: 763 (slow), 763, 764 (fast). Fast is empty -> steps to 763.
  EXPECT_EQ(pes::KindRow(*t2, pes::Mbinfo{}, Constants(), 3, 25.0f, false, 0.0f), 763);
}

TEST(PesDodgeInstall, TheUsersTablesAndRows) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto k = pes::LoadDodgeConstants(*exe, &err);
  ASSERT_TRUE(k.has_value()) << err;
  auto t = pes::LoadAnimetable(*exe, vfs, &err);
  ASSERT_TRUE(t.has_value()) << err;
  auto mb = pes::LoadMbinfo(vfs, &err);
  ASSERT_TRUE(mb.has_value()) << err;

  // The kind -> row table resolves every kind to a row whose column 9 holds a
  // clip of the avoid family (the human dodges).
  int resolved = 0;
  for (int kind = 0; kind < 8; ++kind) {
    const int row = pes::KindRow(*t, *mb, *k, kind, 5.0f, false, 0.0f);
    ASSERT_GE(row, 0) << "kind " << kind;
    EXPECT_TRUE(t->Populated(row, pes::KindColumn(*k, kind)) ||
                t->Populated(row, 9))
        << "kind " << kind << " row " << row;
    ++resolved;
  }
  EXPECT_EQ(resolved, 8);

  // The net selector resolves to the goal-net family on both rows.
  auto slow = pes::NetSelector(*t, *k, false);
  ASSERT_TRUE(slow.has_value());
  EXPECT_EQ(slow->row, 0x310);
  auto fast = pes::NetSelector(*t, *k, true);
  ASSERT_TRUE(fast.has_value());
  EXPECT_EQ(fast->row, 0x30e);

  // The ball selector resolves the jump row.
  std::vector<int> forced = {757, 757, 757};
  auto ball = pes::BallSelector(*t, *k, 0, forced);
  ASSERT_TRUE(ball.has_value());
  EXPECT_EQ(ball->row, 757);
}

}  // namespace

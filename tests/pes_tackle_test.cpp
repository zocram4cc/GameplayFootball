// PES 2021's standing-tackle sub-kind (onthepitch/pestackle.hpp): the
// readiness mask, the first-ready-kind-wins pick over the per-kind group
// tables, and the row -> clip resolution, as a pure layer over
// pes::Animetable. One install test reads the user's exe and the Animetable
// and checks every kind's group resolves to the tacklefoot family.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

#include "onthepitch/pestackle.hpp"
#include "pes/anim_names.hpp"

namespace {

// The install's layout: 3045 rows x 18 columns, the dodge test's own. The
// tackle rows live up at 0x96b..0x9a4, so the synthetic table has to be the
// install's width, not the dodge test's trimmed 900.
pes::AnimetableLayout Layout() {
  pes::AnimetableLayout l;
  l.rows = 3045;
  l.cols = 18;
  l.motionMask = 0x1fff;
  l.mirrorShift = 13;
  l.columnStep_deg = 22.5f;
  l.columnOrigin_deg = 180.0f;
  l.zeroLeft_deg = -0.001f;
  l.zeroRight_deg = 0.001f;
  l.noRow = 3045;
  return l;
}

// The install's real tackle rows, populated at the columns the dumper found.
std::vector<uint8_t> Table() {
  std::vector<uint32_t> cells(3045 * 18, 0);
  auto put = [&](int row, int col, int motion, bool mirror = false) {
    cells[size_t(row) * 18 + col] = uint32_t((motion + 1) | (mirror ? 1u << 13 : 0));
  };
  // tacklefoot_0_0_near_block: c5/c8/c9/c12 (row 0x96b)
  put(0x96b, 5, 0xcd8);
  put(0x96b, 8, 0xcd5);
  put(0x96b, 9, 0xcd5);
  put(0x96b, 12, 0xcd8, true);
  // tacklefoot_0_0_mid (0x96f), its two dodges (0x970/0x971), mid_block (0x972)
  put(0x96f, 5, 0xccf);
  put(0x96f, 8, 0xccb);
  put(0x96f, 9, 0xccb);
  put(0x970, 6, 0xcce);
  put(0x970, 11, 0xccd);
  put(0x971, 6, 0xccd, true);
  put(0x971, 11, 0xcce, true);
  put(0x972, 5, 0xcd0);
  // tacklefoot_far_* (0x974..0x976) and tacklefoot_mid_3_0_000 (0x97b)
  put(0x974, 5, 0xcc7);
  put(0x974, 8, 0xcc5);
  put(0x974, 9, 0xcc5);
  put(0x975, 5, 0xcc8);
  put(0x976, 8, 0xcc6);
  put(0x976, 9, 0xcc6);
  put(0x97b, 5, 0x105d);
  put(0x97b, 8, 0xcd1);
  put(0x97b, 9, 0xcd1);
  // tacklefoot_near_3_0_000 (0x977), its mid_090 (0x97e) and far (0x97f)
  put(0x977, 5, 0xcdc);
  put(0x977, 8, 0xcd9);
  put(0x977, 9, 0xcd9);
  put(0x97e, 8, 0xcd2);
  put(0x97e, 9, 0xcd2);
  put(0x97f, 8, 0xcc9);
  put(0x97f, 9, 0xcc9);
  // tacklefoot_near_0_0_000_intercept (0x96e)
  put(0x96e, 8, 0xcd6);
  put(0x96e, 9, 0xcd6);
  // tacklefoot_near_3_0_000_intercept (0x979), 3_3 (0x97a)
  put(0x979, 8, 0xcdb);
  put(0x979, 9, 0xcdb);
  put(0x97a, 8, 0xcdd);
  put(0x97a, 9, 0xcdd);
  std::vector<uint8_t> b(cells.size() * 4);
  std::memcpy(b.data(), cells.data(), b.size());
  return b;
}

// A group table shaped like the install's: one slot carries one entry per
// speed class, and FUN_14078b550 walks them in slot order.
pes::TackleConstants Constants() {
  pes::TackleConstants k;
  // Kind 1's slot 0 (0x1425e9ce0): the single kick row the install carries.
  k.groups[0][0] = {{0x98d, 0}};
  // Kind 1's slot 1 (0x1425e9d20): the tacklehold rows.
  k.groups[0][1] = {{0x998, 0}, {0x98e, 0}, {0x990, 0}, {0x993, 0}};
  // Kind 4's slots (0x1425e9bb0 / 0x1425e9bb8): one entry each.
  k.groups[3][0] = {{0x96e, 0}};
  k.groups[3][1] = {{0x979, 0}};
  // Kind 5's slot 0 (0x1425e99e0), with the bits the install carries.
  k.groups[4][0] = {{0x96c, 0}, {0x96b, 0}, {0x972, 1}, {0x975, 2}};
  // Kind 7's standing table (param_2 = 0): 0x1425e9aa0.
  k.group7[0][0] = {{0x970, 0}, {0x96a, 0}, {0x96f, 1}, {0x974, 2}};
  // Kind 7's in-contact table (param_2 = 1): 0x1425e9a28.
  k.group7[1][0] = {{0x96a, 0}, {0x96f, 1}, {0x974, 2}};
  return k;
}

// Every kind's solver answered "yes", so the first live entry decides.
std::array<bool, 8> AllSolved() { return {true, true, true, true, true, true, true, true}; }

TEST(PesTackle, ReadinessMaskBitsAreTheKinds) {
  const pes::TackleConstants k = Constants();
  // Bit k is kind k: the pick reads `(mask >> kind) & 1`.
  pes::TackleReadiness r;
  r.touching = true;
  r.opposedBody_m = 1.0f;  // well inside 5 m
  const uint32_t mask = pes::ReadinessMask(r, k);
  EXPECT_EQ(mask & 0x2, 0u);   // kind 1 needs may-play + cooldown
  EXPECT_EQ(mask & 0x4, 0x4u); // kind 2: touching and opposed
  EXPECT_EQ(mask & 0x8, 0u);   // kind 3 needs the second-man flag
  EXPECT_EQ(mask & 0x60, 0x60u); // kinds 5-6, the paired touching gate
  EXPECT_EQ(mask & 0x80, 0u);  // kind 7 is the neither-flag fallback

  r.secondMan = true;
  r.touching = false;
  r.opposedBody_m = -1.0f;
  r.mayPlay = true;
  r.cooldownClear = true;
  const uint32_t m2 = pes::ReadinessMask(r, k);
  EXPECT_EQ(m2 & 0x4, 0u);
  EXPECT_EQ(m2 & 0x60, 0u);
  EXPECT_EQ(m2 & 0x8, 0x8u);   // kind 3 now
  EXPECT_EQ(m2 & 0x2, 0x2u);   // and kind 1
  EXPECT_EQ(m2 & 0x80, 0u);    // kind 7 needs neither flag, and the second man is in
}

TEST(PesTackle, ReadinessNeedsMayPlayAndCooldownForKind1) {
  const pes::TackleConstants k = Constants();
  pes::TackleReadiness r;
  r.cooldownClear = true;
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x2, 0u);  // nothing may play
  r.mayPlay = true;
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x2, 0x2u);
  r.cooldownClear = false;
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x2, 0u);
}

TEST(PesTackle, ReadinessKind4NeedsThePairWindowAndAllThreeAngles) {
  const pes::TackleConstants k = Constants();
  pes::TackleReadiness r;
  r.pairDistSq_m = 4.0f;  // clears 1.44 m^2
  r.headingToBall_deg = 90.0f;
  r.carrierToPoint_deg = 10.0f;
  r.carrierToStep_deg = 10.0f;
  r.delayRollOk = true;
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x10, 0x10u);
  r.pairDistSq_m = 1.0f;  // inside the near bound
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x10, 0u);
  r.pairDistSq_m = 4.0f;
  r.headingToBall_deg = 136.0f;  // past the 135-deg bound
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x10, 0u);
  r.headingToBall_deg = 90.0f;
  r.carrierToPoint_deg = 40.0f;  // past the 36-deg bound
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x10, 0u);
  r.carrierToPoint_deg = 10.0f;
  r.carrierToStep_deg = 40.0f;  // the second 36-deg test
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x10, 0u);
  r.carrierToStep_deg = 10.0f;
  r.delayRollOk = false;
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x10, 0u);
}

TEST(PesTackle, ReadinessKind4LiftedBallTakesTheTighterBound) {
  const pes::TackleConstants k = Constants();
  pes::TackleReadiness r;
  r.headingToBall_deg = 90.0f;
  r.carrierToPoint_deg = 10.0f;
  r.carrierToStep_deg = 10.0f;
  r.delayRollOk = true;
  r.pairDistSq_m = 4.0f;  // past 1.44, so the ball on the ground clears it
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x10, 0x10u);
  r.pairDistSq_m = 1.0f;  // inside 1.44
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x10, 0u);
  // A lifted ball tightens the bound to 0.09.
  r.ballLifted = true;
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x10, 0x10u);
  r.pairDistSq_m = 0.05f;  // inside 0.09
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x10, 0u);
}

TEST(PesTackle, ReadinessKind3NeedsNoOtherTackle) {
  const pes::TackleConstants k = Constants();
  pes::TackleReadiness r;
  r.secondMan = true;
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x8, 0x8u);
  r.otherTackleCommitted = true;
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x8, 0u);
}

TEST(PesTackle, NotPlayableSetsBit8) {
  const pes::TackleConstants k = Constants();
  pes::TackleReadiness r;  // everything default: may play false
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x100, 0x100u);
  r.mayPlay = true;
  EXPECT_EQ(pes::ReadinessMask(r, k) & 0x100, 0u);
}

TEST(PesTackle, PickTakesTheFirstReadyKindWhoseGroupEntryIsLive) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  const pes::TackleConstants k = Constants();
  const std::array<bool, 8> solved = AllSolved();
  // Kind 1 ready, its slot-0 entry's bit 0 set in slot 0 of the clip mask.
  std::array<uint32_t, 8> mask = {};
  mask[0] = 0x1;
  EXPECT_EQ(pes::PickSubKind(k, 0x2, mask, *t, solved), pes::TackleSubKind::k1);
  // Kind 4 (0x10) is next up, and its slot-0 entry is also at bit 0.
  EXPECT_EQ(pes::PickSubKind(k, 0x10, mask, *t, solved), pes::TackleSubKind::k4);
  // Both ready: kind 1 wins, the exe's loop is lowest-kind-first.
  EXPECT_EQ(pes::PickSubKind(k, 0x2 | 0x10, mask, *t, solved), pes::TackleSubKind::k1);
  // No bit set anywhere in the clip mask: no entry is live.
  mask[0] = 0;
  EXPECT_EQ(pes::PickSubKind(k, 0x10, mask, *t, solved), pes::TackleSubKind::k7);
}

TEST(PesTackle, PickWalksTheKindsInOrderAndStopsAtTheFirstLiveOne) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  const pes::TackleConstants k = Constants();
  const std::array<bool, 8> solved = AllSolved();
  std::array<uint32_t, 8> mask = {};
  // Kinds 1, 3, 5 and 7 all ready; 1 and 3 have entries whose bits are clear,
  // 5 has one set. The pick must land on 5, not on 7.
  mask[0] = 0x2;  // kind 5's slot-0 table has an entry at bit 1 (0x972)
  const uint32_t ready = 0x2 | 0x8 | 0x20 | 0x80;
  EXPECT_EQ(pes::PickSubKind(k, ready, mask, *t, solved), pes::TackleSubKind::k5);
}

TEST(PesTackle, PickFallsBackTo7WhenNothingIsReady) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  const pes::TackleConstants k = Constants();
  std::array<uint32_t, 8> mask = {};
  mask[0] = 0xff;  // every bit, so only the readiness can decide
  EXPECT_EQ(pes::PickSubKind(k, 0u, mask, *t, AllSolved()), pes::TackleSubKind::k7);
  // Bit 8 alone (the not-in-play early-out): no kind 1..7 is ready.
  EXPECT_EQ(pes::PickSubKind(k, 0x100, mask, *t, AllSolved()), pes::TackleSubKind::k7);
}

TEST(PesTackle, PickHonoursTheSolverAnswerPerKind) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  const pes::TackleConstants k = Constants();
  std::array<uint32_t, 8> mask = {};
  mask[0] = 0xff;
  std::array<bool, 8> solved = AllSolved();
  // Kind 1's row does not solve, and kind 4 is not ready: nothing lands.
  solved[0] = false;
  EXPECT_EQ(pes::PickSubKind(k, 0x2, mask, *t, solved), pes::TackleSubKind::k7);
  // Kind 4 ready and its row solves: it takes it.
  solved[3] = true;
  EXPECT_EQ(pes::PickSubKind(k, 0x10, mask, *t, solved), pes::TackleSubKind::k4);
}

TEST(PesTackle, ResolveClipReadsTheAnglesColumnOfTheRow) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  const pes::TackleConstants k = Constants();
  // At 0 deg the install's own split (FUN_1406de990) prefers the left of the
  // pair, column 8, when both 8 and 9 carry the same clip.
  auto c = pes::ResolveClip(*t, k, 0x96b, 0.0f);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->row, 0x96b);
  EXPECT_EQ(c->col, 8);
  EXPECT_EQ(c->motionNo, 0xcd5);
  EXPECT_FALSE(c->mirror);
  // One degree off the split and the picker takes the right column 9.
  auto cR = pes::ResolveClip(*t, k, 0x96b, 1.0f);
  ASSERT_TRUE(cR.has_value());
  EXPECT_EQ(cR->col, 9);
  EXPECT_EQ(cR->motionNo, 0xcd5);
  // Row 0x96f (the mid family) at 0 deg.
  auto m = pes::ResolveClip(*t, k, 0x96f, 0.0f);
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->motionNo, 0xccb);
  // A row the table leaves empty gives no clip.
  EXPECT_FALSE(pes::ResolveClip(*t, k, 0x99, 0.0f).has_value());
  // Out of range.
  EXPECT_FALSE(pes::ResolveClip(*t, k, -1, 0.0f).has_value());
  EXPECT_FALSE(pes::ResolveClip(*t, k, 100000, 0.0f).has_value());
}

TEST(PesTackle, ResolveClipCarriesTheMirrorBit) {
  auto t = pes::ParseAnimetable(Table(), Layout(), nullptr);
  ASSERT_TRUE(t.has_value());
  const pes::TackleConstants k = Constants();
  // Row 0x96b's column 5 (centre -67.5) is tacklefoot_near_0_0_f067 plain, and
  // its column 12 (centre +67.5) the same clip mirrored.
  auto plain = pes::ResolveClip(*t, k, 0x96b, -67.5f);
  ASSERT_TRUE(plain.has_value());
  EXPECT_EQ(plain->motionNo, 0xcd8);
  EXPECT_FALSE(plain->mirror);
  auto mirrored = pes::ResolveClip(*t, k, 0x96b, 67.5f);
  ASSERT_TRUE(mirrored.has_value());
  EXPECT_EQ(mirrored->motionNo, 0xcd8);
  EXPECT_TRUE(mirrored->mirror);
}

// The install: the per-kind groups are read out of the user's exe and every
// entry's row resolves to a tacklefoot clip in the user's Animetable.
TEST(PesTackleInstall, TheUsersGroupsResolveToTheTacklefootFamily) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto k = pes::LoadTackleConstants(*exe, &err);
  ASSERT_TRUE(k.has_value()) << err;
  auto t = pes::LoadAnimetable(*exe, vfs, &err);
  ASSERT_TRUE(t.has_value()) << err;
  auto names = pes::MotionNames(*exe, vfs, &err);
  ASSERT_TRUE(names.has_value()) << err;

  // Every kind has at least one non-empty group slot, and every entry of every
  // slot resolves to a clip of the tacklefoot family.
  int kinds = 0, entries = 0, resolved = 0;
  for (int kind = 1; kind <= 8; ++kind) {
    const auto& slots = kind == 7 ? k->group7[0] : k->groups[kind - 1];
    int kindEntries = 0;
    for (int slot = 0; slot < 3; ++slot) {
      for (const auto& entry : slots[slot]) {
        ++kindEntries, ++entries;
        ASSERT_LT(entry.row, t->Layout().rows) << "kind " << kind << " row " << entry.row;

        // The column picker's answer is the one the solver would read.
        const pes::AnimetableCell pick = t->Cell(entry.row, t->Column(entry.row, 0.0f));
        if (pick.motionNo < 0) continue;
        EXPECT_LT(pick.motionNo, int(names->size())) << "kind " << kind;
        const std::string& name = (*names)[pick.motionNo];
        // The tackle family proper (tacklefoot_*), or one of the neighbouring
        // families the same tables reach: a hold, a pull, a shoulder, or one of
        // the two off-family rows (the 0x980 far block and the four pass-cut
        // idle clips).
        const bool tackleFamily =
            name.rfind("tacklefoot_", 0) == 0 || name.rfind("tacklehold_", 0) == 0 ||
            name.rfind("tacklepulled_", 0) == 0 || name.rfind("tackleshoulder_", 0) == 0 ||
            name == "block090_3_0_y00_000_far_L" ||
            name.rfind("js_idle_0_0_000_y00_tacklefoot_passcut", 0) == 0;
        EXPECT_TRUE(tackleFamily) << "kind " << kind << " row " << entry.row << " -> " << name;
        ++resolved;
      }
    }
    // Every kind must carry at least one group: a kind that reads none would
    // be the shared-boundary ponytail swallowing a whole table (pestackle.cpp's
    // ReadGroup), not an install fact.
    EXPECT_GT(kindEntries, 0) << "kind " << kind << " carries no group entry";
    if (kindEntries > 0) ++kinds;
  }
  EXPECT_EQ(kinds, 8);
  EXPECT_GT(entries, 0);
  // Every entry resolves: the rows the tackle kinds name are the tacklefoot
  // family's, and the install's table holds them at every one.
  EXPECT_EQ(resolved, entries);
}

// The install fact that bounds the port: the readiness mask's own constants
// and the default sub-kind, so a different exe fails instead of guessing.
TEST(PesTackleInstall, TheUsersReadinessConstantsAndDefault) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  auto k = pes::LoadTackleConstants(*exe, &err);
  ASSERT_TRUE(k.has_value()) << err;
  EXPECT_FLOAT_EQ(k->opposedBody_m, 5.0f);
  EXPECT_FLOAT_EQ(k->pairNearSq_m, 1.44f);
  EXPECT_FLOAT_EQ(k->pairKeeperSq_m, 0.09f);
  EXPECT_NEAR(k->keeperGate_m, 0.434744f, 1e-6f);
  EXPECT_FLOAT_EQ(k->headingToBall_deg, 135.0f);
  EXPECT_FLOAT_EQ(k->carrierToPoint_deg, 36.0f);
  EXPECT_FLOAT_EQ(k->carrierToStep_deg, 36.0f);
  EXPECT_FLOAT_EQ(k->delayWindow_s, 6.0f);
  EXPECT_FLOAT_EQ(k->delayFramesPerSecond, 60.0f);
  EXPECT_FLOAT_EQ(k->delayStream2_s, 0.15f);
  EXPECT_FLOAT_EQ(k->delayStream1_s, 0.125f);
  EXPECT_FLOAT_EQ(k->delayFloor_s, 0.5f);
  EXPECT_EQ(k->defaultSubKind, 7);
}

}  // namespace

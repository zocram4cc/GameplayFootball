// PES 2021's CPU stamina substitutions (docs/re/pes-re-stamina-subs.md).
// Synthetic constants exercise the rules; one install test checks the loader
// against the user's PES2021.exe through relations, not pinned numbers.
#include <gtest/gtest.h>

#include <cstdlib>

#include "onthepitch/pessubs.hpp"

namespace {

pes::SubstitutionConstants Synthetic() {
  pes::SubstitutionConstants k{};
  k.mode1TwoThirdsFrom = 30;
  k.mode2TwoThirdsFrom = 30;
  k.mode2ThirdFrom = 12;
  k.mode3FullFrom = 22;
  k.mode3TwoThirdsFrom = 8;
  k.neverSubbed[0] = k.neverSubbed[1] = true;
  for (int c = 2; c < pes::kSubRoleClasses; ++c) {
    const bool attacker = c >= 6, central = c == 3 || c == 4;
    for (int m = 0; m < pes::kSubModes; ++m) k.threshold[c][m] = (attacker ? 22 : 18) + 10 * m;
    k.base[c] = attacker ? 0 : central ? 900 : 500;
  }
  k.tierGap = 1.0f;
  k.fitWide = 12.0f;
  k.fitNear = 6.0f;
  return k;
}

}  // namespace

TEST(PesSubs, CapFollowsTheModeScheduleInTheSecondHalfOnly) {
  const auto k = Synthetic();
  constexpr int kCap = 6;
  EXPECT_EQ(pes::StaminaSubCap(k, 3, true, false, 44, kCap), 0) << "never in the first half";
  EXPECT_EQ(pes::StaminaSubCap(k, 1, false, false, 0, kCap), kCap) << "a break gives the whole cap";
  EXPECT_EQ(pes::StaminaSubCap(k, 0, false, true, 44, kCap), 0) << "mode 0 is off";
  EXPECT_EQ(pes::StaminaSubCap(k, 1, false, true, 29, kCap), 0);
  EXPECT_EQ(pes::StaminaSubCap(k, 1, false, true, 30, kCap), 4);
  EXPECT_EQ(pes::StaminaSubCap(k, 2, false, true, 12, kCap), 2);
  EXPECT_EQ(pes::StaminaSubCap(k, 2, false, true, 44, kCap), 4) << "mode 2 never reaches the full cap";
  EXPECT_EQ(pes::StaminaSubCap(k, 3, false, true, 8, kCap), 4);
  EXPECT_EQ(pes::StaminaSubCap(k, 3, false, true, 22, kCap), kCap);
}

// Below the class threshold only, keeper and centre-backs never, and the
// ascending base + stamina order puts a tired attacker ahead of a more
// tired central midfielder.
TEST(PesSubs, TiredBelowTheirClassThresholdComeOffAttackersFirst) {
  const auto k = Synthetic();
  const std::vector<pes::StaminaSubCandidate> pitch = {
      {0, e_PlayerRole_GK, 1},  {1, e_PlayerRole_CB, 1},  {2, e_PlayerRole_CM, 5},
      {3, e_PlayerRole_CF, 20}, {4, e_PlayerRole_LB, 17}, {5, e_PlayerRole_RW, 23},
      {6, e_PlayerRole_DM, 19}};
  const std::vector<int> mode1 = pes::PickStaminaSubs(k, 1, pitch);
  ASSERT_EQ(mode1, (std::vector<int>{3, 4, 2})) << "RW 23 > 22, DM 19 > 18 stay on";
  const std::vector<int> mode3 = pes::PickStaminaSubs(k, 3, pitch);
  EXPECT_EQ(mode3, (std::vector<int>{3, 5, 4, 2, 6})) << "higher modes raise every threshold";
  EXPECT_TRUE(pes::PickStaminaSubs(k, 0, pitch).empty());
}

// Positional fit first: a natural forward beats a better-rated defender for a
// forward's place; within the same tiers a wide fit gap decides.
TEST(PesSubs, ReplacementIsPositionFirstThenRating) {
  const auto k = Synthetic();
  const std::vector<pes::BenchCandidate> bench = {
      {10, {e_PlayerRole_CB}, 90.0f, false},
      {11, {e_PlayerRole_CF}, 60.0f, false},
      {12, {e_PlayerRole_SS, e_PlayerRole_CF}, 80.0f, false},
  };
  EXPECT_EQ(pes::PickReplacement(k, e_PlayerRole_CF, pes::SubKind::Stamina, bench), 12)
      << "both forwards can play CF; the 20-point fit gap beats being listed there";
  const std::vector<pes::BenchCandidate> close = {
      {11, {e_PlayerRole_CF}, 70.0f, false},
      {12, {e_PlayerRole_SS, e_PlayerRole_CF}, 72.0f, false},
  };
  EXPECT_EQ(pes::PickReplacement(k, e_PlayerRole_CF, pes::SubKind::Stamina, close), 11)
      << "inside the near gap, being listed at the position wins";
  const std::vector<pes::BenchCandidate> keeperOnly = {{20, {e_PlayerRole_GK}, 90.0f, false}};
  EXPECT_EQ(pes::PickReplacement(k, e_PlayerRole_CF, pes::SubKind::Stamina, keeperOnly), -1)
      << "a stamina sub needs someone who can play there";
  EXPECT_EQ(pes::PickReplacement(k, e_PlayerRole_CF, pes::SubKind::Injury, keeperOnly), 20)
      << "an injury takes whoever is left";
}

TEST(PesSubsInstall, LoadsTheScheduleAndClassTableFromTheExe) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto k = pes::LoadSubstitutionConstants(*exe, &err);
  ASSERT_TRUE(k) << err;
  EXPECT_LT(k->mode2ThirdFrom, k->mode2TwoThirdsFrom);
  EXPECT_LT(k->mode3TwoThirdsFrom, k->mode3FullFrom);
  const int fullBack = pes::SubRoleClass(e_PlayerRole_LB), cm = pes::SubRoleClass(e_PlayerRole_CM),
            cf = pes::SubRoleClass(e_PlayerRole_CF);
  EXPECT_TRUE(k->neverSubbed[pes::SubRoleClass(e_PlayerRole_GK)]);
  EXPECT_TRUE(k->neverSubbed[pes::SubRoleClass(e_PlayerRole_CB)]);
  // Attackers sort first, central midfielders last.
  EXPECT_LT(k->base[cf], k->base[fullBack]);
  EXPECT_LT(k->base[fullBack], k->base[cm]);
  for (int c : {fullBack, cm, cf})
    for (int m = 1; m < pes::kSubModes; ++m)
      EXPECT_LT(k->threshold[c][m - 1], k->threshold[c][m]) << "higher modes sub sooner";
  EXPECT_GT(k->fitWide, k->fitNear);
}

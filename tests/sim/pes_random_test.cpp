// PES 2021's generator (docs/re/pes-re-rng.md 1-2). Synthetic constants
// exercise the arithmetic; one install test checks the loader against the
// user's own PES2021.exe through relations, not pinned numbers.
#include <gtest/gtest.h>

#include <cstdlib>

#include "onthepitch/pesrandom.hpp"

namespace {

pes::RngConstants Synthetic() {
  pes::RngConstants k{};
  k.lcgMul = 0x10001u;
  k.lcgSub = 0x3u;
  k.modulus = {10, 200, 4096, 4096};
  k.scale = 1.0f / 2147483648.0f;
  k.weakMul = 5;
  k.weakInc = 3;
  k.weakMask = 0xFF;
  k.defaultSeeds = {11, 12, 13, 14};
  return k;
}

}  // namespace

TEST(PesRandom, StepSubtractsTheProductAndClearsBit31) {
  const auto k = Synthetic();
  pes::Random r(k);
  r.SetSeed(0, 7);
  const uint32_t expected = (3u - 7u * 0x10001u) & 0x7FFFFFFFu;
  EXPECT_EQ(r.Step(0), expected);
  EXPECT_EQ(r.State(0), expected);
  EXPECT_LT(r.Step(0), 0x80000000u);
}

TEST(PesRandom, RangeMapsTheNewStateThroughTheSlotModulus) {
  const auto k = Synthetic();
  pes::Random a(k), b(k);
  for (int slot = 0; slot < 3; ++slot) {
    for (int i = 0; i < 100; ++i) {
      const int v = a.Range(slot);
      EXPECT_GE(v, 0);
      EXPECT_LT(v, k.modulus[slot]);
      const uint32_t s = b.Step(slot);
      EXPECT_EQ(v, static_cast<int>(static_cast<float>(s) * k.scale * k.modulus[slot]));
    }
  }
}

TEST(PesRandom, WeakSlotIsItsOwnFifteenBitStyleLcg) {
  const auto k = Synthetic();
  pes::Random r(k);
  r.AdvanceWeak();
  EXPECT_EQ(r.State(3), (5u * 14u + 3u) & 0xFFu);
  const uint32_t before = r.State(0);
  r.Range(3);
  EXPECT_EQ(r.State(0), before) << "the weak slot never touches the 31-bit slots";
}

TEST(PesRandom, Range0DrawsSlotOneAndClampsToTheCount) {
  const auto k = Synthetic();
  pes::Random r(k);
  EXPECT_EQ(r.Range0(0), 0);
  const uint32_t slot0 = r.State(0);
  for (int i = 0; i < 100; ++i) {
    const int v = r.Range0(7);
    EXPECT_GE(v, 0);
    EXPECT_LT(v, 7);
  }
  EXPECT_EQ(r.State(0), slot0) << "Range0 draws slot 1 only";
}

TEST(PesRandom, KickoffSeedsThreeConsecutiveValuesAndKeepsTheWeakDefault) {
  const auto k = Synthetic();
  pes::Random r(k);
  pes::SeedAtKickoff(r, 2783);
  EXPECT_EQ(r.State(0), 2783u);
  EXPECT_EQ(r.State(1), 2784u);
  EXPECT_EQ(r.State(2), 2785u);
  EXPECT_EQ(r.State(3), k.defaultSeeds[3]);
}

TEST(PesRandomInstall, LoadsTheStockGeneratorFromTheExe) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto k = pes::LoadRngConstants(*exe, &err);
  ASSERT_TRUE(k) << err;
  EXPECT_EQ(k->lcgMul & 1u, 1u) << "a full-period LCG multiplier is odd";
  EXPECT_EQ(k->weakMul & 1u, 1u);
  EXPECT_FLOAT_EQ(k->scale * 2147483648.0f, 1.0f) << "the scale is 2^-31";
  for (int i = 1; i < pes::kRandomSlots; ++i) {
    EXPECT_GT(k->modulus[i], 0);
    EXPECT_EQ(k->defaultSeeds[i], k->defaultSeeds[0] + static_cast<uint32_t>(i))
        << "the constructor seeds are consecutive";
  }
  EXPECT_GT(k->weakMask, 0u);
}

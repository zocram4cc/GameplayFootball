// PES 2021's CPU strategy decider (docs/re/pes-re-match-management.md 1).
// Synthetic constants exercise the rules; one install test checks the loader
// against the user's PES2021.exe through relations, not pinned numbers.
#include <gtest/gtest.h>

#include <cstdlib>

#include "onthepitch/pesrandom.hpp"
#include "onthepitch/pesstrategy.hpp"

namespace {

pes::RngConstants SyntheticRng() {
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

pes::StrategyConstants Synthetic() {
  pes::StrategyConstants k{};
  k.strengthMargin = 1.0f;
  k.weightsStronger[0] = 5, k.weightsStronger[1] = 0, k.weightsStronger[2] = 0;
  k.weightsWeaker[0] = 0, k.weightsWeaker[1] = 5, k.weightsWeaker[2] = 0;
  k.weightsEven[0] = 0, k.weightsEven[1] = 0, k.weightsEven[2] = 5;
  k.attackHoldFrom = 11;
  k.defendHoldFrom = 21;
  k.holdTo = 40;
  k.holdNever = 99;
  k.secondHalfFrom = 30;
  k.secondHalfSpan = 5;
  k.rngSlot = 2;
  return k;
}

pes::StrategyInputs At(pes::StrategyPeriod period, int minute, int own, int opp) {
  pes::StrategyInputs in;
  in.period = period;
  in.minute = minute;
  in.ownGoals = own;
  in.oppGoals = opp;
  return in;
}

using P = pes::StrategyPeriod;

}  // namespace

// Minute 0 draws the half's pick from the strength row and the two limits.
// PES compares the draw with `<=` (FUN_14051e970: `if (w3 < r)` falls to the
// other picks), so a draw of 0 is always attacking whatever the row: the rows
// below rule out every other pick except that one.
TEST(PesStrategy, KickoffPickFollowsTheStrengthRowAndDrawsBothLimits) {
  const auto k = Synthetic();
  pes::Random rng(SyntheticRng());
  pes::StrategyState s;
  pes::StrategyInputs in = At(P::FirstHalf, 0, 0, 0);
  in.strengthDiff = 2.0f;  // stronger: every draw lands on attacking
  pes::StrategyStep(k, in, rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyAttacking);
  EXPECT_GE(s.holdUntilMinute, k.attackHoldFrom);
  EXPECT_LT(s.holdUntilMinute, k.holdTo);
  EXPECT_GE(s.secondHalfMinute, k.secondHalfFrom);
  EXPECT_LT(s.secondHalfMinute, k.secondHalfFrom + k.secondHalfSpan);

  constexpr int kDraws = 50;
  bool sawDefensive = false, sawBalanced = false, sawZeroWeightAttack = false;
  for (int i = 0; i < kDraws; ++i) {
    in.strengthDiff = -2.0f;  // weaker: never balanced
    pes::StrategyStep(k, in, rng, &s);
    EXPECT_NE(s.strategy, pes::kStrategyBalanced);
    if (s.strategy == pes::kStrategyDefensive) {
      sawDefensive = true;
      EXPECT_GE(s.holdUntilMinute, k.defendHoldFrom);
    }
    in.strengthDiff = 0.5f;  // inside the margin: never defensive
    pes::StrategyStep(k, in, rng, &s);
    EXPECT_NE(s.strategy, pes::kStrategyDefensive);
    if (s.strategy == pes::kStrategyBalanced) {
      sawBalanced = true;
      EXPECT_EQ(s.holdUntilMinute, 0) << "balanced sets no hold";
    }
    sawZeroWeightAttack |= s.strategy == pes::kStrategyAttacking;
  }
  EXPECT_TRUE(sawDefensive);
  EXPECT_TRUE(sawBalanced);
  EXPECT_TRUE(sawZeroWeightAttack) << "a draw of 0 attacks even on a zero attacking weight";
}

// Inside the hold the pick gives up only against the run of the score.
TEST(PesStrategy, FirstHalfHoldYieldsToTheScoreThenExpires) {
  const auto k = Synthetic();
  pes::Random rng(SyntheticRng());
  pes::StrategyState s;
  s.strategy = pes::kStrategyAttacking;
  s.holdUntilMinute = 30;
  pes::StrategyStep(k, At(P::FirstHalf, 10, 0, 1), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyAttacking) << "attacking while behind holds";
  pes::StrategyStep(k, At(P::FirstHalf, 10, 1, 0), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyBalanced) << "attacking while ahead stops";

  s.strategy = pes::kStrategyDefensive;
  pes::StrategyStep(k, At(P::FirstHalf, 10, 1, 0), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyDefensive) << "defending a lead holds";
  pes::StrategyStep(k, At(P::FirstHalf, 10, 0, 1), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyBalanced) << "defending while behind stops";

  s.strategy = pes::kStrategyAttacking;
  pes::StrategyStep(k, At(P::FirstHalf, 30, 0, 1), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyBalanced) << "at the hold's minute the pick ends";
}

// The second half steps toward attacking when behind and defending when ahead
// before R[6], and jumps straight there from R[6] on.
TEST(PesStrategy, SecondHalfStepsBeforeTheSwitchMinuteAndJumpsAfterIt) {
  const auto k = Synthetic();
  pes::Random rng(SyntheticRng());
  pes::StrategyState s;
  s.secondHalfMinute = 70;

  s.strategy = pes::kStrategyBalanced;
  pes::StrategyStep(k, At(P::SecondHalf, 50, 0, 1), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyBalanced) << "one down is not enough before R[6]";
  pes::StrategyStep(k, At(P::SecondHalf, 50, 0, 2), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyAttacking);
  pes::StrategyStep(k, At(P::SecondHalf, 50, 1, 0), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyDefensive) << "attacking drops back once ahead";
  pes::StrategyStep(k, At(P::SecondHalf, 50, 1, 1), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyAttacking) << "defending a draw is not allowed";

  pes::StrategyStep(k, At(P::SecondHalf, 70, 0, 0), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyAttacking) << "level at R[6]: go for it";
  pes::StrategyStep(k, At(P::SecondHalf, 70, 1, 0), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyDefensive) << "ahead at R[6]: see it out";
}

TEST(PesStrategy, ExtraTimeAttacksUnlessAhead) {
  const auto k = Synthetic();
  pes::Random rng(SyntheticRng());
  pes::StrategyState s;
  pes::StrategyStep(k, At(P::ExtraTime, 95, 1, 1), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyAttacking);
  pes::StrategyStep(k, At(P::ExtraTime, 95, 2, 1), rng, &s);
  EXPECT_EQ(s.strategy, pes::kStrategyDefensive);
}

TEST(PesStrategy, AttackLevelIsTheStrategyExceptBalancedCoversTheRest) {
  for (int s : {0, 1, 3, 4}) EXPECT_EQ(pes::StrategyAttackLevel(s), s);
  EXPECT_EQ(pes::StrategyAttackLevel(2), 2);
  EXPECT_EQ(pes::StrategyAttackLevel(7), 2);
  EXPECT_EQ(pes::StrategyAttackLevel(-1), 2);
}

TEST(PesStrategyInstall, LoadsTheDrawLiteralsFromTheExe) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto k = pes::LoadStrategyConstants(*exe, &err);
  ASSERT_TRUE(k) << err;
  EXPECT_GT(k->strengthMargin, 0.0f);
  EXPECT_LT(k->attackHoldFrom, k->defendHoldFrom) << "attacking ends sooner";
  EXPECT_LT(k->defendHoldFrom, k->holdTo);
  EXPECT_GT(k->holdNever, k->holdTo);
  // The stronger side favours attacking, the weaker leans on defending.
  EXPECT_GT(k->weightsStronger[0], k->weightsStronger[1]);
  EXPECT_GE(k->weightsWeaker[1], k->weightsStronger[1]);
  EXPECT_GE(k->rngSlot, 0);
  EXPECT_LT(k->rngSlot, pes::kWeakSlot);
}

// PES 2021's stamina, fatigue and injury model,
// docs/re/pes-re-stamina-drain.md. The pure functions are tested against
// synthetic constants; one install test loads the user's own PES2021.exe and
// checks the report's own numbers against the constants it reads.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

#include "onthepitch/pesrandom.hpp"
#include "onthepitch/pesstamina.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace {

// PES's own shipped values (report 7, live-validated), so the tests exercise
// the rules under the numbers PES actually runs on.
pes::StaminaConstants Pes() {
  pes::StaminaConstants k{};
  k.abilityFloor = 5;
  k.abilityScale70_79 = 110;
  k.abilityScale80_89 = 120;
  k.abilityScale90_94 = 130;
  k.abilityScale95_99 = 140;
  k.abilityScaleDivisor = 100;
  k.abilityScaleUnit = 400;
  k.arrowTerriblePenalty = -8000.0f;
  k.arrowPoorPenalty = -4000.0f;
  k.arrowGreatBonus = 2000.0f;
  k.arrowTopBonus = 6000.0f;
  k.fitnessBonus = 8000.0f;
  k.tankFloor = 16000.0f;
  k.freshnessFloor = 10;
  k.drainScale_tick = 60.0f;
  k.drainPerSpeed[0] = 19;
  k.drainPerSpeed[1] = 17;
  k.drainPerSpeed[2] = 9;
  k.drainPerSpeed[3] = -5;
  k.drainPerSpeed[4] = -9;
  k.drainBreathFullBonus = 5;
  k.drainNoBallControlCost = 2;
  k.drainAiFlagCost = 3;
  k.drainEmptyFloor = 10;
  k.speedClassThresholdWalk_kmh = 0.05f;
  k.speedClassThresholdJog_kmh = 8.0f;
  k.speedClassThresholdRun_kmh = 13.5f;
  k.speedClassThresholdDash_kmh = 22.0f;
  k.gameSpeedIntDivisor = 54.0f;
  k.breathFrameRate = 48.0f;
  k.breathBaseTicks = 6.0f;
  k.breathDashCostTicks = 9.0f;
  k.breathBallHolderCost = 2.0f;
  k.breathDashSpeed_kmh = 22.0f;
  k.breathMinSpeed_kmh = 2.5f;
  k.breathKmhPerMs = 3.6f;
  k.breathCapScale = 1920;
  k.breathCapFloor_pct = 20;
  k.breathCapDivisor = 100;
  k.halfTimeBonus = 9000.0f;
  k.halfTimeT12Bonus = 3600.0f;
  k.halfTimeT36Bonus = 7200.0f;
  k.extraTimeBonus = 5400.0f;
  k.extraTimeT12Bonus = 1800.0f;
  k.a5FloorPct = 10.0f;
  k.a5FullPct = 70.0f;
  k.a5SpanPct = 60.0f;
  k.a5Base = 0.8f;
  k.a5Scale = 0.2f;
  k.breathPenaltyScale = 10.0f;
  k.breathPenaltyFullPct = 20.0f;
  k.breathPenaltyDivisor = -20.0f;
  k.injurySpeedScale_kmh = 25.0f;
  k.injurySpeedFloor = 0.7f;
  k.injurySpeedSpan = 0.3f;
  k.injuryBaseDown = 100.0f;
  k.injuryBaseUp = 50.0f;
  k.injuryAttackerSlide = 0.8f;
  k.injuryAttackerSlideFromRun = 0.5f;
  k.injurySlowFactor = 0.2f;
  k.injuryAngleDivisor = 180.0f;
  k.injuryHalf = 0.5f;
  k.injuryCap = 255.0f;
  k.injuryJackpotSev = 85.0f;
  k.injuryKnockAcc = 150.0f;
  k.injuryInjuredAcc = 200.0f;
  k.injuryJackpotSevCap = 200;
  k.injuryJackpotThr[0] = 5;
  k.injuryJackpotThr[1] = 2;
  k.injuryJackpotThr[2] = 0;
  k.injuryResistanceThr[0] = 30;
  k.injuryResistanceThr[1] = 50;
  k.injuryResistanceThr[2] = 70;
  k.injuryRngSlot = 2;
  k.injuryRngModulus = 100;
  return k;
}

// A 20-minute match: F = 10, the report's own live setting.
constexpr float F10 = 10.0f;

// The injury object's generator: the constructor defaults (0x2f3d0..0x2f3d3),
// never seeded, so the roll sequence restarts from them every match (report 9).
pes::RngConstants InjuryRng() {
  pes::RngConstants k;
  k.lcgMul = 0x2d22b2e3;   // RangeRaw 0x140a924ef
  k.lcgSub = 0x4513;       // mov r8d, 0x4513
  k.modulus = {100, 5000, 32768, 32768};
  k.scale = 4.656612873077393e-10f;  // 2^-31
  k.defaultSeeds = {193488, 193489, 193490, 193491};
  return k;
}

}  // namespace

// --- the tank (report 2) ---

TEST(PesStamina, TankScalesWithAbilityAndMatchLength) {
  const pes::StaminaConstants k = Pes();
  pes::StaminaState s;
  // Stamina 99 -> 99*140/100 = 138; arrow normal (a = 0); T != 0.
  pes::InitStamina(k, 99, 2, 1, F10, 100, &s);
  // 138 * 400 * 10 = 552000 (report 7's observed tank).
  EXPECT_EQ(s.max, 552000);
  EXPECT_EQ(s.cur, s.max);
  // The breath cap at 100%: 100 * 1920 / 100.
  EXPECT_EQ(s.breath, 1920);

  // Stamina 80 -> 96, so 96 * 4000 = 384000 (report 8's worked example).
  pes::InitStamina(k, 80, 2, 1, F10, 100, &s);
  EXPECT_EQ(s.max, 384000);

  // The tank floor: any ability below it, at a 10-minute match, is 160000.
  pes::InitStamina(k, 40, 0, 1, F10, 100, &s);
  EXPECT_EQ(s.max, 160000);
}

TEST(PesStamina, ConditionArrowAndFitnessShiftTheTank) {
  const pes::StaminaConstants k = Pes();
  pes::StaminaState normal, top, terrible, rested;
  pes::InitStamina(k, 80, 2, 1, F10, 100, &normal);
  pes::InitStamina(k, 80, 4, 1, F10, 100, &top);
  pes::InitStamina(k, 80, 0, 1, F10, 100, &terrible);
  pes::InitStamina(k, 80, 2, 0, F10, 100, &rested);
  EXPECT_EQ(top.max - normal.max, 60000);      // +6000 * F
  EXPECT_EQ(terrible.max - normal.max, -80000);  // -8000 * F
  EXPECT_EQ(rested.max - normal.max, 80000);   // T == 0 -> +8000 * F
}

TEST(PesStamina, KickOffValueIsTheFreshnessPercentage) {
  const pes::StaminaConstants k = Pes();
  pes::StaminaState s;
  pes::InitStamina(k, 80, 2, 1, F10, 97, &s);
  EXPECT_EQ(s.cur, s.max * 97 / 100);
  // The start-of-match freshness is floored at 10.
  pes::InitStamina(k, 80, 2, 1, F10, 4, &s);
  EXPECT_EQ(s.cur, s.max * 10 / 100);
  // The breath cap follows the starting percentage, with the 20% floor.
  EXPECT_EQ(s.breath, 20 * 1920 / 100);
}
// --- the per-tick drain (report 3.3, live-validated) ---

TEST(PesStamina, SpeedClasses) {
  const pes::StaminaConstants k = Pes();
  EXPECT_EQ(pes::SpeedClass(k, 0.0f), 0);
  EXPECT_EQ(pes::SpeedClass(k, 0.05f), 0);
  EXPECT_EQ(pes::SpeedClass(k, 0.06f), 1);
  EXPECT_EQ(pes::SpeedClass(k, 8.0f), 1);
  EXPECT_EQ(pes::SpeedClass(k, 8.1f), 2);
  EXPECT_EQ(pes::SpeedClass(k, 13.5f), 2);
  EXPECT_EQ(pes::SpeedClass(k, 13.6f), 3);
  EXPECT_EQ(pes::SpeedClass(k, 22.0f), 3);
  EXPECT_EQ(pes::SpeedClass(k, 22.1f), 4);
}

TEST(PesStamina, DrainPerSpeedClassMatchesTheLiveSeries) {
  const pes::StaminaConstants k = Pes();
  pes::StaminaState s;
  pes::InitStamina(k, 80, 2, 1, F10, 100, &s);
  // The breath is full at kick-off, so bp / 100 == 1.
  const int full = 100;
  ASSERT_EQ(s.breath * 100 / 1920, full);
  // Start a little below the tank: SetCurrent clamps upward, so a full tank
  // cannot show the standing gain.
  s.cur = s.max - 50;
  const int cur = s.cur;
  // Report 7's most frequent per-tick deltas, at gs 54, carried onto GF's
  // 10 ms tick (0.54 of a PES tick): the integer the drain computes is the
  // PES per-tick delta, scaled per GF tick and truncated toward zero.
  //   stand +8 -> +4, walk +6 -> +3, jog -2 -> -1, run -17 -> -9,
  //   dash -22 -> -11.
  EXPECT_EQ(pes::DrainTick(k, s, 0, full, false), cur + 4);
  EXPECT_EQ(pes::DrainTick(k, s, 1, full, false), cur + 3);
  EXPECT_EQ(pes::DrainTick(k, s, 2, full, false), cur - 1);
  EXPECT_EQ(pes::DrainTick(k, s, 3, full, false), cur - 9);
  EXPECT_EQ(pes::DrainTick(k, s, 4, full, false), cur - 11);
  // F20 = 1 (the side does control it): F68 drops out, the -2 stays.
  //   run -18 -> -9, dash -23 -> -12.
  EXPECT_EQ(pes::DrainTick(k, s, 3, full, true), cur - 9);
  EXPECT_EQ(pes::DrainTick(k, s, 4, full, true), cur - 12);
  // Breath not full (bp below 100), F68 = 1: run -23 -> -12, dash -27 -> -14.
  EXPECT_EQ(pes::DrainTick(k, s, 3, 19, false), cur - 12);
  EXPECT_EQ(pes::DrainTick(k, s, 4, 19, false), cur - 14);
}

TEST(PesStamina, DrainIsFrozenAtEmptyAndClampedAtTheTank) {
  const pes::StaminaConstants k = Pes();
  pes::StaminaState s;
  pes::InitStamina(k, 80, 2, 1, F10, 100, &s);
  s.cur = 12;  // just above the 10 floor
  s.breath = 0;
  // Standing refills, so it recovers; but the runner below drains past 10.
  s.cur = 12;
  const int next = pes::DrainTick(k, s, 4, 0, true);
  EXPECT_EQ(next, 0) << "cur < 10 is clamped to 0, not to a negative number";
  // Refining from an empty tank: the drain runs every tick, so an exhausted
  // player never goes below 0 either.
  s.cur = 0;
  EXPECT_EQ(pes::DrainTick(k, s, 4, 0, true), 0);
}

// --- the breath reserve (report 3.2) ---

TEST(PesStamina, BreathRefillsWhileStandingAndDrainsWhileDashing) {
  const pes::StaminaConstants k = Pes();
  pes::StaminaState s;
  pes::InitStamina(k, 80, 2, 1, F10, 100, &s);
  s.breath = 0;
  // Standing (below the 22 km/h dash test): +5 per PES tick = +2 per GF tick
  // (5 * 54 / 100, truncated toward zero), with the cap holding at 1920.
  pes::UpdateBreath(k, s, 3.0f, false, &s.breath);
  EXPECT_EQ(s.breath, 2);
  for (int i = 0; i < 2000; i++) pes::UpdateBreath(k, s, 3.0f, false, &s.breath);
  EXPECT_EQ(s.breath, 1920) << "the cap is max(pct, 20) * 1920 / 100";

  // Dashing, no ball: d = (int)(5 - 9*48/54) = -3, so -1 per GF tick.
  s.breath = 100;
  pes::UpdateBreath(k, s, 25.0f, false, &s.breath);
  EXPECT_EQ(s.breath, 99);
  // Dashing with the ball: d = (int)(-3 - 2*48/54) = -4, so -2 per GF tick.
  s.breath = 100;
  pes::UpdateBreath(k, s, 25.0f, true, &s.breath);
  EXPECT_EQ(s.breath, 98);
  // Walking with the ball: d = (int)(5 - 2*48/54) = 3, so +1 per GF tick -
  // the ball holder still refills, just more slowly.
  s.breath = 100;
  pes::UpdateBreath(k, s, 3.0f, true, &s.breath);
  EXPECT_EQ(s.breath, 101);
}

TEST(PesStamina, BreathCapFollowsTheTankPercentage) {
  const pes::StaminaConstants k = Pes();
  pes::StaminaState s;
  pes::InitStamina(k, 80, 2, 1, F10, 100, &s);
  s.cur = s.max / 2;  // 50%
  s.breath = 5000;
  pes::UpdateBreath(k, s, 3.0f, false, &s.breath);
  EXPECT_EQ(s.breath, 50 * 1920 / 100);
  // The floor: below 20% the cap still allows a fifth of the reserve.
  s.cur = s.max / 20;  // 5%
  s.breath = 5000;
  pes::UpdateBreath(k, s, 3.0f, false, &s.breath);
  EXPECT_EQ(s.breath, 20 * 1920 / 100);
}

// --- the break bonus (report 3.4) ---

TEST(PesStamina, HalfTimeBonusAndFitnessTiers) {
  const pes::StaminaConstants k = Pes();
  pes::StaminaState s;
  pes::InitStamina(k, 80, 2, 1, F10, 100, &s);
  s.cur = 100000;
  // Report 7's observed jumps at F = 10: 90k, 126k, 162k. The base lands
  // first (cast down from a float), then the tier lands on top of it.
  EXPECT_EQ(pes::BreakBonus(k, s, 2, 0, F10), 100000 + 90000);
  EXPECT_EQ(pes::BreakBonus(k, s, 2, 1, F10), 100000 + 126000);
  EXPECT_EQ(pes::BreakBonus(k, s, 2, 2, F10), 100000 + 126000);
  EXPECT_EQ(pes::BreakBonus(k, s, 2, 3, F10), 100000 + 162000);
  EXPECT_EQ(pes::BreakBonus(k, s, 2, 6, F10), 100000 + 162000);
  EXPECT_EQ(pes::BreakBonus(k, s, 2, 7, F10), 100000 + 90000);
  EXPECT_EQ(pes::BreakBonus(k, s, 4, 0, F10), 100000 + 54000);
  EXPECT_EQ(pes::BreakBonus(k, s, 4, 2, F10), 100000 + 54000 + 18000);
  // Clamped to the tank, never past it.
  s.cur = s.max - 1;
  EXPECT_EQ(pes::BreakBonus(k, s, 2, 3, F10), s.max);
}

// --- the fatigue effect on stats (report 4) ---

TEST(PesStamina, FatigueFactorCurve) {
  const pes::StaminaConstants k = Pes();
  pes::StaminaState s;
  pes::InitStamina(k, 80, 2, 1, F10, 100, &s);
  s.cur = s.max;  // 100%
  EXPECT_FLOAT_EQ(pes::FatigueFactor(k, s), 1.0f);
  s.cur = s.max * 70 / 100;  // 70%
  EXPECT_FLOAT_EQ(pes::FatigueFactor(k, s), 1.0f);
  s.cur = s.max * 40 / 100;  // 40% -> t = (40-10)/60 = 0.5
  EXPECT_FLOAT_EQ(pes::FatigueFactor(k, s), 0.9f);
  s.cur = s.max * 10 / 100;
  EXPECT_FLOAT_EQ(pes::FatigueFactor(k, s), 0.8f);
  s.cur = s.max * 5 / 100;
  EXPECT_FLOAT_EQ(pes::FatigueFactor(k, s), 0.8f) << "the floor holds below 10%";

  // The 1/300 grid: an integer percentage moves the factor in steps of 0.2/60.
  s.cur = s.max * 41 / 100;  // t = 31/60
  EXPECT_NEAR(pes::FatigueFactor(k, s), 0.8f + 0.2f * (31.0f / 60.0f), 1e-6);
}

// --- the breath penalty (report 4) ---

TEST(PesStamina, BreathPenaltySteps) {
  const pes::StaminaConstants k = Pes();
  pes::StaminaState s;
  pes::InitStamina(k, 80, 2, 1, F10, 100, &s);
  s.breath = 1920;  // full
  EXPECT_EQ(pes::BreathPenalty(k, s), 0);
  s.breath = 20;
  EXPECT_EQ(pes::BreathPenalty(k, s), 0);
  s.breath = 10;  // (10-20)/-20 * 10 = 5
  EXPECT_EQ(pes::BreathPenalty(k, s), 5);
  s.breath = 0;
  EXPECT_EQ(pes::BreathPenalty(k, s), 10);
}

// --- the injury roll (report 6) ---

TEST(PesStamina, ContactSeverityGrowsWithSpeed) {
  const pes::StaminaConstants k = Pes();
  // PES's injury object's generator: the constructor defaults, never seeded.
  pes::Random rng(InjuryRng());
  const int standing = pes::ContactSeverity(k, 0, false, 0.0f, 0.0f, false, false, 0.0f, &rng);
  const int running = pes::ContactSeverity(k, 0, false, 25.0f, 25.0f, false, false, 180.0f, &rng);
  EXPECT_GT(running, standing);
  EXPECT_LE(standing, 255);
  EXPECT_LE(running, 255);
  // The victim down doubles the base.
  const int down = pes::ContactSeverity(k, 0, true, 25.0f, 25.0f, false, false, 180.0f, &rng);
  EXPECT_GT(down, running);
}

TEST(PesStamina, InjuryStatesComeFromAccumulation) {
  const pes::StaminaConstants k = Pes();
  pes::Random rng(InjuryRng());
  pes::StaminaState s;
  pes::InitStamina(k, 80, 2, 1, F10, 100, &s);
  // Severity of one contact, PES's own arithmetic: 100 base (the victim is
  // down) x 1.0 x 1.0 (both speed ramps full at 25 km/h) x 0.5 (the attacker
  // is neither sliding) x (0.2 + 0.5*1) (a head-on approach) = 35, and the
  // resistance roll (A = 70) halves it when the draw falls below 70. A never
  // jacks the pot, so injury is purely cumulative.
  //
  // The default seeds make the first five draws 28, 67, 64, 2, 68 - all below
  // 70 - so the first five standing contacts each land 17.
  for (int i = 0; i < 5; i++) {
    const int sev =
        pes::RecordContact(k, &s, 2, true, 25.0f, 25.0f, false, false, 180.0f, &rng);
    EXPECT_EQ(sev, 17);
  }
  EXPECT_EQ(s.accumulation, 85);
  EXPECT_EQ(s.injuryLevel, 0);

  // The sixth draw is 97, above 70, so the roll fails: the standing contact
  // lands its full 35 and the man is marked as a knock (150 in PES's levels).
  // The seventh (91) fails too, so the second full contact takes him past 150.
  for (int i = 0; i < 2; i++) {
    const int sev =
        pes::RecordContact(k, &s, 2, true, 25.0f, 25.0f, false, false, 180.0f, &rng);
    EXPECT_EQ(sev, 35);
  }
  EXPECT_GE(s.accumulation, 150);
  EXPECT_EQ(s.injuryLevel, 2);
  // A slide from 11..12 is 0.8 against 0.5 for a standing attacker: 100 x 1 x 1
  // x 0.8 x 0.75 = 60, halved to 30 when the roll passes. Those contacts push
  // the accumulation past 200, which is the injured level.
  for (int i = 0; i < 5; i++) {
    pes::RecordContact(k, &s, 2, true, 25.0f, 25.0f, true, false, 180.0f, &rng);
  }
  EXPECT_GE(s.accumulation, 200);
  EXPECT_EQ(s.injuryLevel, 3);
}

// --- the real install ---

TEST(PesStaminaInstall, LoadsTheInstallConstants) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set: the constants come from the install";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  auto constants = pes::LoadStaminaConstants(*exe, &err);
  ASSERT_TRUE(constants.has_value()) << err;
  const pes::StaminaConstants& k = *constants;

  // The report's live-validated values, read from the install itself.
  EXPECT_NEAR(k.tankFloor, 16000.0f, 1e-3);
  EXPECT_NEAR(k.arrowTopBonus, 6000.0f, 1e-3);
  EXPECT_NEAR(k.arrowTerriblePenalty, -8000.0f, 1e-3);
  EXPECT_EQ(k.abilityScale95_99, 140);
  EXPECT_EQ(k.abilityScale90_94, 130);
  EXPECT_EQ(k.abilityScale80_89, 120);
  EXPECT_EQ(k.abilityScale70_79, 110);
  EXPECT_EQ(k.abilityScaleUnit, 400);
  EXPECT_NEAR(k.gameSpeedIntDivisor, 54.0f, 1e-4);
  EXPECT_NEAR(k.drainScale_tick, 60.0f, 1e-4);
  EXPECT_NEAR(k.speedClassThresholdWalk_kmh, 0.05f, 1e-5);
  EXPECT_NEAR(k.speedClassThresholdJog_kmh, 8.0f, 1e-5);
  EXPECT_NEAR(k.speedClassThresholdRun_kmh, 13.5f, 1e-5);
  EXPECT_NEAR(k.speedClassThresholdDash_kmh, 22.0f, 1e-5);
  EXPECT_NEAR(k.halfTimeBonus, 9000.0f, 1e-3);
  EXPECT_NEAR(k.halfTimeT12Bonus, 3600.0f, 1e-3);
  EXPECT_NEAR(k.halfTimeT36Bonus, 7200.0f, 1e-3);
  EXPECT_NEAR(k.a5Base, 0.8f, 1e-6);
  EXPECT_NEAR(k.a5Scale, 0.2f, 1e-6);
  EXPECT_NEAR(k.breathFrameRate, 48.0f, 1e-4);
  EXPECT_NEAR(k.breathBaseTicks, 6.0f, 1e-4);
  EXPECT_NEAR(k.breathDashCostTicks, 9.0f, 1e-4);
  EXPECT_EQ(k.breathCapScale, 1920);
  EXPECT_EQ(k.injuryResistanceThr[0], 30);
  EXPECT_EQ(k.injuryResistanceThr[1], 50);
  EXPECT_EQ(k.injuryResistanceThr[2], 70);
  EXPECT_NEAR(k.injuryBaseDown, 100.0f, 1e-3);
  EXPECT_NEAR(k.injuryKnockAcc, 150.0f, 1e-3);
  EXPECT_NEAR(k.injuryInjuredAcc, 200.0f, 1e-3);

  // The report's worked example, from the install's own numbers.
  pes::StaminaState s;
  pes::InitStamina(k, 99, 2, 1, F10, 100, &s);
  EXPECT_EQ(s.max, 552000);
  pes::InitStamina(k, 80, 2, 1, F10, 100, &s);
  EXPECT_EQ(s.max, 384000);
  EXPECT_EQ(s.breath, 1920);
  // A little below the tank: SetCurrent clamps upward, so a full tank cannot
  // show the standing gain.
  s.cur = s.max - 50;
  const int cur = s.cur;
  // The live-validated drain series (report 7), scaled to GF's tick:
  // breath full, F68 = 1 -> stand +4, jog -1, dash -11; breath not full,
  // F68 = 1, dash -14.
  EXPECT_EQ(pes::DrainTick(k, s, 0, 100, false), cur + 4);
  EXPECT_EQ(pes::DrainTick(k, s, 2, 100, false), cur - 1);
  EXPECT_EQ(pes::DrainTick(k, s, 4, 100, false), cur - 11);
  EXPECT_EQ(pes::DrainTick(k, s, 4, 19, false), cur - 14);
}

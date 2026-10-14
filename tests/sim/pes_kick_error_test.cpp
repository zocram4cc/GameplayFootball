// PES 2021's kick execution error (docs/re/pes-re-kick-scatter.md,
// pes-re-kick-accuracy.md). Synthetic constants pin the arithmetic; the install
// test checks the loader through relations, not transcribed values.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <string>

#include "onthepitch/peskickerror.hpp"
#include "onthepitch/pesrandom.hpp"
#include "pes/exe.hpp"

namespace {

pes::KickErrorConstants Synthetic() {
  pes::KickErrorConstants k{};
  k.qFloorStat = 40; k.qMidStat = 60; k.qTopStat = 95; k.qLowGain = 0.1f; k.qLowSpan = 20;
  k.qMidGain = 0.85f; k.qMidSpan = 35; k.qTopGain = 0.05f; k.qTopQuarter = 0.25f;
  k.qTopBase = 0.95f; k.skillBonus = 5;
  k.angleScale_deg = 22.5f; k.speedScale = 0.3f; k.ampGain = 4.25f; k.ampBase = 0.75f;
  k.half = 0.5f; k.aBase = 0.25f;
  k.minute_s = 60; k.km_m = 1000; k.rampPerQuality = 10; k.rampLo_kmh = 60;
  k.groundHi_kmh = 85; k.otherHi_kmh = 80; k.wQuality = 0.95f; k.wGround = 0.3f;
  k.wOther = 0.375f; k.pitchLo_deg = 20; k.pitchHi_deg = 30;
  k.powerLo = 0.2f; k.powerSpan = 0.8f; k.shootPowerWeight = 0.25f;
  k.rWeight = 0.4f; k.dBase = 0.8f; k.contactLo = -20; k.contactSpan = 40; k.contactHi = 20;
  k.nearOpp_m = 1; k.farOpp_m = 2; k.dWeight = 1.5f;
  k.normalDiv = std::sqrt(-2.0f * std::log(1.0f / 512.0f)); k.normalCentre = 50;
  k.normalTabScale = 1.0f / 4096.0f; k.normalNegTwo = -2; k.normalTabSteps = 512;
  k.normalDraw = 1000; k.fullTurn_deg = 360; k.quarterTurn_deg = 90;
  for (int i = 1; i < pes::kNormalTableSize; ++i)
    k.logTable[i] = -std::log(512.0f / static_cast<float>(i)) * 4096.0f;
  k.logTable[0] = k.logTable[1];
  return k;
}

pes::RngConstants SyntheticRng() {
  pes::RngConstants r{};
  r.scale = 1.0f / 2147483648.0f;
  r.lcgMul = 0x2d22b2e3;
  r.lcgSub = 0x4513;
  r.modulus = {100, 5000, 32768, 32768};
  r.defaultSeeds = {1, 2, 3, 4};
  return r;
}

TEST(PesKickError, QualityCurveFollowsTheThreeSegments) {
  const auto k = Synthetic();
  EXPECT_FLOAT_EQ(pes::KickQuality(k, 30), 0.0f);
  EXPECT_FLOAT_EQ(pes::KickQuality(k, 50), 0.05f);
  EXPECT_NEAR(pes::KickQuality(k, 77.5f), 0.525f, 1e-5);
  EXPECT_NEAR(pes::KickQuality(k, 99), 1.0f, 1e-5);
}

TEST(PesKickError, SoftMinLeansToTheWorstFactor) {
  EXPECT_FLOAT_EQ(pes::SoftMin4(1, 1, 1, 1), 1.0f);
  EXPECT_FLOAT_EQ(pes::SoftMin4(1, 0.5f, 1, 1), 0.5f);
  EXPECT_FLOAT_EQ(pes::SoftMin4(1, 0.5f, 0.5f, 1), 0.375f);
}

TEST(PesKickError, PerfectKickHasNoSpreadAndAPoorKickerAWideOne) {
  const auto k = Synthetic();
  const auto good = pes::BuildScatter(k, 1.0f, 1.0f, 1.0f);
  EXPECT_FLOAT_EQ(good.yaw.scale, 0.0f);
  EXPECT_FLOAT_EQ(good.yaw.amp, 0.75f);
  EXPECT_FLOAT_EQ(good.yaw.pSign, 0.5f);
  const auto poor = pes::BuildScatter(k, 0.0f, 1.0f, 1.0f);
  // A = 0.25, q4 = 0.25, amp = 0.75^2 * 4.25 + 0.75.
  EXPECT_NEAR(poor.yaw.amp, 0.5625f * 4.25f + 0.75f, 1e-5);
  // A hurried, pressed kick widens the scale.
  const auto pressed = pes::BuildScatter(k, 1.0f, 0.5f, 1.0f);
  EXPECT_NEAR(pressed.yaw.scale, 0.5f * 22.5f, 1e-4);
  EXPECT_NEAR(pressed.speed.scale, 0.5f * 0.3f, 1e-5);
}

TEST(PesKickError, NormalWithZeroAmpIsCentredAndWiderAmpSpreads) {
  const auto k = Synthetic();
  const auto rc = SyntheticRng();
  pes::Random rng(rc);
  rng.SetSeed(pes::kKickRngSlot, 12345);
  for (int i = 0; i < 50; ++i) EXPECT_EQ(pes::Normal(rng, k, 0.0f), 50);
  auto meanDev = [&](float amp) {
    float sum = 0;
    for (int i = 0; i < 2000; ++i) {
      const int n = pes::Normal(rng, k, amp);
      EXPECT_GE(n, 0);
      EXPECT_LE(n, 99);
      sum += std::fabs(n - 50.0f);
    }
    return sum / 2000;
  };
  EXPECT_LT(meanDev(0.75f), meanDev(5.0f));
}

TEST(PesKickError, PressureOnlyFromAnOpponentWithinReach) {
  const auto k = Synthetic();
  EXPECT_FLOAT_EQ(pes::PressureFactor(k, 70, 70, 3.0f), 1.0f);
  // Equal contact: D = 0.8 - 0.4*0.5 = 0.6, R = 1 - 0.4*1.5*0.6.
  EXPECT_NEAR(pes::PressureFactor(k, 70, 70, 0.5f), 1.0f - 0.36f, 1e-5);
  // The stronger kicker shrugs more of it off.
  EXPECT_GT(pes::PressureFactor(k, 90, 60, 0.5f), pes::PressureFactor(k, 60, 90, 0.5f));
}

TEST(PesKickError, LaunchPenaltyRampsWithSpeedAndPower) {
  const auto k = Synthetic();
  EXPECT_FLOAT_EQ(pes::PassLaunchPenalty(k, 0.5f, 10.0f, 0.0f, true), 1.0f);
  // 120 km/h is past every window: 1 - w, w = (1 - 0.95*0.5) * 0.3.
  EXPECT_NEAR(pes::PassLaunchPenalty(k, 0.5f, 120.0f / 3.6f, 0.0f, true),
              1.0f - 0.525f * 0.3f, 1e-5);
  // A steep ball ramps on elevation alone.
  EXPECT_LT(pes::PassLaunchPenalty(k, 0.5f, 10.0f, 30.0f, false), 1.0f);
  EXPECT_FLOAT_EQ(pes::ShotLaunchPenalty(k, 0.1f), 1.0f);
  EXPECT_NEAR(pes::ShotLaunchPenalty(k, 1.0f), 0.75f, 1e-6);
}

TEST(PesKickError, DrawWithZeroScaleLeavesTheKickAlone) {
  const auto k = Synthetic();
  const auto rc = SyntheticRng();
  pes::Random rng(rc);
  const auto e = pes::DrawKickError(k, rng, pes::BuildScatter(k, 1, 1, 1), false);
  EXPECT_FLOAT_EQ(e.yaw_deg, 0.0f);
  EXPECT_FLOAT_EQ(e.pitch_deg, 0.0f);
  EXPECT_FLOAT_EQ(e.speedFactor, 1.0f);
}

TEST(PesKickError, DrawStaysInsideTheScaleAndHalvesLowPitch) {
  const auto k = Synthetic();
  const auto rc = SyntheticRng();
  pes::Random rng(rc);
  rng.SetSeed(pes::kKickRngSlot, 777);
  const auto s = pes::BuildScatter(k, 0.3f, 0.6f, 0.8f);
  bool sawYaw = false;
  for (int i = 0; i < 500; ++i) {
    const auto e = pes::DrawKickError(k, rng, s, true);
    EXPECT_LE(std::fabs(e.yaw_deg), s.yaw.scale + 1e-4f);
    EXPECT_LE(e.pitch_deg, s.pitch.scale + 1e-4f);
    EXPECT_GE(e.pitch_deg, -0.5f * s.pitch.scale - 1e-4f);
    EXPECT_LE(std::fabs(e.speedFactor - 1.0f), s.speed.scale + 1e-4f);
    sawYaw |= std::fabs(e.yaw_deg) > 0.1f;
  }
  EXPECT_TRUE(sawYaw);
}

TEST(PesKickError, ApplyTurnsLiftsAndScales) {
  float v[3] = {10, 0, 0};
  pes::ApplyKickError({90.0f, 0.0f, 1.0f}, v);
  EXPECT_NEAR(v[0], 0.0f, 1e-4);
  EXPECT_NEAR(v[1], 10.0f, 1e-4);
  float w[3] = {10, 0, 0};
  pes::ApplyKickError({0.0f, 45.0f, 0.5f}, w);
  EXPECT_NEAR(w[0], 5.0f / std::sqrt(2.0f), 1e-4);
  EXPECT_NEAR(w[2], 5.0f / std::sqrt(2.0f), 1e-4);
}

TEST(PesKickErrorInstall, LoadsFromTheExe) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto k = pes::LoadKickErrorConstants(*exe, &err);
  ASSERT_TRUE(k) << err;
  // The curve is continuous at both joints.
  EXPECT_NEAR(k->qLowGain * (k->qMidStat - k->qFloorStat) / k->qLowSpan, k->qLowGain, 1e-6);
  EXPECT_NEAR(k->qLowGain + k->qMidGain * (k->qTopStat - k->qMidStat) / k->qMidSpan,
              k->qTopBase, 1e-5);
  // The table is -ln(512/i) in 2^-12 units, and the divisor is its far end.
  EXPECT_FLOAT_EQ(k->logTable[512], 0.0f);
  EXPECT_NEAR(k->logTable[256] * k->normalTabScale, -std::log(2.0f), 1e-3);
  EXPECT_NEAR(std::sqrt(k->normalNegTwo * k->logTable[1] * k->normalTabScale), k->normalDiv,
              1e-3);
  // A perfect kicker's amp is the floor, a poor one's sits inside Normal's range.
  EXPECT_GT(k->ampGain + k->ampBase, k->ampBase);
}

}  // namespace

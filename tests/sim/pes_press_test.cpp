// Pure tests of the presser picker (src/sim/onthepitch/pespress.hpp) against
// synthetic constants, plus one install relation test. The synthetic values
// are arbitrary on purpose: the tests check the arithmetic, not PES's numbers.

#include <gtest/gtest.h>

#include <cstdlib>

#include "onthepitch/pespress.hpp"

namespace {

pes::PressConstants Synthetic() {
  pes::PressConstants c{};
  c.rampStart_deg = 20.0f;
  c.rampEnd_deg = 90.0f;
  c.rampSpan_deg = 70.0f;
  c.radiusBase_m = 4.0f;
  c.halfTurn_deg = 180.0f;
  c.fullTurn_deg = 360.0f;
  c.radToDeg = 57.2957763671875f;
  c.notSlotPenalty = 1000.0f;
  c.notRolePenalty = 5000.0f;
  c.carrierRampStart_m = 10.0f;
  c.carrierRampEnd_m = 20.0f;
  c.carrierRampSpan_m = 10.0f;
  c.pass2Lo = 2.0f;
  c.pass2Hi = 5.0f;
  c.pass2Div = -3.0f;
  c.offsetBase_deg = 7.0f;
  c.offsetSpan_deg = 3.0f;
  c.slot1Lo_deg = 30.0f;
  c.slot1Hi_deg = 120.0f;
  c.lowBand_deg = 40.0f;
  c.highBand_deg = 100.0f;
  c.slot2Band_deg = 80.0f;
  c.slot3Lo_deg = -30.0f;
  c.slot3Hi_deg = -120.0f;
  return c;
}

constexpr float kEps = 1e-3f;

TEST(PressSort, StableAscendingByScore) {
  std::vector<pes::PressCandidate> b = {{3, 5.0f}, {7, 1.0f}, {9, 5.0f}, {2, 0.5f}};
  pes::SortPressBucket(b);
  ASSERT_EQ(b.size(), 4u);
  EXPECT_EQ(b[0].man, 2);
  EXPECT_EQ(b[1].man, 7);
  // Equal scores keep their input order.
  EXPECT_EQ(b[2].man, 3);
  EXPECT_EQ(b[3].man, 9);
}

TEST(PressBearing, CardinalDirectionsAtAtan2DxDz) {
  auto c = Synthetic();
  EXPECT_NEAR(pes::PressBearingDeg(c, 0, 0, 0, 5), 0.0f, kEps);     // +z
  EXPECT_NEAR(pes::PressBearingDeg(c, 0, 0, 5, 0), 90.0f, kEps);    // +x
  EXPECT_NEAR(pes::PressBearingDeg(c, 0, 0, 0, -5), 180.0f, kEps);  // -z
  EXPECT_NEAR(pes::PressBearingDeg(c, 0, 0, -5, 0), 270.0f, kEps);  // -x
}

TEST(PressBearing, CoincidentPointsGiveZero) {
  auto c = Synthetic();
  EXPECT_EQ(pes::PressBearingDeg(c, 3, 4, 3, 4), 0.0f);
}

TEST(PressWrap, IntoZeroToFullTurn) {
  auto c = Synthetic();
  EXPECT_NEAR(pes::PressWrapDeg(c, -10.0f), 350.0f, kEps);
  EXPECT_NEAR(pes::PressWrapDeg(c, 370.0f), 10.0f, kEps);
  EXPECT_NEAR(pes::PressWrapDeg(c, 360.0f), 0.0f, kEps);
}

TEST(PressSideAngle, FoldsAcrossTheSeamWithSign) {
  auto c = Synthetic();
  // 10 against 350 is +20 (the short way round); 350 against 10 is -20.
  EXPECT_NEAR(pes::PressSideAngleDeg(c, 10.0f, 350.0f), 20.0f, kEps);
  EXPECT_NEAR(pes::PressSideAngleDeg(c, 350.0f, 10.0f), -20.0f, kEps);
  EXPECT_NEAR(pes::PressSideAngleDeg(c, 90.0f, 0.0f), 90.0f, kEps);
}

TEST(PressCarrierRamp, ZeroInsideStartOneAtEnd) {
  auto c = Synthetic();
  EXPECT_EQ(pes::PressCarrierRamp(c, 5.0f), 0.0f);
  EXPECT_NEAR(pes::PressCarrierRamp(c, 15.0f), 0.5f, kEps);
  EXPECT_EQ(pes::PressCarrierRamp(c, 30.0f), 1.0f);
}

TEST(PressOffset, RampFromRootScore) {
  auto c = Synthetic();
  // At or below the low edge the offset is base plus full span.
  EXPECT_NEAR(pes::PressOffsetDeg(c, 1.0f), 7.0f + 3.0f, kEps);
  // At or above the high edge it is the base alone.
  EXPECT_NEAR(pes::PressOffsetDeg(c, 6.0f), 7.0f, kEps);
  // Between the edges it is linear: (root - hi) / div, here 0.5.
  EXPECT_NEAR(pes::PressOffsetDeg(c, 3.5f), 7.0f + 3.0f * 0.5f, kEps);
}

TEST(PressManScore, InConeAddsTheConeTermOutsideAddsTheRampStart) {
  auto c = Synthetic();
  EXPECT_NEAR(pes::PressManScore(c, 4.0f, 0.0f, pes::kPresserRole, 0), 4.0f + 16.0f, kEps);
  EXPECT_NEAR(pes::PressManScore(c, 100.0f, 0.0f, pes::kPresserRole, 0), 100.0f + 20.0f, kEps);
}

TEST(PressManScore, RoleAndSlotPenaltiesStack) {
  auto c = Synthetic();
  float base = pes::PressManScore(c, 100.0f, 0.0f, pes::kPresserRole, 0);
  EXPECT_NEAR(pes::PressManScore(c, 100.0f, 0.0f, 0, 0) - base, 5000.0f, kEps);
  EXPECT_NEAR(pes::PressManScore(c, 100.0f, 0.0f, pes::kPresserRole, -1) - base, 1000.0f, kEps);
}

TEST(PressAngleCode, SlotZeroIsAlwaysZero) {
  auto c = Synthetic();
  EXPECT_EQ(pes::PressAngleCode(c, 0.0f, 0), 0);
  EXPECT_EQ(pes::PressAngleCode(c, 170.0f, 0), 0);
}

TEST(PressAngleCode, SlotOneTakesTheForwardBandOnly) {
  auto c = Synthetic();
  EXPECT_EQ(pes::PressAngleCode(c, 60.0f, 1), 1);   // inside (30, 120)
  EXPECT_EQ(pes::PressAngleCode(c, 20.0f, 1), 0);   // outside: falls through, |a| < 40
  EXPECT_EQ(pes::PressAngleCode(c, -60.0f, 1), 3);  // outside: falls through, a <= 0
}

TEST(PressAngleCode, SlotTwoAndThreeHaveTheirOwnBands) {
  auto c = Synthetic();
  EXPECT_EQ(pes::PressAngleCode(c, 90.0f, 2), 2);   // |a| > 80
  EXPECT_EQ(pes::PressAngleCode(c, 60.0f, 2), 1);   // |a| <= 80 falls through to the band code
  EXPECT_EQ(pes::PressAngleCode(c, -60.0f, 3), 3);  // inside (-120, -30)
}

TEST(PressAngleCode, NoSlotFallsThroughToTheBands) {
  auto c = Synthetic();
  EXPECT_EQ(pes::PressAngleCode(c, 10.0f, -1), 0);
  EXPECT_EQ(pes::PressAngleCode(c, 60.0f, -1), 1);
  EXPECT_EQ(pes::PressAngleCode(c, -60.0f, -1), 3);
  EXPECT_EQ(pes::PressAngleCode(c, 170.0f, -1), 2);
}

// Install relation test: the image's constants are internally consistent.
// Skips without PES21_ROOT, like the other install tests.
TEST(PressConstantsLoader, ImageRelationsHold) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  auto c = pes::LoadPressConstants(*exe, &err);
  ASSERT_TRUE(c.has_value()) << err;
  EXPECT_FLOAT_EQ(c->fullTurn_deg, 2.0f * c->halfTurn_deg);
  EXPECT_FLOAT_EQ(c->rampSpan_deg, c->rampEnd_deg - c->rampStart_deg);
  EXPECT_FLOAT_EQ(c->carrierRampSpan_m, c->carrierRampEnd_m - c->carrierRampStart_m);
  EXPECT_FLOAT_EQ(c->pass2Div, c->pass2Lo - c->pass2Hi);
  EXPECT_FLOAT_EQ(c->slot3Hi_deg, -c->slot1Hi_deg);
}

}  // namespace

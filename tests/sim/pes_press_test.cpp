// Pure tests of the presser picker (src/sim/onthepitch/pespress.hpp) against
// synthetic constants, plus one install relation test. The synthetic values
// are arbitrary on purpose: the tests check the arithmetic, not PES's numbers.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <vector>


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
  c.pass2DelayDistSq = 16.0f;
  c.pass2ZeroRoleDistSq = 81.0f;
  c.pass2SameCodeAdd = 8.0f;
  c.pass2ActionAdd = 4.0f;
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
// Pass-2 fixture: the ball is at the origin, the carrier reference 15 m away, so
// the candidate radius is 15 m. Bearings are measured from +z.
struct Pass2Fixture {
  pes::PressConstants c = Synthetic();
  pes::PressPickInput in;
  pes::PressPickState st;
  Pass2Fixture() {
    in.ballX = 0;
    in.ballZ = 0;
    in.refX = 0;
    in.refZ = -15;
    in.referenceDeg = 0;
    in.onField.fill(true);
  }
  // Places man `m` at `bearingDeg` from the ball, `dist` metres away.
  void Place(int m, float bearingDeg, float dist) {
    const float r = bearingDeg * 3.14159265358979f / 180.0f;
    in.posX[m] = dist * std::sin(r);
    in.posZ[m] = dist * std::cos(r);
  }
};

TEST(PressPass2, FirstUseRecordsTheCodeAndAddsNothing) {
  Pass2Fixture f;
  f.Place(3, 60.0f, 5.0f);
  auto out = pes::RunPresserPicker(f.c, f.in, {{3, 1.0f}}, f.st);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].man, 3);
  EXPECT_NEAR(out[0].score, 1.0f, kEps);
  EXPECT_EQ(f.st.codeTaken[1], 1);
  EXPECT_EQ(f.st.pressCode[3], 1);
  EXPECT_FALSE(f.st.zeroCodeFlag);
}

TEST(PressPass2, ZeroCodeFarManIsSkippedWithoutAPresser) {
  Pass2Fixture f;
  f.Place(5, 0.0f, 11.0f);  // code 0, dist^2 121 > rampStart 20
  EXPECT_TRUE(pes::RunPresserPicker(f.c, f.in, {{5, 1.0f}}, f.st).empty());
}

TEST(PressPass2, ZeroCodeFarManKeptWhenTheTeamHasAPresser) {
  Pass2Fixture f;
  f.in.presserFlag = true;
  f.Place(5, 0.0f, 11.0f);
  auto out = pes::RunPresserPicker(f.c, f.in, {{5, 1.0f}}, f.st);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(f.st.pressCode[5], 0);
  EXPECT_TRUE(f.st.zeroCodeFlag);
}

TEST(PressPass2, PresserRoleIsExemptFromTheZeroCodeSkip) {
  Pass2Fixture f;
  f.Place(5, 0.0f, 11.0f);
  f.in.role[5] = pes::kPresserRole;
  EXPECT_EQ(pes::RunPresserPicker(f.c, f.in, {{5, 1.0f}}, f.st).size(), 1u);
}

TEST(PressPass2, SameCodeTakenAddsEight) {
  Pass2Fixture f;
  f.Place(3, 60.0f, 5.0f);
  f.Place(4, 60.0f, 6.0f);
  auto out = pes::RunPresserPicker(f.c, f.in, {{3, 1.0f}, {4, 1.0f}}, f.st);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_NEAR(out[0].score, 1.0f, kEps);
  EXPECT_NEAR(out[1].score, 9.0f, kEps);
  EXPECT_EQ(f.st.pressCode[4], -1);  // no reassignment
}

TEST(PressPass2, DifferentCodeReassignsAndAddsFour) {
  Pass2Fixture f;
  f.Place(3, 60.0f, 5.0f);
  f.Place(4, 95.0f, 5.0f);  // code 1; +offset moves it to code 2
  auto out = pes::RunPresserPicker(f.c, f.in, {{3, 1.0f}, {4, 1.0f}}, f.st);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_NEAR(out[1].score, 5.0f, kEps);
  EXPECT_EQ(f.st.pressCode[4], 2);
}

TEST(PressPass2, ZeroChosenCodeFarFromTheBallAddsEightAndKeepsCode) {
  Pass2Fixture f;
  f.Place(3, 60.0f, 5.0f);
  f.Place(6, 45.0f, 10.0f);  // code 1; -offset moves it to code 0
  auto out = pes::RunPresserPicker(f.c, f.in, {{3, 1.0f}, {6, 1.0f}}, f.st);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_NEAR(out[1].score, 9.0f, kEps);
  EXPECT_EQ(f.st.pressCode[6], -1);
}

TEST(PressPass2, DelayManTakesHisSlotAsCode) {
  Pass2Fixture f;
  f.in.presserFlag = true;
  f.Place(3, 0.0f, 5.0f);   // takes code 0
  f.Place(7, 0.0f, 3.0f);   // DELAY man in slot 2, dist^2 9 <= 16
  f.in.action[7] = 0x33;
  f.in.pressSlotMan[2] = 7;
  auto out = pes::RunPresserPicker(f.c, f.in, {{3, 1.0f}, {7, 1.0f}}, f.st);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_NEAR(out[1].score, 1.0f, kEps);  // DELAY: no action add
  EXPECT_EQ(f.st.pressCode[7], 2);
}

TEST(PressPass2, ExcludedOffPitchAndOutsideTheRadiusAreDropped) {
  Pass2Fixture f;
  f.Place(9, 60.0f, 5.0f);
  f.Place(10, 60.0f, 5.0f);
  f.Place(8, 0.0f, 16.0f);  // radius 15
  f.in.excludedMan = 9;
  f.in.onField[10] = false;
  EXPECT_TRUE(pes::RunPresserPicker(f.c, f.in, {{9, 1.0f}, {10, 1.0f}, {8, 1.0f}}, f.st).empty());
}

TEST(PressPass1, BucketsOnlyTheSidesOnFieldMenSortedByScore) {
  Pass2Fixture f;
  f.in.onField.fill(false);
  f.in.role.fill(pes::kPresserRole);
  f.in.pressSlotMan = {{0, 1, 11, 12}};
  f.Place(0, 0.0f, 3.0f);   // distSq 9 inside radius 4: score 9 + rampStart-cone 16 = 25
  f.Place(1, 0.0f, 5.0f);   // distSq 25 outside radius: score 25 + rampStart 20 = 45
  f.Place(11, 0.0f, 1.0f);  // the other side: not in this bucket
  f.in.onField[0] = f.in.onField[1] = f.in.onField[11] = true;
  auto b = pes::RunPresserPass1(f.c, f.in, 0);
  ASSERT_EQ(b.size(), 2u);
  EXPECT_EQ(b[0].man, 0);
  EXPECT_NEAR(b[0].score, 25.0f, kEps);
  EXPECT_EQ(b[1].man, 1);
  EXPECT_NEAR(b[1].score, 45.0f, kEps);
}

TEST(PressPass1, NonPresserRoleAndNoSlotAddTheirPenalties) {
  Pass2Fixture f;
  f.in.onField.fill(false);
  f.in.role.fill(pes::kPresserRole);
  f.in.role[1] = 0;  // not the presser role: +notRolePenalty (5000)
  f.Place(0, 0.0f, 3.0f);
  f.Place(1, 0.0f, 5.0f);
  f.in.onField[0] = f.in.onField[1] = true;
  f.in.pressSlotMan = {{0, -1, -1, -1}};  // man 1 has no slot: +notSlotPenalty (1000)
  auto b = pes::RunPresserPass1(f.c, f.in, 0);
  ASSERT_EQ(b.size(), 2u);
  EXPECT_NEAR(b[1].score, 45.0f + 5000.0f + 1000.0f, kEps);
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

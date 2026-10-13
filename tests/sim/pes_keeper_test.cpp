// PES 2021's goalkeeper deflect point and post-catch distribution target
// (docs/re/pes-re-keeper-port.md). Synthetic constants exercise the formulas;
// the install test checks the loader reads the real exe.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cmath>

#include "onthepitch/peskeeper.hpp"

namespace {

// PES's own values, so the ported arithmetic is exercised with the numbers it
// will actually see (they are read from the exe at run time; these are what
// those loads return - docs/re/pes-re-keeper-port.md 5).
pes::KeeperConstants Real() {
  pes::KeeperConstants k{};
  k.deflectSidewaysRadius_m = 1.0f;
  k.deflectRequestedRadius_m = 2.0f;
  k.quarterTurn_deg = 90.0f;
  k.deflectSidewaysGate_deg = 120.0f;
  k.deflectFarSideAngle_deg = 60.0f;
  k.deflectRaisedHeight_m = 2.4f;
  k.distributionCarryUpfield_m = 4.0f;
  k.distributionOwnLineMargin_m = 1.0f;
  k.distributionTargetStep_m = 4.0f;
  k.angleWrap_deg = 360.0f;
  k.degreesPerRadian = 57.2957764f;
  return k;
}

constexpr float kTol = 1e-4f;

}  // namespace

TEST(PesKeeper, DeflectsOnTheRequestedAngleWhenItIsAcrossHisBody) {
  const auto k = Real();
  // Heading 0 (facing +x), asked to deflect 90 degrees to his left: inside the
  // 120-degree gate, so the contact point is 2 m out at the requested angle.
  const pes::DeflectPoint p = pes::PesDeflectPoint(90.0f, 0.0f, k);
  EXPECT_FLOAT_EQ(p.radius_m, 2.0f);
  EXPECT_FLOAT_EQ(p.angle_deg, 90.0f);
  EXPECT_TRUE(p.raised);
}

TEST(PesKeeper, DeflectsTheFarSideWhenTheRequestIsSideways) {
  const auto k = Real();
  // 135 degrees off his heading is outside the gate: FUN_140470d60's
  // heading + 60, 1 m out.
  pes::DeflectPoint p = pes::PesDeflectPoint(135.0f, 0.0f, k);
  EXPECT_FLOAT_EQ(p.radius_m, 1.0f);
  EXPECT_FLOAT_EQ(p.angle_deg, 60.0f);
  // The other side: 0x140470d00's heading - 60.
  p = pes::PesDeflectPoint(-135.0f, 0.0f, k);
  EXPECT_FLOAT_EQ(p.radius_m, 1.0f);
  EXPECT_NEAR(p.angle_deg, 300.0f, kTol);
}

TEST(PesKeeper, DeflectAngleWrapsAndTheGateIsSymmetric) {
  const auto k = Real();
  // 300 degrees off the heading is -60 after the wrap, well inside the gate.
  const pes::DeflectPoint p = pes::PesDeflectPoint(300.0f, 0.0f, k);
  EXPECT_FLOAT_EQ(p.radius_m, 2.0f);
  EXPECT_NEAR(p.angle_deg, 300.0f, kTol);
  // Exactly on the gate (120) takes the far side; just inside does not.
  EXPECT_EQ(pes::PesDeflectPoint(120.0f, 0.0f, k).radius_m, 1.0f);
  EXPECT_EQ(pes::PesDeflectPoint(119.0f, 0.0f, k).radius_m, 2.0f);
}

TEST(PesKeeper, DeflectUsesHisHeadingNotZero) {
  const auto k = Real();
  // A request 180 degrees off his heading is outside the gate, and the wrapped
  // difference is exactly +180, so PES's `d < 0` is false and it takes
  // FUN_140470d60: his heading + 60, 1 m out.
  const pes::DeflectPoint p = pes::PesDeflectPoint(270.0f, 90.0f, k);
  EXPECT_FLOAT_EQ(p.radius_m, 1.0f);
  EXPECT_FLOAT_EQ(p.angle_deg, 150.0f);
  // The other side of the same request: -180 wraps to +180 too, so both signs
  // of the request give the same wrapped difference. Facing 90 asked for -90 is
  // -180 -> also +180 -> heading + 60.
  const pes::DeflectPoint q = pes::PesDeflectPoint(-90.0f, 90.0f, k);
  EXPECT_FLOAT_EQ(q.angle_deg, 150.0f);
  // A request just past the gate takes FUN_140470d00: heading - 60.
  const pes::DeflectPoint r = pes::PesDeflectPoint(90.0f - 130.0f, 90.0f, k);
  EXPECT_FLOAT_EQ(r.angle_deg, 30.0f);
}

TEST(PesKeeper, HoldsTheCarryOnThePenaltyAreaLineWhenHeIsOnHisOwnLine) {
  const auto k = Real();
  // Keeper of the -x goal (attackDir +1) standing on his own line: his signed
  // own penalty-area line is -36.0 (Law-1: 52.5 - 16.5), so the carry is held at
  // 1 m into his own half, -37.0.
  const pes::DistributionPoint carry = pes::PesAfterCatchCarry(-52.5f, 0.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(carry.x, -37.0f);
  EXPECT_FLOAT_EQ(carry.z, 0.0f);
  // Mirrored: the +x goal's keeper is held at +37.0.
  const pes::DistributionPoint mirror = pes::PesAfterCatchCarry(52.5f, -2.0f, -1, 36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(mirror.x, 37.0f);
  EXPECT_FLOAT_EQ(mirror.z, -2.0f);
}

TEST(PesKeeper, CarriesTheBallUpFieldWhenHeIsFurtherOut) {
  const auto k = Real();
  // Inside his own box, 4 m up-field is the carry.
  const pes::DistributionPoint carry = pes::PesAfterCatchCarry(-30.0f, 0.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(carry.x, -26.0f);
  // The boundary is `-A <= dir*x + 4`, i.e. dir*x = -41: exactly on it the
  // up-field branch wins (PES uses <=), and 1 m deeper the carry is held on the
  // mark instead.
  const pes::DistributionPoint edge =
      pes::PesAfterCatchCarry(-41.0f, 0.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(edge.x, -37.0f);
  const pes::DistributionPoint justOutside =
      pes::PesAfterCatchCarry(-41.5f, 0.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(justOutside.x, -37.0f);
}

TEST(PesKeeper, ReflectsTheCarryLateralByOneGoalArea) {
  const auto k = Real();
  // Outside the goal-area half width the lateral is reflected by two widths,
  // PES's `z - sign(z)*2*F`.
  const pes::DistributionPoint carry = pes::PesAfterCatchCarry(-52.5f, 12.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(carry.z, -6.32f);
  const pes::DistributionPoint mirror =
      pes::PesAfterCatchCarry(-52.5f, -12.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(mirror.z, 6.32f);
  // Inside it, the lateral is untouched.
  const pes::DistributionPoint inside =
      pes::PesAfterCatchCarry(-52.5f, 3.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(inside.z, 3.0f);
}

TEST(PesKeeper, StepsTowardTheChosenTeammate) {
  const auto k = Real();
  // PES's bearing is FUN_140a92d40(dz, dx) = atan(dz/dx), and its step
  // FUN_140a8f7c0 adds r*sin(bearing) to x and r*sin(bearing+90) to z. So a
  // teammate directly up the pitch (dz = 0, bearing 0) gets a purely lateral
  // step, and one directly to the side (dx = 0, bearing 90) a purely up-field
  // one. Both are what the code does; the first is PES's oddity.
  const pes::DistributionPoint upfield =
      pes::PesAfterCatchStep(-48.0f, 0.0f, -30.0f, 0.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(upfield.x, -48.0f);
  EXPECT_FLOAT_EQ(upfield.z, 4.0f);
  const pes::DistributionPoint lateral =
      pes::PesAfterCatchStep(-48.0f, 0.0f, -48.0f, 10.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(lateral.x, -44.0f);
  EXPECT_NEAR(lateral.z, 0.0f, kTol);
  // A teammate 10 m to the side and 10 m along: bearing 45 degrees, so the step
  // is r*sin(45) = 2.83 m each way.
  const pes::DistributionPoint diagonal =
      pes::PesAfterCatchStep(-48.0f, 0.0f, -38.0f, 10.0f, 1, -36.0f, 9.16f, k);
  const float bearing = std::atan2(10.0f, 10.0f);  // atan(dz/dx): PES's order
  EXPECT_NEAR(diagonal.x, -48.0f + std::sin(bearing) * 4.0f, kTol);
  EXPECT_NEAR(diagonal.z, 0.0f + std::sin(bearing + k.quarterTurn_deg /
                                                      k.degreesPerRadian) * 4.0f,
              kTol);
}

TEST(PesKeeper, StepsAreHeldAtHisOwnPenaltyAreaLine) {
  const auto k = Real();
  // A step that would land nearer the far side than 1 m short of his own
  // penalty-area line is pulled back to it.
  const pes::DistributionPoint clamped =
      pes::PesAfterCatchStep(-33.0f, 0.0f, -20.0f, 0.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(clamped.x, -37.0f);
  const pes::DistributionPoint mirror =
      pes::PesAfterCatchStep(33.0f, 0.0f, 20.0f, 0.0f, -1, 36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(mirror.x, 37.0f);
  // A step into his own half is left alone.
  const pes::DistributionPoint back =
      pes::PesAfterCatchStep(-45.0f, 0.0f, -60.0f, 0.0f, 1, -36.0f, 9.16f, k);
  EXPECT_FLOAT_EQ(back.x, -45.0f);
  // A step past the goal-area half width is reflected back by two widths.
  const pes::DistributionPoint wide =
      pes::PesAfterCatchStep(-48.0f, 8.0f, -30.0f, 8.0f, 1, -36.0f, 9.16f, k);
  // bearing 0: the step is purely lateral, which takes it past the goal-area
  // half width, so the reflection applies: 12.0 - 2*9.16.
  EXPECT_FLOAT_EQ(wide.z, -6.32f);
}

// The loader: every value the port uses lives in the image, and the two
// constants that only differ by value share VAs read twice. Skipped without the
// user's install.
TEST(PesKeeper, LoadsTheRealValuesFromTheInstall) {
  if (std::getenv("PES21_ROOT") == nullptr) GTEST_SKIP() << "no PES21_ROOT";
  const std::string root = std::getenv("PES21_ROOT");
  std::string error;
  auto exe = pes::PeImage::Open(root + "/PES2021.exe", &error);
  ASSERT_TRUE(exe.has_value()) << error;
  auto k = pes::LoadKeeperConstants(*exe, &error);
  ASSERT_TRUE(k.has_value()) << error;
  // Checked through relations, not pinned numbers: the deflect radii are the
  // two literals PES's exec uses, and the raised height is its own constant.
  EXPECT_GT(k->deflectSidewaysGate_deg, 100.0f);
  EXPECT_LT(k->deflectSidewaysGate_deg, 140.0f);
  EXPECT_EQ(k->deflectSidewaysRadius_m, 1.0f);
  EXPECT_EQ(k->deflectRequestedRadius_m, 2.0f);
  EXPECT_EQ(k->deflectFarSideAngle_deg, 60.0f);
  EXPECT_EQ(k->quarterTurn_deg, 90.0f);
  EXPECT_EQ(k->distributionCarryUpfield_m, 4.0f);
  EXPECT_EQ(k->distributionTargetStep_m, 4.0f);
  EXPECT_NEAR(k->deflectRaisedHeight_m, 2.4f, 1e-3f);
  EXPECT_NEAR(k->degreesPerRadian, 57.29578f, 1e-4f);
}

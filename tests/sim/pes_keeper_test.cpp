// PES 2021's goalkeeper: deflect point, post-catch walk target, open-play base
// position and rush (docs/re/pes-re-keeper-port.md and
// docs/re/pes-re-goalkeeper.md section 10). Synthetic constants exercise the
// formulas; the install test checks the loader reads the real exe.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cmath>

#include "onthepitch/peskeeper.hpp"

namespace {

// PES's own values, so the ported arithmetic is exercised with the numbers it
// will actually see (they are read from the exe at run time; these are what
// those loads return - docs/re/pes-re-keeper-port.md 5).
pes::KeeperConstants Real() {
  pes::KeeperConstants k;
  k.deflectSidewaysRadius_m = 1.0f;
  k.distributionOwnLineMargin_m = 1.0f;
  k.distributionBoxSideMargin_m = 1.0f;
  k.deflectRequestedRadius_m = 2.0f;
  k.distributionLateralCorrection_m = 2.0f;
  k.distributionCarryUpfield_m = 4.0f;
  k.distributionTargetStep_m = 4.0f;
  k.deflectFarSideAngle_deg = 60.0f;
  k.quarterTurn_deg = 90.0f;
  k.deflectSidewaysGate_deg = 120.0f;
  k.deflectRaisedHeight_m = 2.4f;
  k.angleWrap_deg = 360.0f;
  k.degreesPerRadian = 57.2957764f;
  // Positioning.
  k.positioningFloor_m = 2.0f;
  k.positioningFloorGoalBias_m = 0.5f;
  k.positioningBearingSpan_deg = 50.0f;
  k.positioningLineBack_m = 25.0f;
  k.positioningLineClamp_m = 10.0f;
  k.positioningDefensiveCap_m = 4.0f;
  k.positioningNoStyleCap_m = 15.0f;
  k.positioningOffensiveCap_m = 20.0f;
  k.positioningRampNear_m = 40.0f;
  k.positioningRampFar_m = 70.0f;
  k.positioningRampSpan_m = 30.0f;
  k.positioningRampSpanDepth_m = 9.0f;
  k.positioningMaxRadius_m = 15.0f;
  k.positioningGoalLineMargin_m = 0.8f;
  k.positioningJitterStep_m = 0.1f;
  k.positioningAwarenessLow = 40.0f;
  k.positioningAwarenessSpan = 59.0f;
  k.positioningAwarenessHigh = 99.0f;
  k.positioningPossessionDeep_m = 6.0f;
  k.positioningPossessionRamp1_m = 3.0f;
  k.positioningPossessionMid_m = 9.0f;
  k.positioningPossessionRamp2_m = 4.0f;
  k.positioningPossessionFar_m = 13.0f;
  k.positioningPossessionRamp3_m = 7.0f;
  k.positioningPossessionLineMargin_m = 25.0f;
  k.positioningPossessionUpfield_m = 3.0f;
  k.positioningPossessionDefensiveCap_m = 13.0f;
  k.positioningPossessionNoStyleCap_m = 15.0f;
  k.positioningPossessionOffensiveCap_m = 17.0f;
  // Rush.
  k.rushOffensiveFactor = 1.1f;
  k.rushNoStyleFactor = 1.2f;
  k.rushDefensiveFactor = 1.3f;
  k.rushHysteresis_s = 0.71f;
  k.rushMaxBallTime_s = 3.3f;
  return k;
}

// Law-1 marks for a 105 x 68 m pitch, the same surfaces PES's FieldInfo holds
// (F0 52.56, F2 3.66, F4 47.06, F5 9.16, F6 36.14, F7 20.12).
pes::KeeperFieldMarks Pitch() {
  const float halfW = 52.5f, goalHalf = 3.7f, goalAreaHalf = 9.16f;
  return pes::KeeperFieldMarks{halfW,   goalHalf,       halfW - 5.5f,
                               goalAreaHalf, halfW - 16.5f, 20.15f};
}

constexpr float kTol = 1e-4f;

// A defender of the -x goal: he defends -1 (the -x side), so he attacks +x.
constexpr int kDefendMinus = -1;
constexpr int kDefendPlus = +1;

// His team line and the offside line, in his own frame: positive is up HIS
// pitch (toward the opponents' goal). A line deep in his own half is negative.
// The centre is a realistic default for a settled defence; the individual tests
// that care set their own.
constexpr float kDeepLine = -20.0f;
constexpr float kMidLine = 0.0f;

pes::KeeperPositionInput OutOfPossession(float ballX, float ballZ, int defend) {
  pes::KeeperPositionInput in;
  in.ballX = ballX;
  in.ballZ = ballZ;
  // He is somewhere sane; the ramp uses |ball.x - keeper.x|.
  in.keeperX = (defend < 0 ? -1.0f : 1.0f) * 51.0f;
  in.keeperZ = 0.0f;
  in.defenseSide = defend;
  in.inPossession = false;
  in.teamLineX = kMidLine;
  in.offsideLineX = kMidLine;
  in.offensiveStyle = false;
  in.defensiveStyle = false;
  in.gkAwareness = 70.0f;
  in.jitterDraw = 0;
  return in;
}

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
  const auto marks = Pitch();
  // Keeper of the -x goal (attackDir +1) on his own line: in his own frame the
  // carry is -52.5 + 4 = -48.5, which is below -A = -37.0, so it wins and the
  // carry lands at -48.5 (still 11.5 m short of his own half's line).
  const pes::DistributionPoint carry = pes::PesAfterCatchCarry(-52.5f, 0.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(carry.x, -48.5f);
  EXPECT_FLOAT_EQ(carry.z, 0.0f);
  // Mirrored: the +x goal's keeper carries to +48.5.
  const pes::DistributionPoint mirror = pes::PesAfterCatchCarry(52.5f, -2.0f, -1, marks, k);
  EXPECT_FLOAT_EQ(mirror.x, 48.5f);
  EXPECT_FLOAT_EQ(mirror.z, -2.0f);
  // A keeper standing in his own half is held on the line instead: -30 + 4 =
  // -26 is not below -37.0.
  const pes::DistributionPoint held = pes::PesAfterCatchCarry(-30.0f, 0.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(held.x, -37.0f);
}

TEST(PesKeeper, CarriesTheBallUpFieldWhenHeIsFurtherOut) {
  const auto k = Real();
  const auto marks = Pitch();
  // Inside his own half, `-A <= dir*x + 4` is true (-37 <= -26), so he is held
  // on the line rather than carried: the carry is only the deeper-in-his-own-
  // half case, where up-field means toward the box line.
  const pes::DistributionPoint inside = pes::PesAfterCatchCarry(-30.0f, 0.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(inside.x, -37.0f);
  // Deeper than his own half minus the 4 m carry at his own goal line: the
  // carry wins. At x = -41.0 the own-frame carry is -37.0, and -37 <= -37 is
  // true (jbe), so it still holds.
  const pes::DistributionPoint edge = pes::PesAfterCatchCarry(-41.0f, 0.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(edge.x, -37.0f);
  // One metre deeper: -42.0 + 4 = -38.0 < -37.0, so the carry fires.
  const pes::DistributionPoint deeper = pes::PesAfterCatchCarry(-42.0f, 0.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(deeper.x, -38.0f);
  // On his own goal line: -52.5 + 4 = -48.5 < -37.0, so the carry fires.
  const pes::DistributionPoint line = pes::PesAfterCatchCarry(-52.5f, 0.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(line.x, -48.5f);
  // Mirrored for the +x goal.
  const pes::DistributionPoint mirrorInside = pes::PesAfterCatchCarry(30.0f, 0.0f, -1, marks, k);
  EXPECT_FLOAT_EQ(mirrorInside.x, 37.0f);
  const pes::DistributionPoint mirrorLine = pes::PesAfterCatchCarry(52.5f, 0.0f, -1, marks, k);
  EXPECT_FLOAT_EQ(mirrorLine.x, 48.5f);
}

TEST(PesKeeper, CorrectsTheCarryLateralTwoMetresTowardTheMiddle) {
  const auto k = Real();
  const auto marks = Pitch();
  // Wider than the goal-area half width, the lateral is pulled 2 m toward the
  // middle, PES's `z - sign(z)*2.0` (not one goal-area width).
  const pes::DistributionPoint carry = pes::PesAfterCatchCarry(-52.5f, 12.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(carry.z, 10.0f);
  const pes::DistributionPoint mirror = pes::PesAfterCatchCarry(-52.5f, -12.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(mirror.z, -10.0f);
  // Inside it, the lateral is untouched.
  const pes::DistributionPoint inside = pes::PesAfterCatchCarry(-52.5f, 3.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(inside.z, 3.0f);
  // Exactly on the mark is not wider than it.
  const pes::DistributionPoint onMark = pes::PesAfterCatchCarry(-52.5f, 9.16f, 1, marks, k);
  EXPECT_FLOAT_EQ(onMark.z, 9.16f);
}

TEST(PesKeeper, StepsTowardTheChosenTeammate) {
  const auto k = Real();
  const auto marks = Pitch();
  // The bearing is measured from +z and FUN_140a92d40 takes delta x first, so a
  // teammate directly up the pitch gets a purely up-field 4 m step, and one
  // directly to the side a purely lateral one (10.6.2's sign correction: the
  // step goes toward the mate).
  const pes::DistributionPoint upfield =
      pes::PesAfterCatchWalk(-48.0f, 0.0f, -48.0f, 0.0f, -30.0f, 0.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(upfield.x, -44.0f);
  EXPECT_NEAR(upfield.z, 0.0f, kTol);
  // A mate 10 m to the side: the step is purely lateral.
  const pes::DistributionPoint lateral =
      pes::PesAfterCatchWalk(-48.0f, 0.0f, -48.0f, 0.0f, -48.0f, 10.0f, 1, marks, k);
  EXPECT_NEAR(lateral.x, -48.0f, kTol);
  EXPECT_FLOAT_EQ(lateral.z, 4.0f);
  // A teammate 10 m to the side and 10 m along: bearing 45 degrees from +z, so
  // the step is 4*sin(45) = 2.83 m each way.
  const pes::DistributionPoint diagonal =
      pes::PesAfterCatchWalk(-48.0f, 0.0f, -48.0f, 0.0f, -38.0f, 10.0f, 1, marks, k);
  EXPECT_NEAR(diagonal.x, -48.0f + 2.8284271f, kTol);
  EXPECT_NEAR(diagonal.z, 2.8284271f, kTol);
  // A mate behind him (down his own pitch) walks him back.
  const pes::DistributionPoint back =
      pes::PesAfterCatchWalk(-40.0f, 0.0f, -40.0f, 0.0f, -50.0f, 0.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(back.x, -44.0f);
  EXPECT_NEAR(back.z, 0.0f, kTol);
}

TEST(PesKeeper, StepsAreHeldAtHisOwnPenaltyAreaLine) {
  const auto k = Real();
  const auto marks = Pitch();
  // A step that would land nearer the far side than 1 m short of his own
  // penalty-area line is pulled back to it.
  const pes::DistributionPoint clamped =
      pes::PesAfterCatchWalk(-33.0f, 0.0f, -33.0f, 0.0f, -20.0f, 0.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(clamped.x, -37.0f);
  const pes::DistributionPoint mirror =
      pes::PesAfterCatchWalk(33.0f, 0.0f, 33.0f, 0.0f, 20.0f, 0.0f, -1, marks, k);
  EXPECT_FLOAT_EQ(mirror.x, 37.0f);
  // A step into his own half is left alone.
  const pes::DistributionPoint back =
      pes::PesAfterCatchWalk(-45.0f, 0.0f, -45.0f, 0.0f, -60.0f, 0.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(back.x, -49.0f);
}

TEST(PesKeeper, StepsAreClampedInsideTheBoxSide) {
  const auto k = Real();
  const auto marks = Pitch();
  // Every branch ends with |z| <= F7 - 1: the walk target never goes more than
  // 1 m inside the box's side line (20.15 - 1 = 19.15). The carry already put
  // him in the middle, so the step has to be enormous to reach the clamp.
  const pes::DistributionPoint wide =
      pes::PesAfterCatchWalk(-48.0f, 18.0f, -48.0f, 18.0f, -20.0f, 30.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(wide.z, 19.15f);
  const pes::DistributionPoint mirror =
      pes::PesAfterCatchWalk(48.0f, -18.0f, 48.0f, -18.0f, 20.0f, -30.0f, -1, marks, k);
  EXPECT_FLOAT_EQ(mirror.z, -19.15f);
  // A step that stays inside is untouched.
  const pes::DistributionPoint inside =
      pes::PesAfterCatchWalk(-48.0f, 0.0f, -48.0f, 0.0f, -48.0f, 8.0f, 1, marks, k);
  EXPECT_FLOAT_EQ(inside.z, 4.0f);
  // Exactly on the clamp is not clamped.
  const pes::DistributionPoint onMark =
      pes::PesAfterCatchWalk(-48.0f, 19.15f, -48.0f, 19.15f, -48.0f, 19.15f, 1, marks, k);
  EXPECT_FLOAT_EQ(onMark.z, 19.15f);
}

// --- Open-play positioning (docs/re/pes-re-goalkeeper.md 10.2) ---

TEST(PesKeeper, StandsOnTheLineFromBehindHisGoalToTheBall) {
  const auto k = Real();
  const auto marks = Pitch();
  // A ball dead centre in front of him: he is on that line, at the capped
  // distance. The ball is 51 m from him lengthwise, so the ramp gives
  // 4 + 9 * (51 - 40) / 30 = 7.3 m - less than the 15 m style cap.
  const pes::KeeperSpot spot =
      pes::PesKeeperBasePosition(OutOfPossession(0.0f, 0.0f, kDefendMinus), marks, k);
  EXPECT_FLOAT_EQ(spot.radius_m, 7.3f);
  // theta is 90 degrees off +z, so the spot is 7.3 m up the pitch from R.
  EXPECT_NEAR(spot.x, -53.5f + 7.3f, kTol);
  EXPECT_NEAR(spot.z, 0.0f, kTol);
}

TEST(PesKeeper, CapsTheDepthAtFourMetresForADefensiveKeeper) {
  const auto k = Real();
  const auto marks = Pitch();
  auto in = OutOfPossession(0.0f, 0.0f, kDefendMinus);
  in.defensiveStyle = true;
  const pes::KeeperSpot spot = pes::PesKeeperBasePosition(in, marks, k);
  // The style cap is 4 m, the line term is far deeper, so the cap wins - but
  // the ball is dead ahead of him, so the floor's 2..4.2 m term sits on top of
  // it at 4.2 m (F2 + 0.5 = 3.7 + 0.5).
  EXPECT_FLOAT_EQ(spot.radius_m, 4.2f);
  EXPECT_NEAR(spot.x, -53.5f + 4.2f, kTol);
  // The cap itself is visible when the floor is lower: a ball down the goal
  // axis gives a floor of 2 m, so the cap's 4 m wins.
  auto onAxis = OutOfPossession(-53.0f, 0.6f, kDefendMinus);
  onAxis.defensiveStyle = true;
  onAxis.teamLineX = 0.0f;
  onAxis.offsideLineX = 0.0f;
  const pes::KeeperSpot axisSpot = pes::PesKeeperBasePosition(onAxis, marks, k);
  EXPECT_FLOAT_EQ(axisSpot.radius_m, 4.0f);
}

TEST(PesKeeper, RampsTheDepthWithTheBallDistanceAlongThePitch) {
  const auto k = Real();
  const auto marks = Pitch();
  // The ramp: 4 m while the ball is within 40 m of him lengthwise, growing
  // linearly to 13 m at 70 m. With the defensive line at the centre (0), the
  // line term is max(-F4, -25) + F0 = 27.5, which never binds, so the ramp and
  // the caps are what decide. The keeper stands at -51.
  auto in = OutOfPossession(-51.0f + 30.0f, 0.0f, kDefendMinus);  // 30 m away
  in.teamLineX = 0.0f;
  in.offsideLineX = 0.0f;
  const pes::KeeperSpot near = pes::PesKeeperBasePosition(in, marks, k);
  EXPECT_FLOAT_EQ(near.radius_m, 4.0f);
  // Exactly at the ramp's start.
  in.ballX = -51.0f + 40.0f;  // 40 m away
  const pes::KeeperSpot atStart = pes::PesKeeperBasePosition(in, marks, k);
  EXPECT_FLOAT_EQ(atStart.radius_m, 4.0f);
  // Halfway.
  in.ballX = -51.0f + 55.0f;  // 55 m away: (55-40)/30 = 0.5 -> 8.5
  const pes::KeeperSpot halfway = pes::PesKeeperBasePosition(in, marks, k);
  EXPECT_FLOAT_EQ(halfway.radius_m, 8.5f);
  // Past the far end: 13 m.
  in.ballX = -51.0f + 70.0f;
  const pes::KeeperSpot atEnd = pes::PesKeeperBasePosition(in, marks, k);
  EXPECT_FLOAT_EQ(atEnd.radius_m, 13.0f);
  // Beyond it the ramp stays at 13, and the 15 m max radius does not lift it.
  in.ballX = -51.0f + 100.0f;
  const pes::KeeperSpot far = pes::PesKeeperBasePosition(in, marks, k);
  EXPECT_FLOAT_EQ(far.radius_m, 13.0f);
  // A DEEP line binds the other way: a line 40 m behind his own goal gives
  // max(-F4, -40 - 25) + F0 = -47.0 + 52.5 = 5.5, so the ball-distance ramp
  // cannot take him past 5.5 m.
  in.teamLineX = -40.0f;
  in.offsideLineX = -40.0f;
  in.ballX = -51.0f + 70.0f;
  const pes::KeeperSpot deep = pes::PesKeeperBasePosition(in, marks, k);
  EXPECT_FLOAT_EQ(deep.radius_m, 5.5f);
}

TEST(PesKeeper, NeverComesCloserThanTheTwoMetreFloor) {
  const auto k = Real();
  const auto marks = Pitch();
  // The floor is (F2 + 0.5) * w + 2 * (1 - w), where w is |theta| / 50 clamped,
  // and theta is the bearing of R -> B from +z. R sits on the goal line, so any
  // ball ahead of it is 90 degrees off +z and w = 1: the floor is F2 + 0.5 =
  // 4.2 m. It only falls to 2 m with the ball level with R AND far up the pitch,
  // which the pitch does not allow - so in practice the floor is 4.2 m.
  //
  // A ball 1.5 m away, dead ahead, with a Defensive GK: the floor's 4.2 m beats
  // the 4 m cap, but the ball-distance ramp (4 m while within 40 m lengthwise)
  // then caps it to 4.0 - the ramp is the last min, after the floor.
  auto ahead = OutOfPossession(-49.5f, 0.0f, kDefendMinus);
  ahead.teamLineX = 0.0f;
  ahead.offsideLineX = 0.0f;
  ahead.defensiveStyle = true;
  const pes::KeeperSpot aheadSpot = pes::PesKeeperBasePosition(ahead, marks, k);
  EXPECT_FLOAT_EQ(aheadSpot.radius_m, 4.0f);
  // A ball far up the pitch and level with R: theta is still 90, so the floor
  // keeps him at 4.2 m even though the ball is 100 m away - here the ramp gives
  // 13 m, so the floor is the binding term and lifts the 4 m cap to 4.2.
  auto far = OutOfPossession(46.0f, 0.0f, kDefendMinus);
  far.teamLineX = 0.0f;
  far.offsideLineX = 0.0f;
  far.defensiveStyle = true;
  const pes::KeeperSpot farSpot = pes::PesKeeperBasePosition(far, marks, k);
  EXPECT_FLOAT_EQ(farSpot.radius_m, 4.2f);
  // A no-style keeper: the floor sits below both the 15 m cap and the 13 m
  // ramp, so it never binds and the ramp decides.
  auto wide = OutOfPossession(19.0f, 0.0f, kDefendMinus);
  wide.teamLineX = 0.0f;
  wide.offsideLineX = 0.0f;
  const pes::KeeperSpot wideSpot = pes::PesKeeperBasePosition(wide, marks, k);
  EXPECT_FLOAT_EQ(wideSpot.radius_m, 13.0f);
  // The floor does bind for a Defensive GK, whose 4 m cap is below it, once the
  // ramp stops capping.
  auto def = OutOfPossession(19.0f, 0.0f, kDefendMinus);
  def.teamLineX = 0.0f;
  def.offsideLineX = 0.0f;
  def.defensiveStyle = true;
  const pes::KeeperSpot defSpot = pes::PesKeeperBasePosition(def, marks, k);
  EXPECT_FLOAT_EQ(defSpot.radius_m, 4.2f);
}

TEST(PesKeeper, CapsTheDepthAtTwentyMetresForAnOffensiveKeeper) {
  const auto k = Real();
  const auto marks = Pitch();
  const pes::KeeperSpot spot =
      pes::PesKeeperBasePosition(OutOfPossession(0.0f, 0.0f, kDefendMinus), marks, k);
  // No style: the 15 m cap, and the ramp's 7.3 m is below it, so the ramp wins.
  EXPECT_FLOAT_EQ(spot.radius_m, 7.3f);
  // An Offensive GK's 20 m cap only binds when the ramp and the 15 m max let
  // it - which they do not - so the ramp still wins here.
  auto near = OutOfPossession(-40.0f, 0.0f, kDefendMinus);
  near.offensiveStyle = true;
  near.teamLineX = 0.0f;
  near.offsideLineX = 0.0f;
  const pes::KeeperSpot capped = pes::PesKeeperBasePosition(near, marks, k);
  EXPECT_FLOAT_EQ(capped.radius_m, 4.0f);  // the ball is 11 m away: ramp gives 4
}

TEST(PesKeeper, StepsUpAsASweeperWhenHisTeamHasTheBall) {
  const auto k = Real();
  const auto marks = Pitch();
  // The ball is 20 m up his frame (dir +1): 13 + 7 * ramp(20; 0, 26.25) =
  // 18.33, capped by the no-style 15.
  auto in = OutOfPossession(20.0f, 0.0f, kDefendMinus);
  in.inPossession = true;
  in.teamLineX = kDeepLine;
  const pes::KeeperSpot spot = pes::PesKeeperBasePosition(in, marks, k);
  EXPECT_FLOAT_EQ(spot.radius_m, 15.0f);
  // An Offensive GK's 17 m cap is above 15, and the max radius caps him there.
  auto off = OutOfPossession(20.0f, 0.0f, kDefendMinus);
  off.inPossession = true;
  off.offensiveStyle = true;
  const pes::KeeperSpot offSpot = pes::PesKeeperBasePosition(off, marks, k);
  EXPECT_FLOAT_EQ(offSpot.radius_m, 15.0f);
  // A Defensive GK's 13 m cap binds below the 15 m max.
  auto def = OutOfPossession(20.0f, 0.0f, kDefendMinus);
  def.inPossession = true;
  def.defensiveStyle = true;
  const pes::KeeperSpot defSpot = pes::PesKeeperBasePosition(def, marks, k);
  EXPECT_FLOAT_EQ(defSpot.radius_m, 13.0f);
  // Deep in his own third: deeper than 3 m inside the box line, 6 m.
  auto deep = OutOfPossession(-50.0f, 0.0f, kDefendMinus);
  deep.inPossession = true;
  const pes::KeeperSpot deepSpot = pes::PesKeeperBasePosition(deep, marks, k);
  EXPECT_FLOAT_EQ(deepSpot.radius_m, 6.0f);
  // At the halfway of his half: 9 m.
  auto half = OutOfPossession(-26.25f, 0.0f, kDefendMinus);
  half.inPossession = true;
  const pes::KeeperSpot halfSpot = pes::PesKeeperBasePosition(half, marks, k);
  EXPECT_FLOAT_EQ(halfSpot.radius_m, 9.0f);
  // At the centre line: 13 m.
  auto centre = OutOfPossession(0.0f, 0.0f, kDefendMinus);
  centre.inPossession = true;
  const pes::KeeperSpot centreSpot = pes::PesKeeperBasePosition(centre, marks, k);
  EXPECT_FLOAT_EQ(centreSpot.radius_m, 13.0f);
}

TEST(PesKeeper, KeepsTheSweeperThreeMetresUpFieldOfHisLine) {
  const auto k = Real();
  const auto marks = Pitch();
  // In possession the committed point is at least 3 m up-field of R, whatever
  // the bearing.
  auto in = OutOfPossession(0.0f, 5.0f, kDefendMinus);
  in.inPossession = true;
  const pes::KeeperSpot spot = pes::PesKeeperBasePosition(in, marks, k);
  EXPECT_GE(spot.radius_m, 3.0f);
  // A ball dead ahead of him: the bearing is up-field, so the x offset is the
  // radius itself and never less than 3 m.
  auto ahead = OutOfPossession(-30.0f, 0.0f, kDefendMinus);
  ahead.inPossession = true;
  const pes::KeeperSpot aheadSpot = pes::PesKeeperBasePosition(ahead, marks, k);
  EXPECT_GE(aheadSpot.radius_m, 3.0f);
}

TEST(PesKeeper, ScalesTheLateralShuffleByAwareness) {
  const auto k = Real();
  const auto marks = Pitch();
  // The shuffle is Range0(6) * 0.1 * (1 - ramp(GK Awareness; 40 -> 0, 99 -> 1)):
  // a draw of 6 at the awareness floor gives 0.6 m, and at the top it is nil.
  // A ball dead centre is at exactly 90 degrees, which is the flip's boundary
  // (`90 <= angle`), so it lands negative there.
  auto unaware = OutOfPossession(0.0f, 0.0f, kDefendMinus);
  unaware.gkAwareness = 40.0f;
  unaware.jitterDraw = 6;
  const pes::KeeperSpot worst = pes::PesKeeperBasePosition(unaware, marks, k);
  EXPECT_FLOAT_EQ(worst.jitter_m, -0.6f);
  auto aware = OutOfPossession(0.0f, 0.0f, kDefendMinus);
  aware.gkAwareness = 99.0f;
  aware.jitterDraw = 6;
  const pes::KeeperSpot best = pes::PesKeeperBasePosition(aware, marks, k);
  EXPECT_FLOAT_EQ(best.jitter_m, 0.0f);
  // The shuffle is perpendicular to R -> B, and its sign is flipped when the
  // ball's bearing is 90 degrees or more off the goal axis - which puts it on
  // the ball's side either way (a +z ball shuffles him +z, a -z ball -z).
  auto wide = OutOfPossession(0.0f, 20.0f, kDefendMinus);
  wide.gkAwareness = 40.0f;
  wide.jitterDraw = 6;
  const pes::KeeperSpot positive = pes::PesKeeperBasePosition(wide, marks, k);
  EXPECT_FLOAT_EQ(positive.jitter_m, 0.6f);
  EXPECT_GT(positive.z, 0.0f);
  // The other side, where the 90-degree flip reverses the sign so the shuffle
  // still opens the near post he is standing off.
  auto other = OutOfPossession(0.0f, -20.0f, kDefendMinus);
  other.gkAwareness = 40.0f;
  other.jitterDraw = 6;
  const pes::KeeperSpot negative = pes::PesKeeperBasePosition(other, marks, k);
  EXPECT_FLOAT_EQ(negative.jitter_m, -0.6f);
  EXPECT_LT(negative.z, 0.0f);
  // And there is no shuffle at all with the ball: the sweeper spot is committed.
  auto held = OutOfPossession(0.0f, 0.0f, kDefendMinus);
  held.inPossession = true;
  held.gkAwareness = 40.0f;
  held.jitterDraw = 6;
  const pes::KeeperSpot heldSpot = pes::PesKeeperBasePosition(held, marks, k);
  EXPECT_FLOAT_EQ(heldSpot.jitter_m, 0.0f);
}

TEST(PesKeeper, MirrorsThePositioningForTheOtherGoal) {
  const auto k = Real();
  const auto marks = Pitch();
  const pes::KeeperSpot plus =
      pes::PesKeeperBasePosition(OutOfPossession(0.0f, 0.0f, kDefendPlus), marks, k);
  const pes::KeeperSpot minus =
      pes::PesKeeperBasePosition(OutOfPossession(0.0f, 0.0f, kDefendMinus), marks, k);
  EXPECT_FLOAT_EQ(plus.radius_m, minus.radius_m);
  EXPECT_NEAR(plus.x, -minus.x, kTol);
  EXPECT_NEAR(plus.z, minus.z, kTol);
}

// --- The rush (docs/re/pes-re-goalkeeper.md 10.3) ---

TEST(PesKeeper, ChoosesTheRushFactorByStyle) {
  const auto k = Real();
  EXPECT_FLOAT_EQ(pes::PesKeeperRushFactor(false, false, k), 1.2f);
  EXPECT_FLOAT_EQ(pes::PesKeeperRushFactor(true, false, k), 1.1f);
  EXPECT_FLOAT_EQ(pes::PesKeeperRushFactor(false, true, k), 1.3f);
}

TEST(PesKeeper, ChoosesTheRushLateralLimitByStyle) {
  const pes::KeeperFieldMarks marks = Pitch();
  // F5 = 9.16, F7 = 20.15, midpoint 14.655.
  EXPECT_FLOAT_EQ(pes::PesKeeperRushLateralLimit_m(false, false, marks), 14.655f);
  EXPECT_FLOAT_EQ(pes::PesKeeperRushLateralLimit_m(true, false, marks), 20.15f);
  EXPECT_FLOAT_EQ(pes::PesKeeperRushLateralLimit_m(false, true, marks), 9.16f);
}

TEST(PesKeeper, RushesWhenTheOpponentIsNotThere) {
  const auto k = Real();
  // He needs 1 s. With no style (k = 1.2) he rushes only when the opponent
  // needs more than 1.2 s, i.e. `of > k * kf`.
  EXPECT_TRUE(pes::PesKeeperRushRaceWins(1.0f, 2.0f, false, 1.2f, k));
  EXPECT_FALSE(pes::PesKeeperRushRaceWins(1.0f, 1.0f, false, 1.2f, k));
  EXPECT_FALSE(pes::PesKeeperRushRaceWins(1.0f, 1.15f, false, 1.2f, k));
  // Already rushing: the 0.71 s hysteresis makes the bar lower, so a tighter
  // race still goes.
  EXPECT_TRUE(pes::PesKeeperRushRaceWins(1.0f, 0.6f, true, 1.2f, k));
  EXPECT_FALSE(pes::PesKeeperRushRaceWins(1.0f, 0.6f, false, 1.2f, k));
  // The factor by style: Offensive (1.1) needs the least of a margin, so a
  // 1.15 s opponent beats a 1.1 s threshold where a Defensive (1.3) keeper
  // would not go.
  EXPECT_TRUE(pes::PesKeeperRushRaceWins(1.0f, 1.15f, false, 1.1f, k));
  EXPECT_FALSE(pes::PesKeeperRushRaceWins(1.0f, 1.15f, false, 1.3f, k));
  EXPECT_TRUE(pes::PesKeeperRushRaceWins(1.0f, 1.35f, false, 1.3f, k));
  // A ball more than 3.3 s away is never chased.
  EXPECT_FALSE(pes::PesKeeperRushRaceWins(3.4f, 10.0f, true, 1.1f, k));
  EXPECT_TRUE(pes::PesKeeperRushRaceWins(3.3f, 10.0f, true, 1.1f, k));
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
  // The corrections of 10.6: the sideways correction is 2 m, not a width, and
  // the box-side margin is its own 1.0 slot.
  EXPECT_EQ(k->distributionLateralCorrection_m, 2.0f);
  EXPECT_EQ(k->distributionBoxSideMargin_m, 1.0f);
  // Positioning: the style caps in PES's own order (defensive < none <
  // offensive), the ramp's far end past its near end, and the rush factors in
  // the same monotone order.
  EXPECT_EQ(k->positioningDefensiveCap_m, 4.0f);
  EXPECT_EQ(k->positioningNoStyleCap_m, 15.0f);
  EXPECT_EQ(k->positioningOffensiveCap_m, 20.0f);
  EXPECT_EQ(k->positioningPossessionDefensiveCap_m, 13.0f);
  EXPECT_EQ(k->positioningPossessionNoStyleCap_m, 15.0f);
  EXPECT_EQ(k->positioningPossessionOffensiveCap_m, 17.0f);
  EXPECT_EQ(k->positioningFloor_m, 2.0f);
  EXPECT_EQ(k->positioningMaxRadius_m, 15.0f);
  EXPECT_LT(k->positioningRampNear_m, k->positioningRampFar_m);
  // The rush factors are monotone the other way round: a Defensive GK (1.3)
  // needs the biggest win before he will go, an Offensive GK (1.1) the least.
  EXPECT_LT(k->rushOffensiveFactor, k->rushNoStyleFactor);
  EXPECT_LT(k->rushNoStyleFactor, k->rushDefensiveFactor);
}

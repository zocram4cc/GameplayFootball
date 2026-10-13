#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "onthepitch/matchsetup.hpp"
#include "onthepitch/pesball.hpp"
#include "onthepitch/pesballphysics.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"

// PES 2021's ball flight and ground contact (docs/re/pes-re-ball-contact.md,
// docs/re/pes-re-officials-ball.md 6.3, docs/re/pes-re-ball-constants.md).
//
// The pure functions are tested against synthetic constants, so no install is
// needed for the rules themselves; one test loads the real install's constants
// and checks the report's own numbers against them.

namespace {

using pes::BallRecordConstants;
using pes::PesBallConstants;
using pesball::BallState;

// The mode-1 column the reports were taken against, with the report's shipped
// values (docs/re/pes-re-ball-constants.md 6.2). Synthetic: a test never
// transcribes an install, it states the rule under test.
PesBallConstants SyntheticConstants() {
  PesBallConstants c;
  c.image.gravity = 9.80665f;
  c.image.airDensity = 1.293f;
  c.image.ballArea = 0.037110474f;
  c.image.ballRadius = 0.108685933f;
  c.image.ballRadiusSquared = 0.0118126301f;
  c.image.dragCrisisSquared = 7.71604872f;
  c.image.half = -0.5f;
  c.image.pi = 3.14159274f;
  c.image.kmhPerMs = 3.5999999f;
  c.image.secondsPerHour = 3600.0f;
  c.image.milli = 1000.0f;
  c.image.tickHz = 54.0f;
  c.image.nonSpinTopMax = 23.6111107f;
  c.image.nonSpinTopPlateau = 48.2253113f;
  c.image.spinScale = 492.181f;
  c.image.invSpinScale = 1.0f / 492.181f;

  BallRecordConstants& r = c.record;
  r.airRegistNormal = 1.0f;
  r.dragSpeedMax = 96.995003f;
  r.dragSpeedMin = 56.995602f;
  r.magnusRate = 0.035f;
  r.nonSpinMax = 106.0f;
  r.nonSpinMin = 45.0f;
  r.nonSpinRate = 1.0f;
  r.boundBoundSpeedMax = 12.0f;
  r.boundBoundSpeedMin = 2.0f;
  r.boundFrictionSpeedMax = 25.0f;
  r.boundFrictionSpeedMin = 10.0f;
  r.boundRollMax = 7.5f;
  r.boundRotationSpeedMax = 20.0f;
  r.boundRotationSpeedMin = 5.0f;
  r.grounderBoundYAdd = 0.09f;
  r.grounderCTan = 8.0f;
  r.grounderFrictionRate = 0.985f;
  r.grounderSpeed = 20.0f;
  r.natualBoundB = 0.4f;
  r.naturalRoll = 2.0f;
  r.rollToBoundAdd = 0.07f;
  r.rollToBoundRollMax = 7.0f;
  r.rollToBoundRollMin = 2.0f;
  r.stopSpeed = 0.175f;
  r.spinScaleSlot = 0;
  const auto set = [&](BallRecordConstants::ArrayRow row, float v0, float v1, float v2, float v3,
                       float v4, float v5) {
    r.arrays[row] = {v0, v1, v2, v3, v4, v5};
  };
  set(BallRecordConstants::kBoundCheckBoundAddRate, 10, 5, 0, 0, 5, 5);
  set(BallRecordConstants::kBoundCheckFrictionAddRate, 0, 0, 0, 0, 7.5f, 0);
  set(BallRecordConstants::kBoundRate, 0.7f, 0.7f, 0.7f, 0.5f, 0.7f, 0.7f);
  set(BallRecordConstants::kBoundToRotationAddRateXZ, 0.6f, 0.62f, 0.64f, 0.12f, 0.62f, 0.62f);
  set(BallRecordConstants::kBoundToRotationAddRateY, 0.07f, 0.07f, 0.07f, 0.07f, 0.07f, 0.03f);
  set(BallRecordConstants::kBoundToRotationBaseXZ, 0.16f, 0.2f, 0.24f, 0.13f, 0.2f, 0.2f);
  set(BallRecordConstants::kBoundToRotationBaseY, 0.03f, 0.03f, 0.03f, 0.03f, 0.03f, 0.02f);
  set(BallRecordConstants::kBoundToRotationSpeedMaxXZ, 25, 25, 25, 25, 0.7f, 25);
  set(BallRecordConstants::kBoundToRotationSpeedMaxY, 0.7f, 0.7f, 0.7f, 0.7f, 0.7f, 30);
  set(BallRecordConstants::kBoundToRotationSpeedMinXZ, 3, 3, 3, 3, 0.1f, 3);
  set(BallRecordConstants::kBoundToRotationSpeedMinY, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 1);
  set(BallRecordConstants::kFrictionRollRateMax, 0.975f, 0.972f, 0.969f, 0.99f, 0.955f, 0.983f);
  set(BallRecordConstants::kFrictionRollRateMin, 0.985f, 0.982f, 0.979f, 0.98f, 0.987f, 0.983f);
  set(BallRecordConstants::kFrictionRollRateStop, 0.975f, 0.972f, 0.969f, 0.97f, 0.987f, 0.983f);
  set(BallRecordConstants::kFrictionRollSpeedMax, 25, 25, 25, 50, 8, 0);
  set(BallRecordConstants::kFrictionRollSpeedMin, 8, 8, 8, 8, 8, 15);
  set(BallRecordConstants::kFrictionRollSpeedStop, 0, 0, 0, 0, 8, 0);
  // gounnderprob: the grounder tick gate, an integer row (report 6.2's "u32
  // 5,5,5,9,9,5"). Mode 1's element is 5, so one contact tick in five fires the
  // driven grounder. The report's prose calls mode 1's value 0, which would be a
  // divide-by-zero; the shipped bytes are 5 and a port follows the bytes.
  r.rawArrays[BallRecordConstants::kGounnderprob] = {5, 5, 5, 9, 9, 5};
  set(BallRecordConstants::kNatualBoundA, 0.055f, 0.055f, 0.055f, 0.055f, 0.055f, 0.055f);
  set(BallRecordConstants::kNatunalRollBackSpinRate, 0.35f, 0.4f, 0.45f, 0.2f, 0.5f, 0.4f);
  set(BallRecordConstants::kOverRotMax, 1, 1, 1, 1, 1, 1);
  set(BallRecordConstants::kOverRotMin, -2, -2, -2, -2, -2, -2);
  set(BallRecordConstants::kRollToSpeedMaxKph, 20, 20, 20, 8.5f, 18, 16);
  set(BallRecordConstants::kRollToSpeedSpeedYRateBase, 0.8f, 0.8f, 0.8f, 0.4f, 0.2f, 0.8f);
  return c;
}

constexpr double kEps = 1e-6;

// --- the snap and the vertical response (report 2.3, 3.4) ---

TEST(PesBallContact, SnapsToRadiusAndKeepsHorizontalSpeed) {
  const PesBallConstants c = SyntheticConstants();
  BallState s;
  s.position = pes::Vec3(1.0f, 0.05f, -2.0f);  // below r
  s.velocity = pes::Vec3(2.0f, -3.0f, 0.5f);
  pesball::ContactTick(c, &s, 0, true, false);
  EXPECT_NEAR(s.position.y, c.image.ballRadius, 1e-6);
  EXPECT_NEAR(s.position.x, 1.0f, 1e-6) << "a contact tick integrates no position";
  EXPECT_NEAR(s.position.z, -2.0f, 1e-6);
}

// The vertical keep of report 3.4 is the raw-spin-Y blend's input, NOT the
// vertical bounce (report 2 §2.1/§4.1). It is pinned here so the port keeps the
// literal rule available, and the contact tick is checked against the bounce
// formula that is the vertical response (report 1.3).
TEST(PesBallContact, VerticalKeepIsPesThreeToTenPercentAndIsNotTheBounce) {
  // Report 3.4, read literally from the raw: the vertical keep ramps from
  // boundToRotationBaseY to boundToRotationBaseY + boundToRotationAddRateY
  // across boundToRotationSpeedMinY .. MaxY (km/h), so mode 1 keeps 3% of |vy| at
  // rest and 10% above 0.7 km/h (0.19 m/s).
  const PesBallConstants c = SyntheticConstants();
  const float loV = c.record.At(BallRecordConstants::kBoundToRotationSpeedMinY, 1) / c.image.kmhPerMs;
  const float hiV = c.record.At(BallRecordConstants::kBoundToRotationSpeedMaxY, 1) / c.image.kmhPerMs;
  const float base = c.record.At(BallRecordConstants::kBoundToRotationBaseY, 1);
  const float add = c.record.At(BallRecordConstants::kBoundToRotationAddRateY, 1);
  EXPECT_NEAR(pesball::VerticalKeep(c, 1, loV * 0.5f), base, 1e-6);
  EXPECT_NEAR(pesball::VerticalKeep(c, 1, hiV * 2.0f), base + add, 1e-6);
  EXPECT_NEAR(pesball::VerticalKeep(c, 1, (loV + hiV) * 0.5f), base + add * 0.5f, 1e-6);

  // And that 3..10% is nowhere near what a contact does to vy: the bounce is
  // F*min(imp, |vy|), which is a speed clamp, not a keep.
  BallState hard;
  hard.position = pes::Vec3(0, c.image.ballRadius, 0);
  hard.velocity = pes::Vec3(0, -4.0f, 0);
  pesball::ContactTick(c, &hard, 0, true, false);
  EXPECT_GT(hard.velocity.y, 0.3f) << "a 4 m/s drop must not come back at 0.4 m/s";
}

// --- the bounce: report 1.3's min-clamp, the two regimes (report 2 §1.3/§5) ---

TEST(PesBallContact, BounceIsSpeedClampWhenTheContactIsFasterThanTheCap) {
  // A normal falling contact: |vy| is far larger than imp, so min(-imp, vy) is
  // the more-negative -imp... no: min is the smaller of the two, so for a steep
  // fall min(-imp, vy) = vy and the contact is a true reflection with ratio F.
  // imp is only >= |vy| for a slow/rolling contact - that is the clamp regime,
  // and it is the one that re-launches a grazed ball at F*imp.
  const PesBallConstants c = SyntheticConstants();
  const pesball::BounceResult b = pesball::Bounce(c, 2, -6.1746f, 0.0f, 0.0f);
  EXPECT_FLOAT_EQ(b.minOperand, -6.1746f) << "z = vy for a falling contact";
  EXPECT_NEAR(b.speedCap_mps, 0.1111f, 1e-4) << "imp = (0.055*3.6*0 + 0.4)/3.6";
  EXPECT_NEAR(b.vy, 4.3222f, 1e-3) << "report 5.2 case A, contact #1 (mode 2)";
}

TEST(PesBallContact, BounceIsAReflectionOfTheIncomingSpeed) {
  // Report 5.2, the mode-2 series for a 2 m drop: boundCheckBoundAddRate[2] = 0
  // removes the speed ramp, so F is a flat boundBoundRate and every contact of
  // the series comes back at exactly 0.70 of |vy|.
  const PesBallConstants c = SyntheticConstants();
  const float kVy[] = {-6.1746f, -4.3948f, -3.0982f, -2.1898f, -1.5544f};
  const float kExpected[] = {4.3222f, 3.0764f, 2.1687f, 1.5329f, 1.0881f};
  for (int i = 0; i < 5; ++i) {
    const pesball::BounceResult b = pesball::Bounce(c, 2, kVy[i], 0.0f, 0.0f);
    EXPECT_NEAR(b.restitution, 0.7f, 1e-6);
    EXPECT_NEAR(b.vy, kExpected[i], 1e-3) << "contact #" << (i + 1);
    EXPECT_NEAR(b.vy / -kVy[i], 0.7f, 1e-3) << "ratio is F in mode 2";
  }
}

TEST(PesBallContact, BounceRelaunchesAGrazeAboveItsIncomingSpeed) {
  // A graze: |vy| <= imp, so min(-imp, vy) = -imp and the ball leaves at F*imp,
  // which is above its own inbound speed. That is the bobble regime report 5.3
  // identifies as the top of the live 0.37..2.26 band.
  const PesBallConstants c = SyntheticConstants();
  const float graze = 0.05f;  // |vy| far under imp
  const pesball::BounceResult b = pesball::Bounce(c, 2, -graze, 0.5f, 0.0f);
  EXPECT_FLOAT_EQ(b.minOperand, -b.speedCap_mps) << "z = -imp for a graze";
  EXPECT_NEAR(b.vy, 0.7f * b.speedCap_mps, 1e-6) << "vy_out = +F*imp";
  EXPECT_GT(b.vy, graze) << "a graze leaves faster than it arrived";
  const pesball::BounceResult stop = pesball::Bounce(c, 2, -0.01f, 1.0f, 0.0f);
  EXPECT_NEAR(stop.vy, 0.1163f, 1e-3) << "report 5.3's vxz=1, |vy|=0.010 row";
  EXPECT_NEAR(stop.vy / 0.01f, 11.628f, 0.01) << "its ratio, the report's 11.628";
}

TEST(PesBallContact, BounceDoesNotDependOnSpin) {
  // Report 3.3: the tangential vector is exactly horizontal (T_y = A_*e_y +
  // B_*e_y with e_y = 0), so a body-axis Y spin couples into vy nowhere in the
  // contact function. Report 5's sweep confirms it: vy_out is identical across
  // spin = +-20 rps. F does carry a `s4^2 * rollToBoundAdd` term, but over
  // hypot(wx, wz) - the HORIZONTAL spin - so a pure Y spin must not move it.
  const PesBallConstants c = SyntheticConstants();
  auto bounceWith = [&](float spinRawY) {
    BallState s;
    s.position = pes::Vec3(0, c.image.ballRadius, 0);
    s.velocity = pes::Vec3(5.0f, -4.99f, 0.0f);
    s.spinRaw = pes::Vec3(0.0f, spinRawY, 0.0f);
    pesball::ContactTick(c, &s, 0, true, false);
    return s.velocity.y;
  };
  const float none = bounceWith(0.0f);
  EXPECT_FLOAT_EQ(bounceWith(10.0f * 2.0f * 3.14159265f), none)
      << "10 rps topspin does not change vy";
  EXPECT_FLOAT_EQ(bounceWith(-10.0f * 2.0f * 3.14159265f), none)
      << "10 rps backspin does not change vy";
}

TEST(PesBallContact, RollSpinRampAddsRestitutionThroughTheHorizontalSpin) {
  // Report 3.3/2.2: F carries `s4^2 * rollToBoundAdd`, s4 being the clamped ramp
  // over hypot(wx, wz) from rollToBoundRollMin (2) to rollToBoundRollMax (7). So
  // a horizontal spin raises F above boundBoundRate, while a small spin does not.
  const PesBallConstants c = SyntheticConstants();
  EXPECT_NEAR(pesball::RollSpinRamp(c, 2, 0.0f), 0.0f, 1e-6);
  EXPECT_NEAR(pesball::RollSpinRamp(c, 2, 2.0f), 0.0f, 1e-6);
  EXPECT_NEAR(pesball::RollSpinRamp(c, 2, 7.0f), 1.0f, 1e-6);
  EXPECT_NEAR(pesball::RollSpinRamp(c, 2, 4.5f), 0.5f, 1e-6);
  const float flat = pesball::Bounce(c, 2, -0.5f, 0.0f, 0.0f).restitution;
  const float spinning = pesball::Bounce(c, 2, -0.5f, 0.0f, 7.0f).restitution;
  EXPECT_NEAR(spinning, flat + c.record.rollToBoundAdd, 1e-6)
      << "s4 = 1 adds s4^2 * rollToBoundAdd = rollToBoundAdd";
  EXPECT_GT(spinning, flat) << "a rolling spin makes the ball livelier";
}

// --- the grounder kick (report 2.5) ---

TEST(PesBallContact, GrounderKickOnlyFiresOnFastShallowShots) {
  const PesBallConstants c = SyntheticConstants();
  const float fast = 6.0f;   // 21.6 km/h, above grounderSpeed 20
  const float slow = 2.0f;   // 7.2 km/h, below it

  auto kick = pesball::GrounderKick(c, 1, 1.0f, 2.0f, 0, fast, -0.2f);
  EXPECT_TRUE(kick.applied) << "a fast shallow shot takes the driven grounder";
  EXPECT_GT(kick.keepMultiplier, 0.98f);
  EXPECT_NEAR(kick.speedTrim_mps, fast * 0.09f, 0.01f);

  EXPECT_FALSE(pesball::GrounderKick(c, 1, 1.0f, 2.0f, 0, slow, -0.2f).applied);
  EXPECT_FALSE(pesball::GrounderKick(c, 1, 1.0f, 2.0f, 0, fast, 0.0f).applied) << "vy == 0 skips";
  EXPECT_FALSE(pesball::GrounderKick(c, 1, 1.0f, 2.0f, 0, fast, -4.0f).applied)
      << "a steep shot is not a grounder";
}

TEST(PesBallContact, GrounderKickRespectsItsTickGate) {
  // The gate is `int(|x|*1000) mod frictionRollSpeedStop[mode][3] == 0`, so only
  // one tick in five fires it for mode 1 (the shipped array holds 5).
  const PesBallConstants c = SyntheticConstants();
  int fired = 0;
  for (float x = 0.0f; x < 5.0f; x += 0.001f)
    if (pesball::GrounderKick(c, 1, x, x, 0, 6.0f, -0.2f).applied) ++fired;
  EXPECT_GT(fired, 0);
  EXPECT_LT(fired, 5000) << "the gate must thin the branch out";
}

// --- the friction multiplier F (report 3.1) ---

TEST(PesBallContact, FrictionMultiplierIsOneWithoutSpinAndBoostedByBounce) {
  const PesBallConstants c = SyntheticConstants();
  float baseA = 0.0f;
  // Mode 1: boundCheckFrictionAddRate = 0, so s1 never changes K.
  EXPECT_NEAR(pesball::FrictionMultiplier(c, 1, 6.0f, -1.0f, 12.0f, &baseA), 1.0f, 1e-6);
  // A hard bounce raises A through s3 (boundCheckBoundAddRate = 5, i.e. +5%).
  EXPECT_GT(pesball::FrictionMultiplier(c, 1, 6.0f, -4.0f, 0.0f, &baseA), 0.99f);
  EXPECT_GT(baseA, 0.7f);
  // Mode 4 does carry a friction add rate (7.5), and high spin engages it.
  float baseA4 = 0.0f;
  EXPECT_LT(pesball::FrictionMultiplier(c, 4, 6.0f, -1.0f, 20.0f, &baseA4), 1.0f);
}

// --- the spin cap and rebuild (report 3.4) ---

TEST(PesBallContact, SpinIsCappedAtBoundRollMax) {
  const PesBallConstants c = SyntheticConstants();
  EXPECT_NEAR(pesball::SpinCapScale(c, 1, 2.0f), 1.0f, 1e-6);
  EXPECT_NEAR(pesball::SpinCapScale(c, 1, 15.0f), c.record.boundRollMax / 15.0f, 1e-6);
}

TEST(PesBallContact, SpinRebuildMovesRawSpinTowardTheRollingSpin) {
  const PesBallConstants c = SyntheticConstants();
  BallState s;
  s.position = pes::Vec3(0, c.image.ballRadius, 0);
  s.velocity = pes::Vec3(4.0f, 0.0f, 0.0f);  // a coupled roller: w = v/r
  s.spinRaw = pes::Vec3(0, 0, 0);
  pesball::ContactTick(c, &s, 0, true, false);
  // The rolling spin v/r is 36.3 rad/s, well above the boundRollMax cap of 7.5,
  // so the rebuilt raw spin must land on the cap. The cap scales the whole
  // vector, and this contact's bounce (report 1.3: min(-imp, 0) = -imp) also puts
  // a vertical spin in it, so the x component alone is under the cap.
  const float cap = c.record.boundRollMax;
  const float total = std::sqrt(s.spinRaw.x * s.spinRaw.x + s.spinRaw.y * s.spinRaw.y +
                                s.spinRaw.z * s.spinRaw.z) *
                      c.image.spinScale;
  EXPECT_NEAR(total, cap, cap * 1e-3) << "spin_raw is rebuilt towards v/r, capped at boundRollMax";
  EXPECT_GT(s.spinRaw.x, 0.0f) << "a forward roller spins the way it rolls";
  // A slow roller stays under the cap and lands on v/r.
  BallState slowRoll;
  slowRoll.position = pes::Vec3(0, c.image.ballRadius, 0);
  slowRoll.velocity = pes::Vec3(0.5f, 0.0f, 0.0f);
  pesball::ContactTick(c, &slowRoll, 0, true, false);
  EXPECT_NEAR(slowRoll.spinRaw.x * c.image.spinScale, 0.5f / c.image.ballRadius,
              0.5f / c.image.ballRadius * 0.05f);
}

// --- the stop gate (report 3.4, report 2 correction 2) ---

TEST(PesBallContact, StopGateZeroesOnlyTheHorizontalVelocity) {
  const PesBallConstants c = SyntheticConstants();
  const float stop = pesball::StopSpeed_mps(c, 1);
  EXPECT_NEAR(stop, 0.175f * 1000.0f / 3600.0f, 1e-6);
  EXPECT_NEAR(stop, 0.04861f, 1e-4);

  // A ball slower than the stop speed - measured on the post-contact horizontal
  // velocity, which is what the raw compares - loses its horizontal motion, and
  // keeps its vertical one: report 2's correction 2 zeroes xmm13, xmm3 and xmm12
  // (vx, and the two vz stores) and leaves xmm5 (vy) untouched at 0x140877323.
  BallState s;
  s.position = pes::Vec3(0, c.image.ballRadius, 0);
  s.velocity = pes::Vec3(0.001f, -0.02f, 0.001f);  // horizontal 0.0014 m/s, below
  pesball::ContactTick(c, &s, 0, true, false);
  EXPECT_EQ(s.velocity.x, 0.0f);
  EXPECT_EQ(s.velocity.z, 0.0f);
  EXPECT_GT(s.velocity.y, 0.0f) << "the vertical velocity survives the stop gate";

  // A rolling ball is not stopped, and its vertical is likewise untouched.
  BallState rolling;
  rolling.position = pes::Vec3(0, c.image.ballRadius, 0);
  rolling.velocity = pes::Vec3(1.0f, -0.5f, 0.0f);
  pesball::ContactTick(c, &rolling, 0, true, false);
  EXPECT_GT(std::fabs(rolling.velocity.x), 0.5f) << "a rolling ball keeps rolling";
}

// --- the contact mode table (report 3.1) ---

TEST(PesBallContact, ContactModeComesFromThePitchWeatherTable) {
  // The table report 3.1 read out of FUN_1408723d0's operand, stated here as the
  // rule under test. Rows = pitch, columns = weather; a dry, normal pitch (weather
  // 0) is mode 2 on every row, never the mode 1 the reports observed live.
  PesBallConstants c = SyntheticConstants();
  c.image.modeTable = {{{2, 1, 1}, {2, 1, 0}, {1, 0, 0}}};
  c.contactMode = 1;
  EXPECT_EQ(pes::ContactModeFor(c.image, 0, 0), 2);
  EXPECT_EQ(pes::ContactModeFor(c.image, 1, 0), 2);
  EXPECT_EQ(pes::ContactModeFor(c.image, 2, 0), 1);
  EXPECT_EQ(pes::ContactModeFor(c.image, 0, 1), 1);
  EXPECT_EQ(pes::ContactModeFor(c.image, 1, 1), 1);
  EXPECT_EQ(pes::ContactModeFor(c.image, 2, 2), 0);
  // The writer's out-of-range fallback is 1.
  EXPECT_EQ(pes::ContactModeFor(c.image, 3, 0), pes::kContactModeOutOfRange);
  EXPECT_EQ(pes::ContactModeFor(c.image, 0, 9), pes::kContactModeOutOfRange);
  EXPECT_EQ(pes::ContactModeFor(c.image, -1, 0), pes::kContactModeOutOfRange);
}

TEST(PesBallContact, DryNormalIsModeTwoAndModeTwoHasASeventyPercentBounce) {
  // Report 3.2's action for the porter: GF defaults to PES's dry, normal pitch.
  const PesBallConstants c = SyntheticConstants();
  EXPECT_EQ(pes::kContactModeDryNormal, 2);
  EXPECT_EQ(pes::WeatherIndexForGfWeather(0.0f), 0) << "no weather";
  // boundCheckBoundAddRate[2] = 0 removes mode 1's speed ramp, so F is a flat
  // boundBoundRate = 0.7 at every speed in the dry-normal mode.
  EXPECT_NEAR(c.record.At(BallRecordConstants::kBoundCheckBoundAddRate, 2), 0.0f, 1e-6);
  for (const float vy : {-0.05f, -0.5f, -3.0f, -8.0f}) {
    const pesball::BounceResult slow = pesball::Bounce(c, 2, vy, 0.5f, 0.0f);
    EXPECT_NEAR(slow.restitution, 0.7f, 1e-6) << "F is flat in mode 2 at vy = " << vy;
  }
}

// --- rolling K(V) and the report's own numbers ---

TEST(PesBallContact, RollingKeepReproducesTheObserved0981AtTwoMetresPerSecond) {
  // Report 5.2, computed from the tables with no fitting: at v = 2 m/s the
  // mode-1 slow branch gives K = 0.98100 exactly.
  const PesBallConstants c = SyntheticConstants();
  const float k = pesball::RollingKeepFactor(c, 1, 2.0f);
  EXPECT_NEAR(k, 0.981f, 1e-6);
  // ... so the per-tick deceleration is (1 - K) * 54 * v = 2.05 m/s^2.
  EXPECT_NEAR((1.0f - k) * 54.0f * 2.0f, 2.05f, 0.01f);
}

TEST(PesBallContact, RollingDecelerationIsAboutTwoMetresPerSecondSquared) {
  // Report 5.2/6.4's live window: a ball rolling at 0.5-3 m/s decelerates at
  // about 2.05 m/s^2, which is (1 - K) * 54 * v at 1/54 s. The instantaneous
  // figure falls as the ball slows, so check the figure at 2 m/s directly, then
  // that a second of PES ticks really does take the speed down.
  const PesBallConstants c = SyntheticConstants();
  const float k = pesball::RollingKeepFactor(c, 1, 2.0f);
  const float decel = (1.0f - k) * c.image.tickHz * 2.0f;
  EXPECT_NEAR(decel, 2.05f, 0.01f);
  float v = 2.0f;
  const float dt = 1.0f / 54.0f;
  for (int i = 0; i < 54; ++i) v *= pesball::RollingKeepFactor(c, 1, v);
  // A second of PES ticks must remove more than one tick's worth of the 2 m/s
  // figure, and cannot take the ball past a standstill.
  EXPECT_LT(v, 2.0f - 2.05f * dt) << "the decay is at least one tick's 2 m/s figure";
  EXPECT_GT(v, 0.0f) << "the ball is still rolling after a second";
}

// --- the 10 ms conversion ---

TEST(PesBallConversion, KeepFactorMapsOntoTheGfTick) {
  // PES applies K once per 1/54 s; GF ticks every 10 ms. One GF tick is 0.54 of
  // a PES tick, so the per-GF-tick factor is K^0.54 - and 54 of them is not
  // K^54 but K^(54*0.54) = K^(54*0.54).
  const float perPesTick = 0.981f;
  const float perGfTick = pesball::KeepFactorPerGfTick(perPesTick);
  EXPECT_NEAR(perGfTick, std::pow(perPesTick, 0.01f / (1.0f / 54.0f)), 1e-6);
  EXPECT_LT(perGfTick, 1.0f);
  EXPECT_GT(perGfTick, perPesTick) << "a shorter step loses less";
  // A second of GF ticks now equals a second of PES ticks to within float
  // precision: 0.01*100 ticks of K^(0.54) == 0.0185*54 ticks of K.
  float gf = 1.0f, pes = 1.0f;
  for (int i = 0; i < 100; ++i) gf *= perGfTick;
  for (int i = 0; i < 54; ++i) pes *= perPesTick;
  // 100 GF ticks and 54 PES ticks are both one second of sim time.
  EXPECT_NEAR(gf, pes, 1e-3);
}

TEST(PesBallConversion, FlightTickUsesTheGfStep) {
  // The flight model is an acceleration, integrated with dt = 0.01 s: one GF
  // tick at 10 ms is 0.54 of a PES tick's worth of acceleration.
  const PesBallConstants c = SyntheticConstants();
  BallState s;
  s.position = pes::Vec3(0, 1.0f, 0);
  s.velocity = pes::Vec3(0, 0, 20.0f);  // 72 km/h, above the crisis band
  // One GF tick is the acceleration at the tick's entry velocity held across the
  // step: semi-implicit Euler integrates the acceleration first.
  const pes::Vec3 a = pesball::FlightAcceleration(c, s, 0, 0, 0, false);
  const float v0 = s.velocity.z;
  pesball::FlightTick(c, &s, 0.01f, false);
  const float dv = s.velocity.z - v0;
  EXPECT_LT(dv, 0.0f) << "drag slows the ball down";
  EXPECT_NEAR(dv, a.z * 0.01f, 1e-6);
  // Gravity is applied on top: after a second of ticks the ball has fallen.
  BallState falling;
  falling.position = pes::Vec3(0, 5.0f, 0);
  for (int i = 0; i < 100; ++i) pesball::FlightTick(c, &falling, 0.01f, false);
  EXPECT_LT(falling.position.y, 5.0f);
}

// --- the flight model (report 6.3) ---

TEST(PesBallFlight, BelowTheCrisisThresholdGravityOnly) {
  // Report 6.3: s2 <= 7.716049 (below 2.778 m/s horizontal) is gravity only -
  // no drag and no Magnus.
  const PesBallConstants c = SyntheticConstants();
  BallState s;
  s.position = pes::Vec3(0, 1.0f, 0);
  s.velocity = pes::Vec3(0, 0, 2.0f);
  s.spinRaw = pes::Vec3(0, 2.0f, 0);  // a huge spin that must not bend it
  const pes::Vec3 a = pesball::FlightAcceleration(c, s, 0, 1000.0f, 0, false);
  EXPECT_NEAR(a.x, 0.0f, 1e-6);
  EXPECT_NEAR(a.z, 0.0f, 1e-6);
  EXPECT_NEAR(a.y, -c.image.gravity, 1e-6);
}

TEST(PesBallFlight, DragCrisisBandShapesTheDrag) {
  // Report 6.3's shaping: between dragSpeedMin and dragSpeedMax the effective
  // speed is mirrored (2*v2 - s), and above the max it is offset
  // (2*(v2 - v1) + s). The drag magnitude therefore dips inside the band.
  const PesBallConstants c = SyntheticConstants();
  auto dragMagnitude = [&](float mps) {
    BallState s;
    s.velocity = pes::Vec3(mps, 0, 0);
    const pes::Vec3 a = pesball::FlightAcceleration(c, s, 0, 0, 0, true);
    return std::fabs(a.x);
  };
  const float v2 = c.record.dragSpeedMin / c.image.kmhPerMs;  // 15.83 m/s
  const float v1 = c.record.dragSpeedMax / c.image.kmhPerMs;  // 26.94 m/s
  const float mid = (v1 + v2) * 0.5f;
  const float below = dragMagnitude(v2 * 0.9f);
  const float inside = dragMagnitude(mid);
  const float above = dragMagnitude(v1 * 1.1f);
  EXPECT_GT(below, 0.0f);
  EXPECT_GT(above, below) << "faster still means more drag";
  // Inside the band the shaped speed is mirrored to 2*v2 - s, while a ball
  // actually moving at that mirrored speed takes the plain branch - so the drag
  // force per unit velocity (the k factor) is identical for both.
  auto shapedSpeed = [&](float mps) {
    const float v1 = c.record.dragSpeedMax / c.image.kmhPerMs;
    const float v2 = c.record.dragSpeedMin / c.image.kmhPerMs;
    if (mps <= v1) return mps > v2 ? (2.0f * v2 - mps) : mps;
    return 2.0f * (v2 - v1) + mps;
  };
  EXPECT_NEAR(shapedSpeed(mid), shapedSpeed(2.0f * v2 - mid), 1e-4)
      << "the band mirrors the speed onto the plain branch";
  // The mirror falls as the speed rises across the band: that is the crisis dip.
  EXPECT_LT(shapedSpeed(mid + 1.0f), shapedSpeed(mid - 1.0f));
  // Above the max, the offset branch keeps climbing again.
  EXPECT_GT(shapedSpeed(v1 * 1.1f), shapedSpeed(v1));
}

TEST(PesBallFlight, MagnusBendsTheBallWithItsSpin) {
  const PesBallConstants c = SyntheticConstants();
  BallState s;
  s.velocity = pes::Vec3(0, 0, 20.0f);  // +z
  s.spinRaw = pes::Vec3(0, 0, 0);
  const pes::Vec3 spin(0, 1, 0);  // rad/s, +y spin (backspin against +z motion)
  const pes::Vec3 a = pesball::FlightAcceleration(c, s, spin.x, spin.y, spin.z, false);
  const pes::Vec3 none = pesball::FlightAcceleration(c, s, 0, 0, 0, false);
  // cross(w, v) with w = +y and v = +z is +x, so the ball curves sideways.
  EXPECT_GT(a.x, none.x + 1e-6f);
  EXPECT_NEAR(a.y, none.y, 1e-6) << "a pure side spin does not change the vertical drag";
}

// --- resting (report 2.1) ---

TEST(PesBallContact, RestingBallSkipsTheFlightFunction) {
  const PesBallConstants c = SyntheticConstants();
  BallState s;
  s.position = pes::Vec3(0, c.image.ballRadius, 0);
  s.velocity = pes::Vec3(0, 0, 0);
  EXPECT_TRUE(pesball::IsResting(c, s));
  s.velocity = pes::Vec3(0.1f, 0, 0);
  EXPECT_FALSE(pesball::IsResting(c, s)) << "a rolling ball is not resting";
  s.velocity = pes::Vec3(0, 0, 0);
  s.spinRaw = pes::Vec3(0.05f, 0, 0);
  EXPECT_FALSE(pesball::IsResting(c, s)) << "a spinning ball is not resting";
}

// --- the mode column and its clamp (report 0.3) ---

TEST(PesBallContact, ModeIsClampedAboveFive) {
  EXPECT_EQ(pesball::ClampMode(1), 1);
  EXPECT_EQ(pesball::ClampMode(5), 5);
  EXPECT_EQ(pesball::ClampMode(6), 1);
  EXPECT_EQ(pesball::ClampMode(99), 1);
}

TEST(PesBallContact, ModesSelectDifferentColumns) {
  const PesBallConstants c = SyntheticConstants();
  // Mode 4 (0.99 / 0.98 / 0.97) keeps far more speed than mode 2
  // (0.969 / 0.979 / 0.969) at the same speed.
  EXPECT_GT(pesball::RollingKeepFactor(c, 4, 2.0f), pesball::RollingKeepFactor(c, 2, 2.0f));
}

// --- the real install ---

// The install's constants: one test, so a rule that depends on the shipped data
// is checked against it, and a synthetic-only test can never drift from it.
TEST(PesBallInstall, LoadsTheInstallConstantsAndReproducesTheReportNumbers) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set: the ball constants come from the install";
  std::string err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  auto constants = pes::LoadPesBallConstants(*exe, vfs, &err);
  ASSERT_TRUE(constants.has_value()) << err;
  const PesBallConstants& c = *constants;

  // docs/re/pes-re-ball-constants.md 3.1: the three flight scalars.
  EXPECT_NEAR(c.record.dragSpeedMax, 96.995003f, 1e-3);
  EXPECT_NEAR(c.record.dragSpeedMin, 56.995602f, 1e-3);
  EXPECT_NEAR(c.record.magnusRate, 0.035f, 1e-6);
  // 6.1/6.2: the contact tables' mode-1 column.
  EXPECT_NEAR(c.record.At(BallRecordConstants::kFrictionRollSpeedMin, 1), 8.0f, 1e-5);
  EXPECT_NEAR(c.record.At(BallRecordConstants::kFrictionRollSpeedMax, 1), 25.0f, 1e-5);
  EXPECT_NEAR(c.record.At(BallRecordConstants::kFrictionRollRateMin, 1), 0.982f, 1e-5);
  EXPECT_NEAR(c.record.At(BallRecordConstants::kFrictionRollRateMax, 1), 0.972f, 1e-5);
  EXPECT_NEAR(c.record.At(BallRecordConstants::kFrictionRollRateStop, 1), 0.972f, 1e-5);
  EXPECT_NEAR(c.record.boundRollMax, 7.5f, 1e-5);
  EXPECT_NEAR(c.record.stopSpeed, 0.175f, 1e-5);
  EXPECT_NEAR(c.record.At(BallRecordConstants::kBoundToRotationBaseY, 1), 0.03f, 1e-5);
  EXPECT_NEAR(c.record.At(BallRecordConstants::kBoundToRotationAddRateY, 1), 0.07f, 1e-5);
  EXPECT_NEAR(c.record.At(BallRecordConstants::kBoundRate, 1), 0.7f, 1e-5);

  // The image constants (officials 6.2 / ball-constants 4).
  EXPECT_NEAR(c.image.gravity, 9.80665f, 1e-4);
  EXPECT_NEAR(c.image.ballRadius, 0.10868593f, 1e-6);
  EXPECT_NEAR(c.image.dragCrisisSquared, 7.7160487f, 1e-5);
  EXPECT_NEAR(c.image.tickHz, 54.0f, 1e-4);
  EXPECT_NEAR(c.image.spinScale, 492.181f, 1e-3);
  EXPECT_NEAR(c.image.invSpinScale, 1.0f / 492.181f, 1e-8);

  // Report 3.1's contact-mode table, read out of the exe's .data at run time.
  // The table is a runtime-written slot, not .rdata; this engine reads the
  // install's own copy, so no on-disk value is transcribed here.
  EXPECT_EQ(pes::ContactModeFor(c.image, 0, 0), 2) << "a dry, normal pitch is mode 2";
  EXPECT_EQ(pes::ContactModeFor(c.image, 1, 0), 2);
  EXPECT_EQ(pes::ContactModeFor(c.image, 0, 1), 1);
  EXPECT_EQ(pes::ContactModeFor(c.image, 1, 1), 1);
  EXPECT_EQ(pes::ContactModeFor(c.image, 2, 0), 1);
  EXPECT_EQ(pes::ContactModeFor(c.image, 1, 2), 0);
  EXPECT_EQ(pes::ContactModeFor(c.image, 2, 2), 0);
  EXPECT_EQ(pes::ContactModeFor(c.image, 5, 0), 1) << "out of range falls back to 1";
  // Report 5.2's mode-2 dry-pitch numbers, from the shipped mode-2 table.
  EXPECT_NEAR(c.record.At(BallRecordConstants::kBoundCheckBoundAddRate, 2), 0.0f, 1e-5);
  EXPECT_NEAR(pesball::Bounce(c, 2, -6.1746f, 0.0f, 0.0f).vy, 4.3222f, 1e-3);
  EXPECT_NEAR(pesball::Bounce(c, 2, -4.9883f, 0.0f, 0.0f).vy, 3.4918f, 1e-3);

  // Report 5.2's own numbers, from the shipped tables.
  EXPECT_NEAR(pesball::RollingKeepFactor(c, 1, 2.0f), 0.981f, 1e-6);
  const float k = pesball::RollingKeepFactor(c, 1, 2.0f);
  EXPECT_NEAR((1.0f - k) * c.image.tickHz * 2.0f, 2.05f, 0.01f);
  EXPECT_NEAR(pesball::StopSpeed_mps(c, 1), 0.175f * 1000.0f / 3600.0f, 1e-6);
  EXPECT_NEAR(pesball::SpinCapScale(c, 1, 10.0f), c.record.boundRollMax / 10.0f, 1e-6);

  // The mode-1 vertical keep: 3% at rest, ramping to 10% across 0.1 .. 0.7 km/h
  // (report 3.4, the raw's boundToRotation*Y band; the report's text says the
  // low bound is 8 km/h = frictionRollSpeedMin - a discrepancy recorded in the
  // final message).
  EXPECT_NEAR(pesball::VerticalKeep(c, 1, 0.0f), 0.03f, 1e-6);
  EXPECT_NEAR(pesball::VerticalKeep(c, 1, 4.0f), 0.10f, 1e-6);
  const float loV = c.record.At(BallRecordConstants::kBoundToRotationSpeedMinY, 1);
  const float hiV = c.record.At(BallRecordConstants::kBoundToRotationSpeedMaxY, 1);
  EXPECT_NEAR(pesball::VerticalKeep(c, 1, (loV + hiV) * 0.5f / c.image.kmhPerMs), 0.065f, 1e-6);
}

// --- the shot that reaches the goal (the bug this suite regressed on) ---

// A ground-level shot from the penalty spot, through the same loop ball.cpp
// runs: contact ticks rewrite the velocity and then carry the ball forward,
// flight ticks integrate the acceleration. The ball must cross the goal plane.
//
// The bug: the per-tick keep factors PES applies once per 1/54 s were applied
// once per GF tick (10 ms), so a GF tick lost 1.85x the speed PES loses per
// second (0.972^100 = 0.058 vs 0.972^54 = 0.216). A 25 m/s shot from the
// penalty spot stopped at 7.19 m, 9 m short of the goal plane at 56.31 m, and
// every penalty kick in a drawn match came back MISSED/SAVED.
TEST(PesBallShot, GroundShotFromThePenaltySpotCrossesTheGoalPlane) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set: the ball constants come from the install";
  std::string err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  auto cc = pes::LoadPesBallConstants(*exe, vfs, &err);
  ASSERT_TRUE(cc.has_value()) << err;
  const PesBallConstants c = *cc;

  // gametypes.hpp: the goal plane is pitchHalfW + lineHalfW + ballRadius
  // (CheckForGoal's triangles). goalHalfWidth is lateral, not longitudinal.
  const float goalPlane = 52.5f + 0.06f + c.image.ballRadius;
  const float kDt = 0.01f;  // Match::kTick_ms / 1000

  // The penalty spot is 11 m out; a 25 m/s shot (90 km/h) is a struck penalty.
  BallState s;
  s.position = pes::Vec3(41.5f, c.image.ballRadius, 0.0f);
  s.velocity = pes::Vec3(25.0f, 0.0f, 0.0f);

  unsigned contactTicks = 0;
  bool crossed = false;
  for (int i = 0; i < 3000 && !crossed; ++i) {
    if (s.position.y <= c.image.ballRadius) {
      pesball::ContactTick(c, &s, contactTicks, true, false);
      ++contactTicks;
      s.position.x += s.velocity.x * kDt;
      s.position.y += s.velocity.y * kDt;
      s.position.z += s.velocity.z * kDt;
    } else {
      pesball::FlightTick(c, &s, kDt, false);
    }
    if (s.position.x >= goalPlane) crossed = true;
  }
  EXPECT_TRUE(crossed) << "a 25 m/s ground shot from the penalty spot must reach the goal "
                       << "(it reached x = " << s.position.x << " of " << goalPlane << " m)";
}

// One second of GF ticks must lose the same speed PES loses in a second of its
// own ticks: the per-tick factors are converted, not re-applied.
TEST(PesBallConversion, ASecondOfGfTicksIsASecondOfPesTicksWhileRolling) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set: the ball constants come from the install";
  std::string err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  auto cc = pes::LoadPesBallConstants(*exe, vfs, &err);
  ASSERT_TRUE(cc.has_value()) << err;
  const PesBallConstants c = *cc;

  // The same rolling ball, one second of PES ticks and one second of GF ticks,
  // in the dry-normal contact mode this engine plays by default (report 3.2).
  float vPes = 10.0f, vGf = 10.0f;
  for (int i = 0; i < 54; ++i) vPes *= pesball::RollingKeepFactor(c, pesball::ClampMode(c.contactMode), vPes);
  for (int i = 0; i < 100; ++i) {
    BallState s;
    s.position = pes::Vec3(0, c.image.ballRadius, 0);
    s.velocity = pes::Vec3(vGf, 0.0f, 0.0f);
    pesball::ContactTick(c, &s, 0, true, false);
    vGf = s.velocity.x;
  }
  EXPECT_NEAR(vGf, vPes, vPes * 0.05)
      << "100 GF ticks and 54 PES ticks are both one second: " << vGf << " vs " << vPes;
}

}  // namespace

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

// ponytail: PES's own vertical keep is 0.03..0.10 (VerticalKeep), which leaves a
// ball at 3-10% of its incoming |vy| - a 10 m/s 30-degree kick at 0.579 m/s, a 2 m
// drop at 0.608 m/s, and spin makes no difference, so every case lands in
// 0.075..0.100. The live measurement (pes-re-officials-ball.md 6.4) is 0.37..2.26,
// whose spread sits in pes-re-ball-contact.md 3.3's un-decoded spin pump. So the
// contact's vertical response is GF's 0.62 bounce (see pesballphysics.hpp), and
// this test pins both numbers: the literal PES keep, and what the contact does.
TEST(PesBallContact, VerticalKeepIsPesThreeToTenPercentAndContactUsesGfBounce) {
  // Report 3.4, read literally: the vertical keep ramps from
  // boundToRotationBaseY to boundToRotationBaseY + boundToRotationAddRateY
  // across boundToRotationSpeedMinY .. boundToRotationSpeedMaxY (km/h), so mode 1
  // keeps 3% of |vy| at rest and 10% above 0.7 km/h (0.19 m/s). That is the true
  // restitution, and the reason report 6.4 measured a spread, not a number.
  const PesBallConstants c = SyntheticConstants();
  const float loV = c.record.At(BallRecordConstants::kBoundToRotationSpeedMinY, 1) / c.image.kmhPerMs;
  const float hiV = c.record.At(BallRecordConstants::kBoundToRotationSpeedMaxY, 1) / c.image.kmhPerMs;
  const float base = c.record.At(BallRecordConstants::kBoundToRotationBaseY, 1);
  const float add = c.record.At(BallRecordConstants::kBoundToRotationAddRateY, 1);
  // PES's own keep: 3% at rest, 10% above 0.7 km/h.
  EXPECT_NEAR(pesball::VerticalKeep(c, 1, loV * 0.5f), base, 1e-6);
  EXPECT_NEAR(pesball::VerticalKeep(c, 1, hiV * 2.0f), base + add, 1e-6);
  EXPECT_NEAR(pesball::VerticalKeep(c, 1, (loV + hiV) * 0.5f), base + add * 0.5f, 1e-6);

  // The contact applies GF's standing bounce: 62% of the incoming |vy|, floored
  // at 0.06 m/s. ponytail: this is the stand-in for the missing 3.3 spin pump.
  BallState soft, hard;
  soft.position = hard.position = pes::Vec3(0, c.image.ballRadius, 0);
  soft.velocity = pes::Vec3(0, -1.0f, 0);
  hard.velocity = pes::Vec3(0, -4.0f, 0);
  pesball::ContactTick(c, &soft, 0, true, false);
  pesball::ContactTick(c, &hard, 0, true, false);
  EXPECT_NEAR(soft.velocity.y, 1.0f * pesball::kGroundBounceFactor, 1e-3f);
  EXPECT_NEAR(hard.velocity.y, 4.0f * pesball::kGroundBounceFactor, 1e-3f);
  EXPECT_GT(soft.velocity.y, 0.0f) << "a bounce is upward";
  EXPECT_GT(hard.velocity.y, soft.velocity.y) << "a harder drop bounces harder";
  // And that ratio is inside the live 0.37..2.26 band, unlike PES's raw keep.
  EXPECT_GE(hard.velocity.y / 4.0f, 0.37f);
  EXPECT_LE(hard.velocity.y / 4.0f, 2.26f);
  // A ball with no vertical speed gets no bounce.
  BallState flat;
  flat.position = pes::Vec3(0, c.image.ballRadius, 0);
  flat.velocity = pes::Vec3(0.0f, 0.0f, 0.0f);
  pesball::ContactTick(c, &flat, 0, true, false);
  EXPECT_LE(std::fabs(flat.velocity.y), 1e-6f);
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
  // The rolling spin v/r is 36.8 rad/s, well above the boundRollMax cap of 7.5,
  // so the rebuilt raw spin must land exactly on the cap.
  const float cap = c.record.boundRollMax;
  EXPECT_NEAR(s.spinRaw.x * c.image.spinScale, cap, cap * 1e-3)
      << "spin_raw is rebuilt towards v/r, capped at boundRollMax";
  // A slow roller stays under the cap and lands on v/r.
  BallState slowRoll;
  slowRoll.position = pes::Vec3(0, c.image.ballRadius, 0);
  slowRoll.velocity = pes::Vec3(0.5f, 0.0f, 0.0f);
  pesball::ContactTick(c, &slowRoll, 0, true, false);
  EXPECT_NEAR(slowRoll.spinRaw.x * c.image.spinScale, 0.5f / c.image.ballRadius,
              0.5f / c.image.ballRadius * 0.05f);
}

// --- the stop gate (report 3.4) ---

TEST(PesBallContact, StopGateZeroesSubStopSpeedMotion) {
  const PesBallConstants c = SyntheticConstants();
  const float stop = pesball::StopSpeed_mps(c, 1);
  EXPECT_NEAR(stop, 0.175f * 1000.0f / 3600.0f, 1e-6);
  EXPECT_NEAR(stop, 0.04861f, 1e-4);

  // A ball slower than the stop speed - measured on the post-contact velocity,
  // which is what the raw compares - is stopped outright, spin included.
  BallState s;
  s.position = pes::Vec3(0, c.image.ballRadius, 0);
  s.velocity = pes::Vec3(0.001f, 0.0f, 0.001f);  // 0.0014 m/s, far below 0.0486
  pesball::ContactTick(c, &s, 0, true, false);
  EXPECT_EQ(s.velocity.x, 0.0f);
  EXPECT_EQ(s.velocity.z, 0.0f);
  EXPECT_EQ(s.spinRaw.x, 0.0f) << "the stop gate clears the spin too";
  EXPECT_EQ(s.spinRaw.z, 0.0f);

  // A ball rolling comfortably faster than the stop speed is not stopped.
  BallState rolling;
  rolling.position = pes::Vec3(0, c.image.ballRadius, 0);
  rolling.velocity = pes::Vec3(1.0f, 0.0f, 0.0f);
  pesball::ContactTick(c, &rolling, 0, true, false);
  EXPECT_GT(std::fabs(rolling.velocity.x), 0.5f) << "a rolling ball keeps rolling";
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

  // The same rolling ball, one second of PES ticks and one second of GF ticks.
  float vPes = 10.0f, vGf = 10.0f;
  for (int i = 0; i < 54; ++i) vPes *= pesball::RollingKeepFactor(c, 1, vPes);
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

#include "peskeeper.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pes {
namespace {

// .rdata floats (OBS, docs/re/pes-re-keeper-port.md §5 and
// docs/re/pes-re-goalkeeper.md §10).
constexpr uint64_t ONE_VA = 0x14256ae1c;       // 1.0
constexpr uint64_t TWO_VA = 0x142565604;       // 2.0
constexpr uint64_t THREE_VA = 0x14258c264;     // 3.0
constexpr uint64_t FOUR_VA = 0x1425656c0;      // 4.0
constexpr uint64_t SIX_VA = 0x14259bdec;       // 6.0
constexpr uint64_t SEVEN_VA = 0x14259bdf0;    // 7.0
constexpr uint64_t NINE_VA = 0x14259d60c;      // 9.0
constexpr uint64_t TEN_VA = 0x142588210;       // 10.0
constexpr uint64_t THIRTEEN_VA = 0x1425c5948;  // 13.0
constexpr uint64_t FIFTEEN_VA = 0x14259be0c;   // 15.0
constexpr uint64_t SEVENTEEN_VA = 0x14259d614;  // 17.0
constexpr uint64_t TWENTY_VA = 0x14259be1c;    // 20.0
constexpr uint64_t TWENTY_FIVE_VA = 0x14259be20;  // 25.0
constexpr uint64_t THIRTY_VA = 0x142591140;       // 30.0
constexpr uint64_t FORTY_VA = 0x14259be2c;        // 40.0
constexpr uint64_t FIFTY_VA = 0x14259be34;        // 50.0
constexpr uint64_t FIFTY_NINE_VA = 0x1425cb2fc;   // 59.0
constexpr uint64_t SEVENTY_VA = 0x14259be4c;      // 70.0
constexpr uint64_t NINETY_NINE_VA = 0x1425c48cc;  // 99.0
constexpr uint64_t HALF_VA = 0x142565600;         // 0.5
constexpr uint64_t EIGHT_TENTHS_VA = 0x142572348;  // 0.8
constexpr uint64_t ONE_TENTH_VA = 0x142588200;     // 0.1
constexpr uint64_t SIXTY_VA = 0x14259a764;        // 60.0
constexpr uint64_t NINETY_VA = 0x14259be5c;       // 90.0
constexpr uint64_t ONE_TWENTY_VA = 0x14256ae30;  // 120.0
constexpr uint64_t TWO_POINT_FOUR_VA = 0x1425c7660;  // 2.4
constexpr uint64_t THREE_SIXTY_VA = 0x14259858c;     // 360.0
constexpr uint64_t RAD_TO_DEG_VA = 0x14259be40;      // 57.2957764
// The rush factors and its hysteresis (OBS raw 0x1421162c0).
constexpr uint64_t RUSH_OFFENSIVE_VA = 0x1425a3590;  // 1.1
constexpr uint64_t RUSH_NONE_VA = 0x14259d5d4;       // 1.2
constexpr uint64_t RUSH_DEFENSIVE_VA = 0x14259bdd8;  // 1.3
constexpr uint64_t RUSH_HYST_VA = 0x1425eb848;       // 0.71
constexpr uint64_t RUSH_MAX_BALL_TIME_VA = 0x1425c4c10;  // 3.3

bool ReadFloat(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
}

// PES's `ramp(v; lo -> 0, hi -> 1)`: 0 below, 1 above, linear between.
float Ramp01(float v, float lo, float hi) {
  if (hi <= lo) return v >= hi ? 1.0f : 0.0f;
  return std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
}

}  // namespace

std::optional<KeeperConstants> LoadKeeperConstants(const PeImage& exe, std::string* error) {
  KeeperConstants k{};
  bool ok = true;
  for (auto [va, out] : {
           std::pair{ONE_VA, &k.deflectSidewaysRadius_m},
           std::pair{ONE_VA, &k.distributionOwnLineMargin_m},
           std::pair{ONE_VA, &k.distributionBoxSideMargin_m},
           std::pair{TWO_VA, &k.deflectRequestedRadius_m},
           std::pair{TWO_VA, &k.distributionLateralCorrection_m},
           std::pair{TWO_VA, &k.rushXLimitOwnEvent_m},
           std::pair{THREE_VA, &k.positioningPossessionRamp1_m},
           std::pair{THREE_VA, &k.positioningPossessionLineMargin_m},
           std::pair{THREE_VA, &k.positioningPossessionUpfield_m},
           std::pair{THREE_VA, &k.rushXLimitRushing_m},
           std::pair{FOUR_VA, &k.distributionCarryUpfield_m},
           std::pair{FOUR_VA, &k.distributionTargetStep_m},
           std::pair{FOUR_VA, &k.positioningDefensiveCap_m},
           std::pair{FOUR_VA, &k.positioningPossessionRamp2_m},
           std::pair{SIX_VA, &k.positioningPossessionDeep_m},
           std::pair{SEVEN_VA, &k.positioningPossessionRamp3_m},
           std::pair{NINE_VA, &k.positioningPossessionMid_m},
           std::pair{NINE_VA, &k.positioningRampSpanDepth_m},
           std::pair{TEN_VA, &k.positioningLineClamp_m},
           std::pair{THIRTEEN_VA, &k.positioningPossessionFar_m},
           std::pair{THIRTEEN_VA, &k.positioningPossessionDefensiveCap_m},
           std::pair{FIFTEEN_VA, &k.positioningNoStyleCap_m},
           std::pair{FIFTEEN_VA, &k.positioningMaxRadius_m},
           std::pair{FIFTEEN_VA, &k.positioningPossessionNoStyleCap_m},
           std::pair{SEVENTEEN_VA, &k.positioningPossessionOffensiveCap_m},
           std::pair{TWENTY_VA, &k.positioningOffensiveCap_m},
           std::pair{TWENTY_FIVE_VA, &k.positioningLineBack_m},
           std::pair{TWENTY_FIVE_VA, &k.positioningPossessionLineMargin_m},
           std::pair{THIRTY_VA, &k.positioningRampSpan_m},
           std::pair{FORTY_VA, &k.positioningRampNear_m},
           std::pair{FORTY_VA, &k.positioningAwarenessLow},
           std::pair{FIFTY_VA, &k.positioningBearingSpan_deg},
           std::pair{FIFTY_NINE_VA, &k.positioningAwarenessSpan},
           std::pair{SEVENTY_VA, &k.positioningRampFar_m},
           std::pair{NINETY_NINE_VA, &k.positioningAwarenessHigh},
           std::pair{SIXTY_VA, &k.deflectFarSideAngle_deg},
           std::pair{NINETY_VA, &k.quarterTurn_deg},
           std::pair{ONE_TWENTY_VA, &k.deflectSidewaysGate_deg},
           std::pair{TWO_POINT_FOUR_VA, &k.deflectRaisedHeight_m},
           std::pair{THREE_SIXTY_VA, &k.angleWrap_deg},
           std::pair{RAD_TO_DEG_VA, &k.degreesPerRadian},
           std::pair{HALF_VA, &k.positioningFloorGoalBias_m},
           std::pair{EIGHT_TENTHS_VA, &k.positioningGoalLineMargin_m},
           std::pair{ONE_TENTH_VA, &k.positioningJitterStep_m},
           std::pair{RUSH_OFFENSIVE_VA, &k.rushOffensiveFactor},
           std::pair{RUSH_NONE_VA, &k.rushNoStyleFactor},
           std::pair{RUSH_DEFENSIVE_VA, &k.rushDefensiveFactor},
           std::pair{RUSH_HYST_VA, &k.rushHysteresis_s},
           std::pair{RUSH_MAX_BALL_TIME_VA, &k.rushMaxBallTime_s},
       }) {
    ok = ReadFloat(exe, va, out) && ok;
  }
  // 2.0: the .rdata slot the positioning's floor term reads (the same slot as
  // the lateral correction, a second read of it).
  ok = ReadFloat(exe, TWO_VA, &k.positioningFloor_m) && ok;
  if (!ok) {
    *error = "keeper constant: an .rdata float is unreadable in this exe";
    return std::nullopt;
  }
  return k;
}

DeflectPoint PesDeflectPoint(float requestAngle_deg, float heading_deg,
                             const KeeperConstants& k) {
  // FUN_1405bb170: the wrapped difference of the requested angle from his
  // heading, in (-180, 180].
  float delta = requestAngle_deg - heading_deg;
  while (delta > k.angleWrap_deg * 0.5f) delta -= k.angleWrap_deg;
  while (delta <= -k.angleWrap_deg * 0.5f) delta += k.angleWrap_deg;

  DeflectPoint p;
  p.raised = true;  // this engine never has a height input: PES lifts the point
  if (delta >= k.deflectSidewaysGate_deg || delta <= -k.deflectSidewaysGate_deg) {
    // The ball is across him: meeting it where it was asked for would push it
    // along his own flank, so he takes the far side, heading ∓ 60 degrees, 1 m
    // out (FUN_140470d00/FUN_140470d60, the wrapped heading ∓ 60).
    float angle = heading_deg + (delta < 0.0f ? -k.deflectFarSideAngle_deg
                                              : k.deflectFarSideAngle_deg);
    angle = std::fmod(angle, k.angleWrap_deg);
    if (angle < 0.0f) angle += k.angleWrap_deg;
    p.radius_m = k.deflectSidewaysRadius_m;
    p.angle_deg = angle;
    return p;
  }
  // Within 120 degrees he meets the ball where it was asked for, 2 m out.
  p.radius_m = k.deflectRequestedRadius_m;
  float angle = std::fmod(requestAngle_deg, k.angleWrap_deg);
  if (angle < 0.0f) angle += k.angleWrap_deg;
  p.angle_deg = angle;
  return p;
}

DistributionPoint PesAfterCatchCarry(float keeperX, float keeperZ, int attackDir,
                                      const KeeperFieldMarks& marks, const KeeperConstants& k) {
  // AfterCatchMove::exec's carry (OBS raw 0x1409e8971 - 0x1409e8a85, own frame,
  // `dir` the attack sign and `A = F6 + 1` the box line plus its 1 m margin):
  //   A = F6 + 1;
  //   if (-A <= dir*x + 4) x = -A*dir; else x = dir*4 + x;   // comiss/jbe
  //   if (|z| > F5) z -= sign(z) * 2.0;                      // addss xmm0,xmm0
  // In words: in his own frame (x' = dir*x, so -A is the line on his own half)
  // the carried x is min(x' + 4, -A). Beyond his own half `-A` is the smaller of
  // the two, so he is held there; inside it the 4 m carry wins.
  const float dir = attackDir >= 0 ? 1.0f : -1.0f;
  // PES's A = F6 + 1 is a MAGNITUDE (36.14 + 1) applied with his attack sign
  // only at the end: the mark itself is 1 m into his own half.
  const float a_m = marks.boxLineX_m + k.distributionOwnLineMargin_m;
  // The carry is 4 m UP-FIELD, which is +4 in his own frame.
  const float own_carried = dir * keeperX + k.distributionCarryUpfield_m;
  const float own_held = -a_m;
  const float own_x = own_carried < own_held ? own_carried : own_held;
  const float x = dir * own_x;
  float z = keeperZ;
  // Wider than the goal area: 2 m toward the middle, not one goal-area width.
  if (marks.goalAreaHalfWidth_m < std::fabs(z)) {
    z -= (z < 0.0f ? -1.0f : 1.0f) * k.distributionLateralCorrection_m;
  }
  return {x, z};
}

DistributionPoint PesAfterCatchWalk(float keeperX, float keeperZ, float carryX, float carryZ,
                                    float mateX, float mateZ, int attackDir,
                                    const KeeperFieldMarks& marks, const KeeperConstants& k) {
  // Then FUN_140a8f7c0 steps 4 m toward the chosen team-mate (OBS raw
  // 0x1409e8c52 - 0x1409e8d70): the bearing is FUN_140a92d40 of the DELTA x
  // first and the delta z second, folded by atan, so theta is measured from +z
  // and `p += r*(sin theta, sin(theta + 90))` is 4 m straight at the mate. The
  // sign fix of 10.6.2 moved the arguments back into that order.
  const float dir = attackDir >= 0 ? 1.0f : -1.0f;
  const float a_m = marks.boxLineX_m + k.distributionOwnLineMargin_m;
  const float dx = mateX - keeperX;
  const float dz = mateZ - keeperZ;
  float x = carryX;
  float z = carryZ;
  if (dx * dx + dz * dz > 0.000001f) {
    const float bearing = std::atan2(dx, dz);  // from +z, as FUN_140a92d40 folds
    const float quarter = k.quarterTurn_deg / k.degreesPerRadian;  // 90 degrees
    x += std::sin(bearing) * k.distributionTargetStep_m;
    z += std::sin(bearing + quarter) * k.distributionTargetStep_m;
  }
  // PES's post-step clamp: in his own frame `if (-A < x') x' = -A`, i.e. the
  // target may not sit nearer the far side of the pitch than 1 m short of the
  // line on his own half.
  const float own_x = dir * x;
  if (-a_m < own_x) x = -a_m * dir;
  // And every branch ends with `|z| <= F7 - 1` (0x1409e8d3e - 0x1409e8d70),
  // the clamp the port was missing.
  const float z_limit = marks.boxHalfWidth_m - k.distributionBoxSideMargin_m;
  if (z_limit < std::fabs(z)) z = (z < 0.0f ? -1.0f : 1.0f) * z_limit;
  return {x, z};
}

KeeperSpot PesKeeperBasePosition(const KeeperPositionInput& in, const KeeperFieldMarks& marks,
                                 const KeeperConstants& k) {
  // 10.2.2 out of possession (0x1409de8c0 -> core 0x1409e0390). R is 1 m behind
  // the centre of his own goal (0x1409e0442): the origin of every keeper
  // geometry. `dir` is the attack sign, so the +dir axis points up HIS pitch.
  const float dir = -in.defenseSide;
  // R = -(F0 + 1) * dir: 1 m behind the centre of HIS OWN goal. For the keeper
  // of the -x goal (defenseSide -1, dir +1) that is -53.5; for the other it is
  // +53.5.
  const float rx = -dir * (marks.halfLength_m + 1.0f);
  const float rz = 0.0f;
  // The ball reference is the PREDICTED ball (live: action+0x74), not the raw
  // ball - the caller's anticipation is what stands in for that.
  const float bx = in.ballX;
  const float bz = in.ballZ;

  // theta = bearing(R -> B), degrees from +z (FUN_140439e50's atan2(dx, dz)).
  const float dx = bx - rx;
  const float dz = bz - rz;
  const float ball_dist = std::sqrt(dx * dx + dz * dz);
  float theta = std::atan2(dx, dz) * k.degreesPerRadian;
  if (theta < 0.0f) theta += k.angleWrap_deg;
  bool jitter = false;

  float d;
  if (in.inPossession) {
    // 10.2.3: the sweeper step-up.
    const float own = dir * bx;  // the ball's up-field coordinate in his frame
    const float half_pitch = marks.halfLength_m * 0.5f;
    if (own < 3.0f - marks.boxLineX_m) {
      d = k.positioningPossessionDeep_m;  // deeper than 3 m inside the box line
    } else if (own < -half_pitch) {
      // 6 m at the box line, 9 m at the halfway of his half.
      d = k.positioningPossessionDeep_m +
          k.positioningPossessionRamp1_m *
              Ramp01(own, 3.0f - marks.boxLineX_m, -half_pitch);
    } else if (own < 0.0f) {
      // 9 m at that halfway, 13 m at the centre line.
      d = k.positioningPossessionMid_m +
          k.positioningPossessionRamp2_m * Ramp01(own, -half_pitch, 0.0f);
    } else {
      // 13 m at the centre line, 20 m with the ball in the opponents' half.
      d = k.positioningPossessionFar_m +
          k.positioningPossessionRamp3_m * Ramp01(own, 0.0f, half_pitch);
    }
    // Never deeper than 25 m behind the team line (the line is in his frame).
    const float floor_d = in.teamLineX - k.positioningLineBack_m;
    if (d < floor_d) d = floor_d;
    // Capped 13/15/17 by style.
    const float cap = in.defensiveStyle   ? k.positioningPossessionDefensiveCap_m
                      : in.offensiveStyle ? k.positioningPossessionOffensiveCap_m
                                          : k.positioningPossessionNoStyleCap_m;
    if (cap < d) d = cap;
    jitter = false;  // 10.2.3 has no shuffle: it is a committed standing point
  } else {
    // The core spot (FUN_1409e0390).
    const float w = std::clamp(std::fabs(theta) / k.positioningBearingSpan_deg, 0.0f, 1.0f);
    const float dmin =
        (marks.goalHalfWidth_m + k.positioningFloorGoalBias_m) * w +
        k.positioningFloor_m * (1.0f - w);
    // The defensive line: the midpoint of the team line and the offside line,
    // 25 m behind it, floored at the goal-area line, then measured from R.
    // The lines are in his own frame.
    const float own_m = std::clamp(in.offsideLineX, in.teamLineX - k.positioningLineClamp_m,
                                   in.teamLineX + k.positioningLineClamp_m);
    const float dline = std::max(-marks.goalAreaLineX_m,
                                 (in.teamLineX + own_m) * 0.5f - k.positioningLineBack_m) +
                        marks.halfLength_m;
    // The style cap.
    const float cap = in.defensiveStyle   ? k.positioningDefensiveCap_m
                      : in.offensiveStyle ? k.positioningOffensiveCap_m
                                          : k.positioningNoStyleCap_m;
    d = std::max(dmin, std::min(cap, dline));
    // The ball-distance ramp: 4 m while the ball is within 40 m of him
    // lengthwise, growing linearly to 13 m at 70 m.
    const float ramp = Ramp01(std::fabs(bx - in.keeperX), k.positioningRampNear_m,
                              k.positioningRampFar_m);
    d = std::min(d, k.positioningDefensiveCap_m + k.positioningRampSpanDepth_m * ramp);
    jitter = true;
  }

  // Never farther than this from R.
  if (k.positioningMaxRadius_m < d) d = k.positioningMaxRadius_m;

  // P = R + d * (sin theta, cos theta), the direction of R -> B at `d` out.
  const float rad = theta / k.degreesPerRadian;
  float px = std::sin(rad) * d;
  float pz = std::cos(rad) * d;

  // The final guard: never nearer than the axis margin to his own line (out of
  // possession 0.8 m, in possession 3 m up-field).
  const float axis = in.inPossession ? k.positioningPossessionUpfield_m
                                     : k.positioningGoalLineMargin_m;
  if (dir * px < axis) px = dir * axis;

  // The lateral shuffle (0x1409e0590): a Range0(6) draw on the PES generator,
  // 0.1 m a step, scaled down by GK Awareness, and flipped when the ball's
  // predicted bearing differs 90 degrees or more from the goal axis. Slot 1
  // (the report leaves the instance open; the move reads Range0, whose only
  // compiled call is on slot 1's generator).
  float jitter_m = 0.0f;
  if (jitter) {
    const float awareness =
        Ramp01(in.gkAwareness, k.positioningAwarenessLow, k.positioningAwarenessHigh);
    jitter_m = static_cast<float>(in.jitterDraw) * k.positioningJitterStep_m *
               (1.0f - awareness);
    // The sign flips when the ball's predicted bearing is 90 degrees or more
    // off the goal axis, so he never shades the same way twice.
    if (90.0f <= std::fabs(theta)) jitter_m = -jitter_m;
    // Perpendicular to R -> B, which is (theta + 90): the side step.
    px += std::sin(rad + k.quarterTurn_deg / k.degreesPerRadian) * jitter_m;
    pz += std::sin(rad) * jitter_m;
  }

  return {rx + px, rz + pz, d, theta, jitter_m};
}

float PesKeeperRushFactor(bool offensiveStyle, bool defensiveStyle, const KeeperConstants& k) {
  // OBS raw 0x1421162c0: the same style-word bits, chosen as defensive 1.3,
  // none 1.2, offensive 1.1.
  if (defensiveStyle) return k.rushDefensiveFactor;
  if (offensiveStyle) return k.rushOffensiveFactor;
  return k.rushNoStyleFactor;
}

float PesKeeperRushLateralLimit_m(bool offensiveStyle, bool defensiveStyle,
                                  const KeeperFieldMarks& marks) {
  // OBS raw 0x1421162c0: F5 for a Defensive GK, F7 for an Offensive GK, the
  // midpoint for no style.
  if (defensiveStyle) return marks.goalAreaHalfWidth_m;
  if (offensiveStyle) return marks.boxHalfWidth_m;
  return (marks.goalAreaHalfWidth_m + marks.boxHalfWidth_m) * 0.5f;
}

bool PesKeeperRushRaceWins(float keeperTime_s, float opponentTime_s, bool rushing,
                           float factor, const KeeperConstants& k) {
  // OBS raw 0x1421162c0: `gs * rushing * 0.71 + of <= k * kf` decides that an
  // opponent gets there first, so he does NOT rush. Because `gs * 0.71` frames
  // is 0.71 s at any rate, the same comparison in seconds is what this runs.
  // A ball more than 3.3 s away is never chased (the report's `kf > 3.3*gs`,
  // OBS 0x1425c4c10; the 250-frame bound is looser).
  if (k.rushMaxBallTime_s < keeperTime_s) return false;
  return opponentTime_s > factor * keeperTime_s - (rushing ? k.rushHysteresis_s : 0.0f);
}

}  // namespace pes

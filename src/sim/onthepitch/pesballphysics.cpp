#include "onthepitch/pesballphysics.hpp"

#include <cmath>

namespace pesball {
namespace {

// The report's clamp01: saturating, as the `comiss`+`jb`/`cmova` pattern is.
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The report's clamped ramp, literal. The raw compares the low bound first:
// `if (lo < v) { if (v < hi) interp else 1 } else 0`, so a non-positive span
// saturates rather than dividing by zero.
inline float Ramp(float v, float lo, float hi) {
  if (!(lo < v)) return 0.0f;
  return v < hi ? (v - lo) / (hi - lo) : 1.0f;
}

// km/h from m/s, with the image's own 3.6 (.rdata 0x14259ce38).
inline float ToKmh(const pes::PesBallConstants& c, float mps) { return mps * c.image.kmhPerMs; }

// 100.0 (.rdata 0x14253ee30): the per-cent divisor of the add-rate fields.
constexpr float kPercent = 100.0f;
// 0.01 s: this engine's tick, and also the |vy| blend divisor
// (.rdata 0x14256ae18) and the km/h->m/s of the stop gate's inner form.
constexpr float kStopBlend_s = 0.01f;

}  // namespace

int ClampMode(int mode) {
  // Report 0.3: `cmp mode, 5; cmova mode, 1` - anything above 5 becomes 1.
  return mode > 5 ? 1 : mode;
}

float RollingKeepFactor(const pes::PesBallConstants& c, int mode, float speed_mps) {
  // Report 2.4, literal. V = 3.6*|v| (km/h). Above frictionRollSpeedMin:
  //   t = clamp01((V - U)/(H - U))
  //   K = t*frictionRollRateMax + (1-t)*frictionRollRateMin
  // at or below it:
  //   L = frictionRollSpeedStop
  //   t = clamp01((V - L)/(U - L))
  //   K = t*frictionRollRateMin + (1-t)*frictionRollRateStop
  const pes::BallRecordConstants& r = c.record;
  const float V = ToKmh(c, speed_mps);
  const float U = r.At(pes::BallRecordConstants::kFrictionRollSpeedMin, mode);
  if (V > U) {
    const float H = r.At(pes::BallRecordConstants::kFrictionRollSpeedMax, mode);
    const float t = Clamp01((V - U) / (H - U));
    return t * r.At(pes::BallRecordConstants::kFrictionRollRateMax, mode) +
           (1.0f - t) * r.At(pes::BallRecordConstants::kFrictionRollRateMin, mode);
  }
  const float L = r.At(pes::BallRecordConstants::kFrictionRollSpeedStop, mode);
  const float t = Clamp01((V - L) / (U - L));
  return t * r.At(pes::BallRecordConstants::kFrictionRollRateMin, mode) +
         (1.0f - t) * r.At(pes::BallRecordConstants::kFrictionRollRateStop, mode);
}

float FrictionMultiplier(const pes::PesBallConstants& c, int mode, float speed_mps, float vy,
                         float spinRad, float* baseA) {
  // Report 3.1, literal. Three clamped ramps over |spin| (rad/s) and |vy| (km/h):
  //   s1 = clamp01((|w| - boundFrictionSpeedMin)/(boundFrictionSpeedMax - boundFrictionSpeedMin))
  //   s2 = clamp01((|vy|*3.6 - boundBoundSpeedMin)/(boundBoundSpeedMax - boundBoundSpeedMin))
  //   s3 = clamp01((|vy|*3.6 - boundRotationSpeedMin)/(boundRotationSpeedMax - boundRotationSpeedMin))
  //   K *= 1 - boundCheckFrictionAddRate[mode]/100 * s1
  //   A  = boundBoundRate[mode] * (1 + boundCheckBoundAddRate[mode]/100 * s3)
  const pes::BallRecordConstants& r = c.record;
  const float vyKmh = ToKmh(c, std::fabs(vy));
  const float s1 = Ramp(spinRad, r.boundFrictionSpeedMin, r.boundFrictionSpeedMax);
  const float s2 = Ramp(vyKmh, r.boundBoundSpeedMin, r.boundBoundSpeedMax);
  const float s3 = Ramp(vyKmh, r.boundRotationSpeedMin, r.boundRotationSpeedMax);
  if (baseA)
    *baseA = r.At(pes::BallRecordConstants::kBoundRate, mode) *
             (1.0f + r.At(pes::BallRecordConstants::kBoundCheckBoundAddRate, mode) / kPercent * s2);
  return 1.0f -
         r.At(pes::BallRecordConstants::kBoundCheckFrictionAddRate, mode) / kPercent * s1;
}

float VerticalKeep(const pes::PesBallConstants& c, int mode, float vy) {
  // Report 3.4, the raw read literally:
  //   X = 3.6*|vy| (km/h)
  //   t = clamp01((X - boundToRotationSpeedMinY[mode]) /
  //               (boundToRotationSpeedMaxY[mode] - boundToRotationSpeedMinY[mode]))
  //   vy_keep = boundToRotationBaseY[mode] + t*boundToRotationAddRateY[mode]
  // Mode 1: base 0.03, +0.07 over the band 0.1 .. 0.7 km/h, so the keep is 3% at
  // rest, 10% above 0.19 m/s of vertical speed - the true restitution, which is
  // why report 6.4 saw a spread instead of a single coefficient.
  //
  // The report's text calls the low bound "frictionRollSpeedMin = 8 km/h"; the
  // raw at 0x1408770e6 reads BallBase+0x11c + mode*4, which is
  // boundToRotationSpeedMinY (0.1 for mode 1), and 0x1408770f3 reads
  // BallBase+0xec + mode*4 = boundToRotationSpeedMaxY (0.7). The raw wins.
  const pes::BallRecordConstants& r = c.record;
  const float X = ToKmh(c, std::fabs(vy));
  const float lo = r.At(pes::BallRecordConstants::kBoundToRotationSpeedMinY, mode);
  const float hi = r.At(pes::BallRecordConstants::kBoundToRotationSpeedMaxY, mode);
  const float t = Ramp(X, lo, hi);
  return r.At(pes::BallRecordConstants::kBoundToRotationBaseY, mode) +
         t * r.At(pes::BallRecordConstants::kBoundToRotationAddRateY, mode);
}

float StopSpeed_mps(const pes::PesBallConstants& c, int mode) {
  // Report 3.4: `stopSpeed * 1000 / 3600`, i.e. 0.175 km/h -> 0.04861 m/s.
  (void)mode;
  return c.record.stopSpeed * c.image.milli / c.image.secondsPerHour;
}

float SpinCapScale(const pes::PesBallConstants& c, int mode, float spinRad) {
  // Report 3.4: |spin| is capped at boundRollMax rad/s on every contact tick; a
  // ball already below the cap is untouched.
  (void)mode;
  const float cap = c.record.boundRollMax;
  if (spinRad <= cap) return 1.0f;
  return cap / spinRad;
}

GrounderResult GrounderKick(const pes::PesBallConstants& c, int mode, float gateX, float fracZ,
                            unsigned tickIndex, float horizontalSpeed_mps, float vy) {
  // Report 2.5, the guard chain verbatim:
  //   H = 3.6*|v_xz| >= grounderSpeed (km/h)
  //   old vy != 0 and |v_xz/vy| > grounderCTan (shallow shots only)
  //   int(|x|*1000) mod frictionRollSpeedStop[mode][3] == 0
  // then the driven grounder: the horizontal speed is trimmed by
  // grounderBoundYAdd*(1-f) and K is multiplied by (f + grounderFrictionRate),
  // with f = frac(|x|*1000)*0.3.
  //
  // PES reads the *x* coordinate for the mod-gate (0x1408769c7, [rsp+0x48] = old
  // x) and the *z* coordinate for the fraction (0x1408769e6, [rsp+0x50] = old z).
  // GF's ball is in GF space, so the caller passes the coordinate it calls
  // longitudinal; both are the same role in their own frame.
  //
  // Mode 1 ships 0 as that array's [3] element, so a signed idiv would fault;
  // PES never reaches it for mode 1 because its guards never all pass. This
  // engine keeps the gate but treats a zero divisor as "not taken", which is
  // the only outcome PES itself produces.
  GrounderResult out;
  const pes::BallRecordConstants& r = c.record;
  const float H = ToKmh(c, horizontalSpeed_mps);
  if (!(H >= r.grounderSpeed)) return out;
  if (vy == 0.0f) return out;
  if (!(std::fabs(horizontalSpeed_mps / vy) > r.grounderCTan)) return out;
  const int divisor = r.AtInt(pes::BallRecordConstants::kGounnderprob, mode);
  if (divisor == pes::kGrounderTickGateNever) return out;
  if (static_cast<int>(static_cast<unsigned>(std::fabs(gateX) * c.image.milli)) % divisor != 0)
    return out;
  // 0.3 is .rdata 0x142590d98.
  constexpr float kGrounderFracScale = 0.3f;
  const float scaled = std::fabs(fracZ) * c.image.milli;
  const float f = (scaled - static_cast<float>(static_cast<int>(scaled))) * kGrounderFracScale;
  out.applied = true;
  out.keepMultiplier = f + r.grounderFrictionRate;
  out.speedTrim_mps = horizontalSpeed_mps * r.grounderBoundYAdd * (1.0f - f);
  (void)tickIndex;
  return out;
}

bool IsResting(const pes::PesBallConstants& c, const BallState& s) {
  // Report 2.1: y <= r AND |spin| == 0 AND horizontal |v| == 0 skips the flight
  // function entirely (PES's r15b = 0). GF's ball carries a zero spin when
  // nothing has set one, so this is the ordinary stopped-ball path.
  const float spinSq =
      s.spinRaw.x * s.spinRaw.x + s.spinRaw.y * s.spinRaw.y + s.spinRaw.z * s.spinRaw.z;
  const float hSq = s.velocity.x * s.velocity.x + s.velocity.z * s.velocity.z;
  return s.position.y <= c.image.ballRadius && spinSq <= 0.0f && hSq <= 0.0f;
}

pes::Vec3 FlightAcceleration(const pes::PesBallConstants& c, const BallState& s, float spinRadX,
                             float spinRadY, float spinRadZ, bool noMagnus) {
  // Report 6.3 (FUN_140874470), literal and validated live to a median error of
  // 0.0013 m/s^2:
  //   s2 = vx^2 + vz^2
  //   if s2 <= dragCrisisSquared: gravity only (also no Magnus)
  //   s = sqrt(s2); v1 = dragSpeedMax/3.6; v2 = dragSpeedMin/3.6
  //   s' = (s <= v1) ? (s > v2 ? 2*v2 - s : s) : (2*(v2 - v1) + s)   // drag crisis
  //   k = airRegistNormal * -0.5 * rho * A * s'
  //   a = k*v + (0, -g, 0)                       // drag on all three
  //   if !noMagnus:
  //     m = min(s, nonSpinTopMax)                // the non-wobble branch
  //     a += m * magnusRate * pi^2 * r^2 * r * cross(w, v)
  const float s2 = s.velocity.x * s.velocity.x + s.velocity.z * s.velocity.z;
  pes::Vec3 a(0.0f, -c.image.gravity, 0.0f);
  if (s2 <= c.image.dragCrisisSquared) return a;
  const float speed = std::sqrt(s2);
  const float v1 = c.record.dragSpeedMax / c.image.kmhPerMs;
  const float v2 = c.record.dragSpeedMin / c.image.kmhPerMs;
  float shaped;
  if (speed <= v1)
    shaped = (speed > v2) ? (2.0f * v2 - speed) : speed;
  else
    shaped = 2.0f * (v2 - v1) + speed;
  const float k = c.record.airRegistNormal * c.image.half * c.image.airDensity * c.image.ballArea *
                  shaped;
  a.x = k * s.velocity.x;
  a.y = k * s.velocity.y - c.image.gravity;
  a.z = k * s.velocity.z;
  if (noMagnus) return a;
  const float m = speed < c.image.nonSpinTopMax ? speed : c.image.nonSpinTopMax;
  const float scale = m * c.record.magnusRate * c.image.pi * c.image.pi *
                      c.image.ballRadiusSquared * c.image.ballRadius;
  // cross(w, v) = (wy*vz - wz*vy, wz*vx - wx*vz, wx*vy - wy*vx)
  a.x += (spinRadY * s.velocity.z - spinRadZ * s.velocity.y) * scale;
  a.y += (spinRadZ * s.velocity.x - spinRadX * s.velocity.z) * scale;
  a.z += (spinRadX * s.velocity.y - spinRadY * s.velocity.x) * scale;
  return a;
}

void FlightTick(const pes::PesBallConstants& c, BallState* state, float dt_s, bool noMagnus) {
  // Report 6.1: semi-implicit Euler, v += a*dt first, then p += v*dt.
  const pes::Vec3 a = FlightAcceleration(
      c, *state, state->spinRaw.x * c.image.spinScale, state->spinRaw.y * c.image.spinScale,
      state->spinRaw.z * c.image.spinScale, noMagnus);
  state->velocity.x += a.x * dt_s;
  state->velocity.y += a.y * dt_s;
  state->velocity.z += a.z * dt_s;
  state->position.x += state->velocity.x * dt_s;
  state->position.y += state->velocity.y * dt_s;
  state->position.z += state->velocity.z * dt_s;
}

void ContactTick(const pes::PesBallConstants& c, BallState* state, unsigned tickIndex, bool airborne,
                 bool quatIntegrate) {
  // The contact mode. ponytail: report 7.2 - the per-match mode byte's writer
  // (weather/pitch condition -> mode 0..5) was not found, so this engine plays
  // mode 1 (normal ground), which is what PES showed live in both observed
  // matches. The whole model is mode-parameterised, so filling the mode in is a
  // one-line change once the writer is known.
  const int mode = 1;

  // Report 2.3/2.7: the snap and the wobble-flag clear, and NO position
  // integration on a contact tick - y stays r and x,z stay where the flight
  // step of this tick put them.
  const float r = c.image.ballRadius;
  state->position.y = r;
  const float vx = state->velocity.x, vy = state->velocity.y, vz = state->velocity.z;
  const float horizontal = std::sqrt(vx * vx + vz * vz);
  const float spinRad =
      std::sqrt(state->spinRaw.x * state->spinRaw.x + state->spinRaw.y * state->spinRaw.y +
                state->spinRaw.z * state->spinRaw.z) *
      c.image.spinScale;

  // Report 2.4: the per-tick keep K, over the pre-contact 3-D speed. PES applies
  // it once per 1/54 s; this engine ticks every 10 ms, so the factor is raised
  // to <func>KeepFactorPerGfTick</func> before it is used - without that one
  // GF tick lost 1.85x the speed PES loses per second (0.972^100 vs 0.972^54),
  // and a 25 m/s shot stopped 8 m short of the goal instead of passing it.
  const float speed3d = std::sqrt(horizontal * horizontal + vy * vy);
  float keep = KeepFactorPerGfTick(RollingKeepFactor(c, mode, speed3d));
  float speed = horizontal;

  // Report 2.5: the grounder kick trims the horizontal speed and K. The trim is
  // a speed, not a factor, so it is used as-is; the keep product is a per-tick
  // factor chain and is converted as one.
  const GrounderResult grounder =
      GrounderKick(c, mode, state->position.x, state->position.z, tickIndex, horizontal, vy);
  if (grounder.applied) {
    keep *= KeepFactorPerGfTick(grounder.keepMultiplier);
    speed = std::max(0.0f, horizontal - grounder.speedTrim_mps);
  }

  // Report 3.1: the tangential friction multiplier F (= A) and the K term. F is
  // the per-tick ratio PES multiplies into K, so it converts with the same
  // exponent.
  float baseA = 0.0f;
  const float friction = FrictionMultiplier(c, mode, speed3d, vy, spinRad, &baseA);
  keep *= KeepFactorPerGfTick(friction);

  // Report 3.4, the vertical response, read literally from
  // 0x1408770e6..0x14087718f and the assembly at 0x14087722c:
  //   X = 3.6*|vy| (km/h)
  //   t = clamp01((X - boundToRotationSpeedMinY[mode]) / (boundToRotationSpeedMaxY[mode] - ...))
  //   vy_keep = t*boundToRotationAddRateY[mode] + boundToRotationBaseY[mode]
  // The velocity's vertical is that vy_keep times the incoming vy (the ratio
  // the report calls the true restitution: 3% at rest rising to 10%), and the
  // |vy|/0.01 blend applies to the raw-spin rebuild, not to it.
  // ponytail: see pesballphysics.hpp - PES's 0.03..0.10 vertical keep leaves the
  // ball 0.075..0.10 of its incoming |vy|, against a live band of 0.37..2.26; the
  // missing spread is in report 3.3's un-decoded spin pump. GF's bounce stands in
  // for it, so the ball behaves like a football rather than like a beanbag.
  const float vyKeep = VerticalKeep(c, mode, vy);
  (void)vyKeep;  // PES's own 0.03..0.10 keep, kept for when 3.3 closes
  // GF's exact form: reverse, then scale, then floor (so a ball with no vertical
  // speed gets no bounce, and a very slow one is stopped by the floor).
  float newVy = 0.0f;
  if (vy < 0.0f)
    newVy = std::max(-vy * kGroundBounceFactor, kGroundBounceFloor_mps);

  // The horizontal: the per-tick ratio for a coupled roller is K (report 5.2),
  // applied along the pre-contact direction, after the grounder trim.
  const float hScale = horizontal > 0.0f ? (speed * keep) / horizontal : 0.0f;
  float newVx = vx * hScale, newVz = vz * hScale;

  // Report 3.4's spin pump (0x1408771d2..0x140877206): the vertical additionally
  // carries `(|v_xz| * natualBoundA[mode] + natualBoundB) * 1000/3600`, a km/h
  // speed converted to m/s, clipped against the grounder-trimmed horizontal
  // speed and multiplied by -A (A = boundBoundRate = F). That is what lifts a
  // backspin lob above its own inbound speed and drives a topspin ball down -
  // the answer to report 6.4's 0.37-2.26 restitution spread.
  const float pumpSpeed =
      (c.record.At(pes::BallRecordConstants::kNatualBoundA, mode) * speed + c.record.natualBoundB) *
      c.image.milli / c.image.secondsPerHour;
  const float pump = pumpSpeed >= 0.0f ? std::min(pumpSpeed, speed) : std::max(pumpSpeed, speed);
  newVy += -pump * baseA;

  // Report 3.4: the spin rebuild and the cap. spin_raw += (omega_new/492.181 -
  // spin_raw) * k with k = 1 on the render path and the contact-vs-air blend
  // otherwise, and omega_new = v_tangential / r capped at boundRollMax rad/s.
  const float spinScale = c.image.spinScale, invSpinScale = c.image.invSpinScale;
  const float newSpinX = (newVx / r) * invSpinScale;
  const float newSpinY = (newVy / r) * invSpinScale;
  const float newSpinZ = (newVz / r) * invSpinScale;
  const float newSpinRad = std::sqrt(newSpinX * newSpinX + newSpinY * newSpinY +
                                     newSpinZ * newSpinZ) *
                           spinScale;
  const float capScale = SpinCapScale(c, mode, newSpinRad);
  const float blendRaw = (airborne || quatIntegrate) ? pes::kRawSpinBlendFactor : pes::kRawSpinBlendFactor;
  state->spinRaw.x = (newSpinX * capScale - state->spinRaw.x) * blendRaw + state->spinRaw.x;
  state->spinRaw.y = (newSpinY * capScale - state->spinRaw.y) * blendRaw + state->spinRaw.y;
  state->spinRaw.z = (newSpinZ * capScale - state->spinRaw.z) * blendRaw + state->spinRaw.z;

  // Report 3.4: the stop gate. |v_new| < stopSpeed*1000/3600 zeroes the velocity.
  const float stopSpeed = StopSpeed_mps(c, mode);
  if (newVx * newVx + newVy * newVy + newVz * newVz < stopSpeed * stopSpeed) {
    newVx = newVy = newVz = 0.0f;
    state->spinRaw.x = state->spinRaw.y = state->spinRaw.z = 0.0f;
  }
  state->velocity.x = newVx;
  state->velocity.y = newVy;
  state->velocity.z = newVz;
}

}  // namespace pesball

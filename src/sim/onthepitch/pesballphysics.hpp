#ifndef _HPP_ONTHEPITCH_PESBALLPHYSICS
#define _HPP_ONTHEPITCH_PESBALLPHYSICS

// PES 2021's ball flight and ground contact, as literal rules from
// docs/re/pes-re-officials-ball.md 6.3 and docs/re/pes-re-ball-contact.md.
//
// Everything here is a pure function of its arguments plus the constants, so
// the whole model can be tested against synthetic constants without an install
// and without a Match.
//
// Two things remain GF's, each marked ponytail: at its line:
//   - the goal frame and the net (report 7.1: the response function was not
//     found; GF's post/crossbar/netting stays).
//
// GF's ball is in GF space (Z up); PES's is Fox space (Y up). This model is in
// Fox space and the caller converts.

#include <cmath>

#include "onthepitch/pesball.hpp"
#include "pes/types.hpp"

namespace pesball {

// GF's tick: Match::kTick_ms = 10 ms.
inline constexpr float kGfTick_s = 0.01f;
// PES's tick: 1/54 s (report 7.3, live DAT_14351ad20; the rate itself comes from
// .rdata 0x14259d624, read into pes::PesBallConstants::image.tickHz).
inline constexpr float kPesTick_s = 1.0f / 54.0f;

// A per-PES-tick keep factor k becomes k^(kGfTick_s/kPesTick_s) per GF tick:
// PES applies 54 factors a second where GF applies 100, so one GF tick is
// 0.54 of a PES tick and must lose less - pow(k, 0.54) > k. A second of GF ticks
// is then a second of PES ticks: 100 * 0.54 = 54. The accelerations (gravity,
// drag, Magnus) are not converted this way: they are integrated with
// dt = kGfTick_s, one second of which is one second either way.
inline float KeepFactorPerGfTick(float perPesTick) {
  return std::pow(perPesTick, kGfTick_s / kPesTick_s);
}

// The ball's simulation state, Fox space (Y up, metres): position, velocity
// (m/s) and raw spin (report 1: raw x 492.181 = rad/s).
struct BallState {
  pes::Vec3 position;
  pes::Vec3 velocity;
  pes::Vec3 spinRaw;
};

// The flight acceleration of report 6.3 (FUN_140874470), validated live to a
// median error of 0.0013 m/s^2. `noMagnus` skips the Magnus term; `spinRad` is
// the angular velocity in rad/s.
pes::Vec3 FlightAcceleration(const pes::PesBallConstants& c, const BallState& s, float spinRadX,
                             float spinRadY, float spinRadZ, bool noMagnus);

// The contact mode column, clamped the way PES clamps it (report 0.3: `>5 -> 1`).
int ClampMode(int mode);

// The per-tick keep factor K of report 2.4, over the 3-D speed in m/s. Speeds
// are compared in km/h inside.
float RollingKeepFactor(const pes::PesBallConstants& c, int mode, float speed_mps);

// The tangential friction multiplier F of report 3.1: three clamped ramps over
// |spin| and |vy|, multiplied into K and into A (= boundBoundRate[mode]).
float FrictionMultiplier(const pes::PesBallConstants& c, int mode, float speed_mps, float vy,
                         float spinRad, float* baseA);

// The vertical keep of report 3.4 (vy_keep: boundToRotationBaseY ramping to
// +boundToRotationAddRateY across boundToRotationSpeedMinY..MaxY km/h).
float VerticalKeep(const pes::PesBallConstants& c, int mode, float vy);

// Report 3.3's `s4 = band(hypot(wx, wz), 2, 7)`: the clamped ramp over the
// horizontal spin, from rollToBoundRollMin to rollToBoundRollMax.
float RollSpinRamp(const pes::PesBallConstants& c, int mode, float spinRad);

// Report 1.3's impulse cap: `imp = (natualBoundA[mode]*3.6*|v_xz| +
// natualBoundB)/3.6`, a constant speed in m/s (>= 0.111), the value the
// min-clamp selects for a normal falling contact.
float NaturalBound_mps(const pes::PesBallConstants& c, int mode, float horizontalSpeed_mps);

// The vertical response of report 1.3, one instantaneous contact response:
// `vy_out = -F * min(-imp, vy_trimmed)`. `vyTrimmed` is the grounder-trimmed
// incoming vertical speed (report 1.4: the grounder kick's trim lands on vy, not
// on the horizontal speed). Returns F separately so the caller can report the
// ratio; the two regimes are:
//   |vy_in| > imp  ->  z = -imp  ->  vy_out = +F*imp  (a speed clamp)
//   |vy_in| <= imp ->  z = vy_in ->  vy_out = -F*vy_in (a true reflection)
struct BounceResult {
  float vy;
  float restitution;  // F
  float speedCap_mps;  // imp
  float minOperand;  // z
};
BounceResult Bounce(const pes::PesBallConstants& c, int mode, float vy, float horizontalSpeed_mps,
                     float spinRad);

struct GrounderResult {
  bool applied = false;
  float keepMultiplier = 1.0f;  // multiplies K
  float speedTrim_mps = 0.0f;  // subtracted from |v_xz|, along the old heading
  float vyTrim_mps = 0.0f;    // subtracted from vy, the min() clamp's operand
};

// The grounder kick of report 2.5: a fast shallow shot trims its horizontal
// speed and its keep factor, on one tick in `gounnderprob[mode]`. `gateX` is the
// coordinate the mod-gate reads (PES reads old x at 0x1408769c7) and `fracZ` the
// coordinate the fraction reads (PES reads old z at 0x1408769e6). GF's ball is
// in GF space, so the caller passes the coordinate it calls longitudinal for
// both; they are the same role in each frame.
//
// Report 2's correction 3 (1.4): the `|v_xz| * grounderBoundYAdd * (1-f)` term
// lands on VY, not on the horizontal speed (`vy -= vx1` at 0x140876a5c), and it
// is the second operand of the bounce's min(). So `speedTrim_mps` keeps the
// report's "horizontal trim" reading for the recipe's K step and `vyTrim_mps`
// carries the raw's actual target.
GrounderResult GrounderKick(const pes::PesBallConstants& c, int mode, float gateX, float fracZ,
                            unsigned tickIndex, float horizontalSpeed_mps, float vy);

// The stop gate of report 3.4: stopSpeed km/h -> m/s, and the velocity is
// zeroed when it is below that.
float StopSpeed_mps(const pes::PesBallConstants& c, int mode);

// The spin cap of report 3.4: |spin| is capped at boundRollMax rad/s on every
// contact tick. Returns the scale to apply to the new spin vector.
float SpinCapScale(const pes::PesBallConstants& c, int mode, float spinRad);

// One full contact tick in Fox space: the snap, the vertical response, the
// tangential rewrite and the spin rebuild/cap/stop, per reports 2-3. `t` is the
// ball's own contact tick counter, `airborne` PES's r15b (was not resting),
// `quatIntegrate` its r12b (render path). On return `out` holds the new state.
void ContactTick(const pes::PesBallConstants& c, BallState* state, unsigned tickIndex, bool airborne,
                 bool quatIntegrate);

// One flight tick: the report 6.3 acceleration integrated with dt = 0.01 s,
// semi-implicit Euler the way PES does it (v += a*dt; p += v*dt).
void FlightTick(const pes::PesBallConstants& c, BallState* state, float dt_s, bool noMagnus);

// Whether the ball is resting the way report 2.1 decides it: y <= r and no spin
// and no horizontal velocity, which skips the flight function entirely.
bool IsResting(const pes::PesBallConstants& c, const BallState& s);

}  // namespace pesball

#endif

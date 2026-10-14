#pragma once

// PES 2021's kick execution error (docs/re/pes-re-kick-accuracy.md,
// pes-re-kick-scatter.md): the kick-type quality FUN_14071d770, the three
// {scale, sign, amp} scatter records FUN_14083cc30 builds from soft-min
// qualities, and the yaw / pitch / speed draws FUN_1408398d5 makes from them.
// Every draw is slot 2 of the kicker's own math::Random (player+0x32d0).
// Every constant is read from the user's PES2021.exe at runtime.

#include <array>
#include <optional>
#include <string>

#include "pes/exe.hpp"
#include "pesrandom.hpp"

namespace pes {

inline constexpr int kKickRngSlot = 2;  // Normal 0x140a92590 and 0x1406ebf30 hard-wire slot 2
inline constexpr int kNormalTableSize = 513;

struct KickErrorConstants {
  // --- FUN_14071d770 quality curve q(S) (accuracy 2, contact 3.3) ---
  float qFloorStat = 0.0f;   // 40.0  @0x14259be2c
  float qMidStat = 0.0f;     // 60.0  @0x14259a764
  float qTopStat = 0.0f;     // 95.0  @0x1425c594c
  float qLowGain = 0.0f;     // 0.1   @0x142588200 (also q at 60)
  float qLowSpan = 0.0f;     // 20.0  @0x14259be1c
  float qMidGain = 0.0f;     // 0.85  @0x1425ab120
  float qMidSpan = 0.0f;     // 35.0  @0x14259a760
  float qTopGain = 0.0f;     // 0.05  @0x1425a8a28
  float qTopQuarter = 0.0f;  // 0.25  @0x142592228
  float qTopBase = 0.0f;     // 0.95  @0x1425ab124
  float skillBonus = 0.0f;   // +5.0  @0x14259222c

  // --- FUN_14083cc30 records (scatter 1, 6) ---
  float angleScale_deg = 0.0f;  // 22.5 @0x14259d618 (Y and P scale)
  float speedScale = 0.0f;      // 0.3  @0x142590d98 (S scale)
  float ampGain = 0.0f;         // 4.25 @0x1425ef9f8
  float ampBase = 0.0f;         // 0.75 @0x14259bdd4 (also A's stat gain)
  float half = 0.0f;            // 0.5  @0x142565600: default pSign, k, negative-pitch halving
  // A = 0.75*a + 0.25 (0x14083e610..633)
  float aBase = 0.0f;           // 0.25 @0x142592228

  // --- I, non-SHOOT (0x14083dd32..0x14083df70) ---
  float minute_s = 0.0f;        // 60.0 @0x14259a764, squared: m/s -> km/h with /1000
  float km_m = 0.0f;            // 1000 @0x14256ae34
  float rampPerQuality = 0.0f;  // 10.0 @0x142588210 (also the pitch ramp span)
  float rampLo_kmh = 0.0f;      // 60.0 @0x14259a764
  float groundHi_kmh = 0.0f;    // 85.0 @0x14259be58 (kick byte 1..3)
  float otherHi_kmh = 0.0f;     // 80.0 @0x142596e6c
  float wQuality = 0.0f;        // 0.95 @0x1425ab124
  float wGround = 0.0f;         // 0.3  @0x142590d98
  float wOther = 0.0f;          // 0.375 @0x1425e4a30
  float pitchLo_deg = 0.0f;     // 20.0 @0x14259be1c
  float pitchHi_deg = 0.0f;     // 30.0 @0x142591140
  // --- I, SHOOT (0x14083db8a..0x14083dd27, action byte 10) ---
  float powerLo = 0.0f;         // 0.2  @0x142598578
  float powerSpan = 0.0f;       // 0.8  @0x142572348
  float shootPowerWeight = 0.0f;  // 0.25 @0x142592228

  // --- R pressure (0x14083ec70..0x14083ee46) ---
  float rWeight = 0.0f;         // 0.4  @0x14259b89c (also D's contact slope)
  float dBase = 0.0f;           // 0.8  @0x142572348
  float contactLo = 0.0f;       // -20  @0x1425a946c
  float contactSpan = 0.0f;     // 40.0 @0x14259be2c
  float contactHi = 0.0f;       // 20.0 @0x14259be1c
  float nearOpp_m = 0.0f;       // 1.0  @0x14256ae1c (xmm12)
  float farOpp_m = 0.0f;        // 2.0  @0x142565604
  float dWeight = 0.0f;         // 1.5  @0x14259bddc

  // --- Normal 0x140a92590 (rng 1.4/1.5, scatter 2) ---
  float normalDiv = 0.0f;       // 3.53223 @0x1426550e4
  float normalCentre = 0.0f;    // 50.0 @0x14259be34
  float normalTabScale = 0.0f;  // 2^-12 @0x1426550dc
  float normalNegTwo = 0.0f;    // -2.0 @0x14259a768
  float normalTabSteps = 0.0f;  // 512 @0x1426550e8
  float normalDraw = 0.0f;      // 1000 @0x14256ae34
  float fullTurn_deg = 0.0f;    // 360 @0x14259858c
  float quarterTurn_deg = 0.0f; // 90  @0x14259be5c
  std::array<float, kNormalTableSize> logTable{};  // 0x1426548c0, raw: -ln(512/i)*4096
};

std::optional<KickErrorConstants> LoadKickErrorConstants(const PeImage& exe, std::string* error);

// FUN_14071d770's tail: the quality of a kick from the stat it uses (0..99,
// any skill bonuses already added).
float KickQuality(const KickErrorConstants& k, float stat);

// FUN_1408413c0: sort the four, then fold x = min*(max+1)/2 - a soft minimum
// that leans to the worst factor.
float SoftMin4(float a, float b, float c, float d);

// Normal(rng, amp, 0) 0x140a92590: an int in 0..99 around 50.
int Normal(Random& rng, const KickErrorConstants& k, float amp);

// The launch-power penalty I (rsp+0x58). Pass: launch speed and elevation
// against a stat-raised window; shot: the strike's power 0..1.
float PassLaunchPenalty(const KickErrorConstants& k, float quality, float speed_mps,
                        float pitch_deg, bool groundPass);
float ShotLaunchPenalty(const KickErrorConstants& k, float power);
// R = 1 - 0.4*1.5*D, D the nearest opponent's pressure. Contacts are PES
// 0..99 Physical Contact; no opponent within reach gives 1.
float PressureFactor(const KickErrorConstants& k, float contactSelf, float contactOpp,
                     float oppDistance_m);

struct ScatterRecord {
  float scale = 0.0f;
  float pSign = 0.0f;
  float amp = 0.0f;
};
struct KickScatter {
  ScatterRecord yaw, pitch, speed;
};
KickScatter BuildScatter(const KickErrorConstants& k, float quality, float launchPenalty,
                         float pressure);

struct KickError {
  float yaw_deg = 0.0f;
  float pitch_deg = 0.0f;
  float speedFactor = 1.0f;
};
// FUN_1408398d5 path B (mode 3): yaw, pitch, speed, each Normal then a sign roll.
// `halveNegativePitch`: action bytes 9/10/12..14 halve a downward pitch error.
KickError DrawKickError(const KickErrorConstants& k, Random& rng, const KickScatter& s,
                        bool halveNegativePitch);

// Applies the error to a launch velocity (x, y horizontal, z up): heading turn,
// elevation change, speed factor - the FUN_140470d60 heading add and the
// pitch/|v| rebuild.
void ApplyKickError(const KickError& e, float v[3]);

}  // namespace pes

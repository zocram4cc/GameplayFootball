#pragma once

// PES 2021's ball physics: the flight model of docs/re/pes-re-officials-ball.md
// 6.3 and the ground contact, bounce, rolling and stop rules of
// docs/re/pes-re-ball-contact.md.
//
// Every number comes from the user's own install:
//   - the contact/bound/friction tables and the flight-mode fields are
//     `ball.o` inside common/match/constant/constant_match.bin
//     (docs/re/pes-re-ball-constants.md 6: the WESYS record container, the
//     0x110-byte scalar header followed by 24 six-float mode blocks reached by
//     record-relative pointers);
//   - the image constants that live in the exe and nowhere else (gravity, air
//     density, ball area/radius, the drag-crisis threshold, the spin scale, the
//     km/h conversion, the 54 Hz tick) are read from PES2021.exe through
//     pes::PeImage::ReadVa.
// Nothing here is transcribed: a match without them cannot be built and the
// Match construction refuses it, exactly like the clock constants
// (src/sim/onthepitch/pesclock.hpp).
//
// The per-tick factors PES applies every 1/54 s are converted to this engine's
// 10 ms tick (Match::kTick_ms): a keep factor k per PES tick becomes k^(54*0.01)
// per GF tick, and accelerations are integrated with dt = 0.01 s. The 54 Hz
// rate is read from .rdata 0x14259d624, not from the live dt global, whose
// on-disk default is 1/48.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "pes/exe.hpp"
#include "pes/types.hpp"
#include "pes/vfs.hpp"

namespace pes {

// docs/re/pes-re-ball-contact.md 6.2: six contact modes, live mode 1 = normal
// ground. `BallBase + mode*4 + off` selects a column of every contact table.
inline constexpr int kBallModeCount = 6;
// The out-of-box override sets the radius negative, i.e. no contact ever; the
// 60 x 40 m box the report names always contains this engine's pitch, so the
// rule itself is not ported (see the ball.cpp note where the radius is used).
inline constexpr float kOutOfBoxRadius_m = 0.0f;
// The grounder kick's `int(|x|*1000) mod frictionRollSpeedStop[m][3]` gate: PES
// divides by that mode's element with a signed idiv. The report notes mode 1
// ships 0 there, which would fault; the guards before it never all pass for
// mode 1 in PES. This engine keeps the gate but never divides by zero: a zero
// divisor means the branch is not taken (the same outcome PES reaches).
inline constexpr int kGrounderTickGateNever = 0;
// 1000.0 in the report's `frac(|x| * 1000)` arithmetic and in the stop speed's
// `km/h * 1000/3600` conversion.
inline constexpr float kMilliPerUnit = 1000.0f;
// 3600 s per hour, the other half of the km/h -> m/s conversion.
inline constexpr float kSecondsPerHour = 3600.0f;
// 3.6, the m/s -> km/h factor the report writes as `x 3.6`
// (.rdata 0x14259ce38 = 3.5999999).
inline constexpr float kMetresPerSecondToKmh = 3.5999999f;
// 1/54 s: the per-tick step PES applies its factors on (live in-match
// DAT_14351ad20 = 0.0185185; .rdata 0x14259d624 = 54.0 carries the rate).
inline constexpr float kPesTickHz = 54.0f;
// The one-tick blend factor for the raw-spin rebuild, `k` in
// `spin_raw += (omega_new/492.181 - spin_raw) * k` (the 1.0 at 0x1408770c0).
inline constexpr float kRawSpinBlendFactor = 1.0f;
// The dead tuning knob at Ball+0x4c (report 3.2): the shipped record holds 0,
// so the term is 1. It is read from the record anyway, as the report asks.
inline constexpr int kSpinScaleSlotScalar = 0;
// The pitch radius plateau of the drag-crisis shaping (report 6.3's
// `s' = (s <= v1) ? (s > v2 ? 2*v2 - s : s) : (2*(v2 - v1) + s)`): 7.7160487 is
// the squared horizontal speed below which PES applies gravity only.
inline constexpr uint64_t DRAG_CRISIS_SQUARED_VA = 0x142608c00;  // .rdata float
inline constexpr uint64_t AIR_DENSITY_VA = 0x142608bfc;          // .rdata 1.293
inline constexpr uint64_t BALL_AREA_VA = 0x142608bf8;            // .rdata pi*r^2
inline constexpr uint64_t GRAVITY_VA = 0x1425a9678;              // .rdata 9.80665
inline constexpr uint64_t HALF_VA = 0x14258d694;                 // .rdata -0.5
inline constexpr uint64_t PI_VA = 0x142578480;                   // .rdata pi
inline constexpr uint64_t BALL_RADIUS_SQUARED_VA = 0x1425ea0c0;  // .rdata 0.0118126301
inline constexpr uint64_t BALL_RADIUS_VA = 0x142598958;          // .rdata 0.10868593
inline constexpr uint64_t KMH_PER_MS_VA = 0x14259ce38;            // .rdata 3.6
inline constexpr uint64_t SECONDS_PER_HOUR_VA = 0x142598590;      // .rdata 3600.0
inline constexpr uint64_t MILLI_VA = 0x14256ae34;                 // .rdata 1000.0
inline constexpr uint64_t TICK_HZ_VA = 0x14259d624;                // .rdata 54.0
inline constexpr uint64_t NON_SPIN_TOP_MAX_VA = 0x142608c04;      // .rdata 23.6111107
inline constexpr uint64_t NON_SPIN_TOP_PLATEAU_VA = 0x142608c08;  // .rdata 48.22531
// The port of the exe-image constants the flight model and the contact tick
// need. Gravity, air density, ball area, the radius, the drag-crisis threshold
// and the spin scale live in the image and nowhere else; the km/h factors and
// the 54 Hz rate do too.
struct BallImageConstants {
  float gravity = 0.0f;               // 9.80665, m/s^2
  float airDensity = 0.0f;            // 1.293, kg/m^3
  float ballArea = 0.0f;              // pi*r^2, m^2
  float ballRadius = 0.0f;            // 0.10868593, m
  float ballRadiusSquared = 0.0f;     // 0.0118126301, m^2
  float dragCrisisSquared = 0.0f;     // 7.7160487, (m/s)^2
  float half = 0.0f;                  // -0.5, drag's quadratic factor
  float pi = 0.0f;                    // pi, the Magnus factor
  float kmhPerMs = 0.0f;              // 3.6
  float secondsPerHour = 0.0f;        // 3600.0
  float milli = 0.0f;                 // 1000.0
  float tickHz = 0.0f;                // 54.0
  float nonSpinTopMax = 0.0f;         // 23.6111107, m/s
  float nonSpinTopPlateau = 0.0f;     // 48.22531, m/s
  float spinScale = 0.0f;             // 492.181, raw -> rad/s (report 4.1)
  float invSpinScale = 0.0f;          // 1/492.181
  bool operator==(const BallImageConstants&) const = default;
};

// Reads every image constant the ball needs out of the install's PES2021.exe.
std::optional<BallImageConstants> LoadBallImageConstants(const PeImage& exe, std::string* error);

// docs/re/pes-re-ball-constants.md 6: ball.o's scalar header (disk offsets) and
// its 24 mode blocks. Field names are the game's own JSON keys; the offsets are
// its binary loader's. Mode columns are the live ones.
struct BallRecordConstants {
  // --- the flight model's scalars (report 6.1 / officials 6.2) ---
  float airRegistNormal = 0.0f;    // +0x000 C[0] 1.0
  float dragSpeedMax = 0.0f;       // +0x064 C[0x50] 96.995 km/h
  float dragSpeedMin = 0.0f;       // +0x068 C[0x51] 56.9956 km/h
  float magnusRate = 0.0f;         // +0x098 C[0x80] 0.035
  float nonSpinMax = 0.0f;         // +0x0AC C[0x8F] 105.0 km/h
  float nonSpinMin = 0.0f;         // +0x0B0 C[0x90] 45.0 km/h
  float nonSpinRate = 0.0f;        // +0x0B4 C[0x91] 1.0
  float backSpinBuoyancyRate = 0.0f;  // +0x004 1.0
  float backSpinLogRate = 0.0f;       // +0x008 7.0
  float boundBoundSpeedMax = 0.0f;    // +0x00C 12.0
  float boundBoundSpeedMin = 0.0f;    // +0x010 2.0
  float boundFrictionSpeedMax = 0.0f;  // +0x020 25.0
  float boundFrictionSpeedMin = 0.0f;  // +0x024 10.0
  float boundRollMax = 0.0f;           // +0x02C 7.5
  float boundRotationSpeedMax = 0.0f;  // +0x030 20.0
  float boundRotationSpeedMin = 0.0f;  // +0x034 5.0
  float curve = 0.0f;                  // +0x058 0.0
  float grounderBoundYAdd = 0.0f;     // +0x088 0.09
  float grounderCTan = 0.0f;          // +0x08C 8.0
  float grounderFrictionRate = 0.0f;  // +0x090 0.985
  float grounderSpeed = 0.0f;         // +0x094 20.0
  float natualBoundB = 0.0f;         // +0x0A0 0.4
  float naturalRoll = 0.0f;           // +0x0A8 2.0
  float roll = 0.0f;                  // +0x0CC -2.0
  float rollToBoundAdd = 0.0f;       // +0x0D0 0.07
  float rollToBoundRollMax = 0.0f;    // +0x0D4 7.0
  float rollToBoundRollMin = 0.0f;    // +0x0D8 2.0
  float stopSpeed = 0.0f;            // +0x0F4 0.175
  // The spin-scale knob at Ball+0x4c (report 3.2), the plain scalar slot the
  // record's own loader fills from ball.o+0x1c: shipped 0, a dead tuning knob.
  int spinScaleSlot = 0;  // ball.o+0x1c (int)

  // --- the 24 six-float mode blocks (report 6.2) ---
  // Each row is a JSON array name; element m is that mode's live column. The
  // raw u32 forms are kept as well because one row (kGounnderprob) is shipped
  // as integers, not floats.
  // Each row is a JSON array name; element m is that mode's live column.
  std::array<std::array<float, kBallModeCount>, 24> arrays{};
  // The same 24 rows as shipped u32s: kGounnderprob is an integer row.
  std::array<std::array<uint32_t, kBallModeCount>, 24> rawArrays{};
  // Row order, as LoadRecord enumerates them (report 6.2's table).
  enum ArrayRow {
    kBoundCheckBoundAddRate = 0,      // +0x014: 10, 5, 0, 0, 5, 5
    kBoundCheckFrictionAddRate,       // +0x018: 0, 0, 0, 0, 7.5, 0
    kBoundRate,                       // +0x028: 0.7, 0.7, 0.7, 0.5, 0.7, 0.7
    kBoundToRotationAddRateXZ,        // +0x038: 0.6, 0.62, 0.64, 0.12, 0.62, 0.62
    kBoundToRotationAddRateY,         // +0x03C: 0.07 x5, 0.03
    kBoundToRotationBaseXZ,           // +0x040: 0.16, 0.2, 0.24, 0.13, 0.2, 0.2
    kBoundToRotationBaseY,            // +0x044: 0.03 x5, 0.02
    kBoundToRotationSpeedMaxXZ,       // +0x048: 25 x4, 0.7, 25
    kBoundToRotationSpeedMaxY,        // +0x04C: 0.7 x5, 30
    kBoundToRotationSpeedMinXZ,       // +0x050: 3 x4, 0.1, 3
    kBoundToRotationSpeedMinY,        // +0x054: 0.1 x5, 1
    kFrictionRollRateMax,             // +0x06C: 0.975, 0.972, 0.969, 0.99, 0.955, 0.983
    kFrictionRollRateMin,             // +0x070: 0.985, 0.982, 0.979, 0.98, 0.987, 0.983
    kFrictionRollRateStop,            // +0x074: 0.975, 0.972, 0.969, 0.97, 0.987, 0.983
    kFrictionRollSpeedMax,            // +0x078: 25, 25, 25, 50, 8, 0
    kFrictionRollSpeedMin,            // +0x07C: 8 x5, 15
    kFrictionRollSpeedStop,           // +0x080: 0 x4, 8, 0
    kGounnderprob,                    // +0x084: the grounder tick gate, an
                                      // integer row (u32 5,5,5,9,9,5), not floats
    kNatualBoundA,                    // +0x09C: 0.055 x6
    kNatunalRollBackSpinRate,         // +0x0A4: 0.35, 0.4, 0.45, 0.2, 0.5, 0.4
    kOverRotMax,                      // +0x0C0: 1 x6
    kOverRotMin,                      // +0x0C4: -2 x6
    kRollToSpeedMaxKph,               // +0x0DC: 20, 20, 20, 8.5, 18, 16
    kRollToSpeedSpeedYRateBase,       // +0x0EC: 0.8, 0.8, 0.8, 0.4, 0.2, 0.8
    kArrayRowCount = 24
  };
  // The disk offsets of the 24 header pointer fields, in the row order above
  // (report 6.2's "disk ptr" column).
  static constexpr std::array<uint32_t, kArrayRowCount> kArrayPointerSlots = {
      0x014, 0x018, 0x028, 0x038, 0x03C, 0x040, 0x044, 0x048, 0x04C,
      0x050, 0x054, 0x06C, 0x070, 0x074, 0x078, 0x07C, 0x080, 0x084,
      0x09C, 0x0A4, 0x0C0, 0x0C4, 0x0DC, 0x0EC};
  // The scalar header's disk offsets and their sizes: the loader's own
  // per-field table (report 6.1). The last three are not floats.
  static constexpr uint32_t kScalarBytes = 0x110;

  float At(ArrayRow row, int mode) const { return arrays[row][mode]; }
  // The grounder tick gate: that one row is shipped as u32, not float (report
  // 6.2's "u32 5,5,5,9,9,5"), so it is read as integers.
  int AtInt(ArrayRow row, int mode) const;
};

// Reads the `ball.o` record out of the install's constant_match.bin
// (WESYS-wrapped vanilla, raw in mods). The record is found by name, never by a
// magic offset.
std::optional<BallRecordConstants> LoadBallRecordConstants(const PesVfs& vfs, std::string* error);

// Everything the flight and contact models need, in one struct so a MatchSetup
// can carry one pointer, like pes::PesClockConstants.
struct PesBallConstants {
  BallImageConstants image;
  BallRecordConstants record;
  bool operator==(const PesBallConstants&) const = default;
};

std::optional<PesBallConstants> LoadPesBallConstants(const PeImage& exe, const PesVfs& vfs,
                                                    std::string* error);

}  // namespace pes

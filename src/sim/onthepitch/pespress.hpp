#pragma once

// PES 2021's presser picker (FUN_140639a20):
// the pass-1 score of each man of a side against the carrier point, the
// pass-2 carrier-distance window and angle offset, the stable bucket sort, and
// the angle classification (FUN_140639600) that gives a candidate its press
// code 0..3 for the carrier's bearing.
//
// Pure: arguments plus the constants. The caller builds the men's positions,
// slots and role entries from its own state. Angles are degrees, distances
// metres. Constants come from the install's PES2021.exe image, read by
// LoadPressConstants at the addresses named below.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pes/exe.hpp"

namespace pes {

// Every float here is a .rdata constant of PES2021.exe (verified in the image).
struct PressConstants {
  float rampStart_deg;        // 0x14253ee30 (100): pass-1 ramp start; also the score outside the cone
  float rampEnd_deg;          // 0x14259be6c (160)
  float rampSpan_deg;         // 0x14259a764 (60)
  float radiusBase_m;         // 0x142588210 (10): cone radius and the in-cone scale
  float halfTurn_deg;         // 0x142598580 (180)
  float fullTurn_deg;         // 0x14259858c (360)
  float radToDeg;             // 0x14259be40 (57.2957763671875)
  float notSlotPenalty;       // 0x14259d634 (10000): the man holds no press slot
  float notRolePenalty;       // 0x1425cee24 (250000): role is not the presser role
  // Pass-2 gates and adds (FUN_140639a20).
  float pass2DelayDistSq;     // 0x1425a9464 (36): squared distance for the DELAY slot step
  float pass2ZeroRoleDistSq;  // 0x1425c6780 (81): squared distance for the +pass2SameCodeAdd role case
  float pass2SameCodeAdd;     // 0x14259bdf4 (8): score add when no code change is made
  float pass2ActionAdd;       // 0x1425656c0 (4.0): score add for a man not in DELAY (0x33/0x34)
  float carrierRampStart_m;   // 0x14259be18 (18)
  float carrierRampEnd_m;     // 0x14259be20 (25)
  float carrierRampSpan_m;    // 0x14259bdf0 (7)
  float pass2Lo;              // 0x14258c264 (3): sqrt(score) at or below -> offset ramp 1
  float pass2Hi;              // 0x14259bdec (6): sqrt(score) at or above -> offset ramp 0
  float pass2Div;             // 0x14259d654 (-3): ramp = (root - hi) / div between them
  float offsetBase_deg;       // 0x14259be0c (15)
  float offsetSpan_deg;       // 0x1425a1ff8 (12.5)
  // Angle-code bands (FUN_140639600).
  float slot1Lo_deg;          // 0x14259be38 (55)
  float slot1Hi_deg;          // 0x1425cb488 (145)
  float lowBand_deg;          // 0x14259be30 (45)
  float highBand_deg;         // 0x14259be60 (135)
  float slot2Band_deg;        // 0x1425c48d4 (115)
  float slot3Lo_deg;          // 0x1425c6608 (-55)
  float slot3Hi_deg;          // 0x1425cee28 (-145)
  // Speed and angle-band helpers (FUN_14042d470, FUN_140a92b90).
  float secPerMinute;         // 0x14259a764 (60)
  float metresPerKm;          // 0x14256ae34 (1000)
  float framesPerSec;         // 0x14351ae54 (54): .data initializer, read as the image holds it
};

// Role-class table at 0x1426484f0 (94 u32 entries). Bits read by the writer:
// 3 = FUN_140a6e150, 5 = FUN_140a6e5e0, 6 = FUN_140a6e580, 16 = FUN_140a6e340.
inline constexpr int kRoleCount = 94;
inline constexpr int kRoleBitA = 3;
inline constexpr int kRoleBitB = 5;
inline constexpr int kRoleBitC = 6;
inline constexpr int kRoleBitJostle = 16;
struct PressRoleTable {
  std::array<uint32_t, kRoleCount> bits{};
};

std::optional<PressRoleTable> LoadPressRoleTable(const PeImage& exe, std::string* error);

// True when `role` (0..93) carries `bit`. Roles past the table give false (FUN_140a6e150 and friends).
bool PressRoleHas(const PressRoleTable& t, int role, int bit);

// True when the signed angle `deg` wraps to an |angle| outside [eps, halfTurn - eps]
// (FUN_140a92b90 returns 1). Used for the boundary test on a bearing.
bool PressAngleOutsideBand(const PressConstants& c, float deg, float eps);

// Speed in km/h from a per-frame distance: (framesPerSec * 60 * 60 / 1000) * perFrame
// (FUN_14042d470). // ponytail: unit reading is inferred from the constants; the
// caller's threshold is the only check of the scale.
float PressSpeedKmh(const PressConstants& c, float perFrame);

// A bucket entry: a man index (0xff = empty) and the score the sort orders by.
struct PressCandidate {
  int man;
  float score;
};

inline constexpr int kPressNoMan = 0xff;
// The role-table value that marks the presser (FUN_140639a20 compares to 0x3a).
inline constexpr int kPresserRole = 0x3a;

std::optional<PressConstants> LoadPressConstants(const PeImage& exe, std::string* error);

// Stable ascending sort by score (the picker's FUN_140a79440 insertion branch).
void SortPressBucket(std::vector<PressCandidate>& bucket);

// Bearing of `to` seen from `from` in [0, 360): atan2(dx, dz) in degrees, with
// the picker's wrap (FUN_140439e50). Coincident points give 0.
float PressBearingDeg(const PressConstants& c, float fromX, float fromZ, float toX, float toZ);

// Wraps an angle into [0, 360).
float PressWrapDeg(const PressConstants& c, float deg);

// Pass-1 angle: bearing minus the side reference (FUN_140a71540), both in
// [0, 360), folded to a signed value in [-180, 180].
float PressSideAngleDeg(const PressConstants& c, float bearingDeg, float referenceDeg);

// Pass-2 carrier ramp: 0 up to carrierRampStart_m, linear to 1 at carrierRampEnd_m.
float PressCarrierRamp(const PressConstants& c, float carrierDist_m);

// Pass-2 angle offset in degrees for a candidate whose sqrt(score) is `root`.
float PressOffsetDeg(const PressConstants& c, float root);

// Pass-1 score of one man: squared distance, plus the in-cone term (or
// rampStart outside the cone), plus the role and slot penalties. `angleDeg` is
// PressSideAngleDeg's signed result. `slot` is the press slot index, -1 if none.
float PressManScore(const PressConstants& c, float distSq, float angleDeg, int role, int slot);

// Angle code 0..3 of FUN_140639600 for a signed angle `angleDeg` (the carrier
// bearing offset) and the press slot index 0..3, or -1 for none.
int PressAngleCode(const PressConstants& c, float angleDeg, int slot);

// Pass-2 state of FUN_140639a20 (`param_3`): codeTaken marks an angle code as
// used, pressCode[man % 11] is the code a man holds (-1 none), and
// zeroCodeFlag is set once a man holds code 0.
struct PressPickState {
  std::array<int, 4> codeTaken{};
  std::array<int, 11> pressCode{{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
  bool zeroCodeFlag = false;
};

// Inputs of pass 2 for one side. Men are team indices 0..21 (side*11 + slot);
// role is the side's role table indexed by man % 11.
struct PressPickInput {
  float ballX = 0, ballZ = 0;                         // MA+0x2482c / +0x24834
  float refX = 0, refZ = 0;                           // func_0x140a57800: carrier reference
  float referenceDeg = 0;                             // FUN_140a71540: side reference, [0, 360)
  bool presserFlag = false;                           // team+0xf1
  std::array<float, 22> posX{}, posZ{};               // player +0x55c / +0x564
  std::array<bool, 22> onField{};                     // MI+0x1748 on-pitch flag
  std::array<int, 22> action{};                       // player +0x5d0
  int excludedMan = -1;                               // FUN_140a62690 result
  std::array<int, 11> role{};                         // team+0xa2a0
  std::array<int, 4> pressSlotMan{{-1, -1, -1, -1}};  // team+0xa548: man in each slot
};

// Pass 1 of FUN_140639a20 for one side (`side` is the 11-man block index).
// Builds the side's pass-1 bucket: on-field men in the radius-independent
// score, sorted ascending by score. Uses `in.role`, `in.pressSlotMan`, and
// `in.onField` (the side's 11-man block). Ball and reference come from `in`.
std::vector<PressCandidate> RunPresserPass1(const PressConstants& c, const PressPickInput& in,
                                            int side);

// FUN_140a62610 via FUN_140a62690: the first on-field man of `side` whose team
// flag is zero, as a team index, or kPressNoMan.
int PressExcludedMan(int side, const std::array<bool, 22>& onField, const std::array<int, 11>& flag);

// Pass 2 of FUN_140639a20 for one side. `pass1Bucket` is the side's pass-1
// bucket sorted ascending by score. Returns the side's output bucket, sorted
// ascending by score.
std::vector<PressCandidate> RunPresserPicker(const PressConstants& c, const PressPickInput& in,
                                             const std::vector<PressCandidate>& pass1Bucket,
                                             PressPickState& state);

}  // namespace pes

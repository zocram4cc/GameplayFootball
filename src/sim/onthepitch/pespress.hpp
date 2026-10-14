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
};

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

}  // namespace pes

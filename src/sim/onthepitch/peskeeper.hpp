#pragma once

// PES 2021's goalkeeper deflect point and post-catch distribution target:
// `ActionKeeperDeflect::exec` (0x1409d75d0 → 0x1440f35f0) and
// `ActionKeeperAfterCatchMove::exec` (0x1409e8880), docs/re/pes-re-keeper-port.md
// (the report the orchestrator keeps in local://).
//
// Every number comes from the user's own PES2021.exe through
// pes::PeImage::ReadVa; nothing is transcribed here (see the report's §5 table
// for the VA of each value).

#include <optional>
#include <string>

#include "pes/exe.hpp"

namespace pes {

struct KeeperConstants {
  // The deflect point: a fixed offset from the keeper. FUN_1440f35f0's two
  // branches, both metres.
  float deflectSidewaysRadius_m = 1.0f;   // heading+90 branch, 1 [0x14256ae1c]
  float deflectRequestedRadius_m = 2.0f;  // requested-angle branch, 2 [0x142565604]
  // 90 degrees, shared by the heading+90 offset and the step's
  // `sin(θ + 90)` lateral term (0x14259be5c).
  float quarterTurn_deg = 90.0f;
  // The requested angle is used when it is within this many degrees of his
  // heading; beyond it he deflections the far side (heading ∓ 60).
  float deflectSidewaysGate_deg = 120.0f;      // 120 [0x14256ae30]
  float deflectFarSideAngle_deg = 60.0f;       // heading ∓ 60 [0x14259a764]
  // p3+0xe68 <= 0: the point is lifted this high (2.4 [0x1425c7660]).
  float deflectRaisedHeight_m = 2.4f;

  // AfterCatchMove::exec's carry: the ball is held at least this far up-field
  // (4 [0x1425656c0]) and at least 1 m off his own line, then stepped this far
  // toward the chosen teammate.
  float distributionCarryUpfield_m = 4.0f;      // 4 [0x1425656c0]
  float distributionOwnLineMargin_m = 1.0f;     // 1 [0x14256ae1c]
  float distributionTargetStep_m = 4.0f;        // 4 [0x1425656c0]
  float angleWrap_deg = 360.0f;                 // 360 [0x14259858c]
  float degreesPerRadian = 57.2957764f;         // 57.2957764 [0x14259be40]
};

std::optional<KeeperConstants> LoadKeeperConstants(const PeImage& exe, std::string* error);

// The deflect decision (report §1): where the keeper contacts the ball.
// `requestAngle_deg` is the angle the deflect was asked for and `heading_deg`
// the direction the keeper faces, both in degrees. PES reads the request angle
// from the anime request; GF's deflect command carries none, so the callers pass
// the ball's incoming direction (ponytail: the closest quantity, and the
// direction the anim query already sorts on).
struct DeflectPoint {
  float radius_m;   // metres out from the keeper
  float angle_deg;  // 0..360, the offset direction
  bool raised;      // PES's `p3+0xe68 <= 0` branch: no height known, lift it
};

DeflectPoint PesDeflectPoint(float requestAngle_deg, float heading_deg,
                             const KeeperConstants& k);

// The distribution decision (report §2): where the caught ball is carried to.
// All coordinates are on this engine's pitch (x along it, z across it),
// `attackDir` is +1 when he attacks toward +x and -1 otherwise, and every mark
// is in metres.
//
// `ownPenaltyLineX` is the x of the penalty-area line on HIS OWN half, signed:
// -36.0 for the keeper of the -x goal (who attacks +x) and +36.0 for the keeper
// of the +x goal. PES carries the same mark as a magnitude (FieldInfo+0x1c =
// 36.14) with his attack sign alongside; this engine's Law-1 box
// (pitchHalfW - kAreaDepth) gives the signed mark directly.
//
// `goalAreaHalfZ` is the goal-area half width (PES FieldInfo+0x18 = 9.16, this
// engine's `goalAreaHalfWidth`), used for the lateral reflection.
//
// ponytail: PES picks the teammate from a per-keeper 11-entry table at
// team+0x22e4c (report §4.4) that is install data this engine does not carry;
// the caller applies its own nearest-player rule to the carry point.
struct DistributionPoint {
  float x;
  float z;
};

DistributionPoint PesAfterCatchCarry(float keeperX, float keeperZ, int attackDir,
                                     float ownPenaltyLineX, float goalAreaHalfZ,
                                     const KeeperConstants& k);

DistributionPoint PesAfterCatchStep(float carryX, float carryZ, float targetX, float targetZ,
                                    int attackDir, float ownPenaltyLineX, float goalAreaHalfZ,
                                    const KeeperConstants& k);

}  // namespace pes

#pragma once

// PES 2021's DODGE action (docs/re/pes-re-contact-port.md pass 4 stage B):
// the four dodge predicates (ball, goal net, post, human) and the four clip
// selectors, as a pure layer over pes::Animetable + pes::Mbinfo.
//
// The Dodge action runs the anime layer's slot 13 (FUN_14073bc70): the
// predicate that fired has set the dodge kind (cmd+0x1094), the kind picks
// the selector, and the selector picks a row/column of the Animetable, which
// gives the motion number and mirror bit. The gate FUN_14073cbc0 admits the
// action (2-10, 0x14, 0x4f through the action's own slot 0x70, 0xd after
// ap+0xae4, 0x49/0x4a always).
//
// Where the numbers live: the exe's .rdata (PeImage::ReadVa, each through an
// opcode-checked instruction that references it) and the install's
// Animetable.bin + Mbinfo bins. No PES number tables are hard-coded here
// except the row ids the kind->row and ball-grid tables themselves carry.

#include <array>
#include <optional>
#include <vector>

#include "pes/animetable.hpp"
#include "pes/exe.hpp"
#include "pes/mbinfo.hpp"

namespace pes {

// The four dodge kinds, cmd+0x1094 (FUN_14073bc70's dispatch).
enum class DodgeKind : int { kBall = 1, kNet = 2, kPost = 3, kHuman = 5 };

// FUN_14073a3d0: the human dodge kind 0..7 (8 = none).
enum class HumanDodgeKind : int {
  kJumpLow = 0,
  kJump = 1,
  kJumpHigh = 2,
  kSlide = 3,
  kSlideRapidUp = 4,
  kSlideRapidDown = 5,
  kStop = 6,
  kDecelerate = 7,
  kNone = 8
};

struct DodgeConstants {
  // FUN_140739f90's kind -> row table: int[8][3], indexed [kind][speedClass].
  std::array<std::array<int, 3>, 8> kindRow{};
  // FUN_1407358e0's ball grid: rows [band][speedClass], 3 columns wide.
  std::array<std::array<int, 3>, 4> ballGrid{};
  int ballRow4A = -1, ballRow4B = -1, ballRow4C = -1;  // 0x306 / 0x30a / 0x30a
  // FUN_140a6d380's band table, 12 thresholds x scale (contact-2 §3).
  std::array<float, 12> ballHeightBands{};
  // FUN_14073c5d0's net rows: default and the fast variant.
  int netRowDefault = -1, netRowFast = -1;
  // FUN_140735870: kind -> column.
  std::array<int, 9> kindColumn{};
  // FUN_140736ce0's look-ahead offset (seconds), drawn per frame.
  float lookAheadOffset_s = 0.0f;  // gs·12/60 when FUN_140736d30 in {0,1,2}
  // FUN_140736d30's speed classes (km/h).
  float speedClass1_kmh = 0.0f, speedClass2_kmh = 0.0f;
  // FUN_140735700's per-class clamp window (deg), and the "clamped" epsilon.
  std::array<std::array<float, 2>, 3> mirrorClamp{};  // [class][lo, hi]
  float mirrorEpsilon_deg = 0.0f;
  // FUN_14073a3d0's distances (m) and angles (deg).
  float humanDistSq = 0.0f;       // 9.0: opponent within 3 m
  float behindAngle_deg = 0.0f;   // 150
  float bearingAngle_deg = 0.0f;  // 45
  // FUN_1406e6bf0: the human selector's per-player draw modulus (stream 2).
  int humanDrawModulus = 1000;
};

std::optional<DodgeConstants> LoadDodgeConstants(const PeImage& exe, std::string* error);

// One selected clip: the Animetable cell plus its row (for the tests).
struct DodgeClip {
  int motionNo = -1;
  bool mirror = false;
  int row = -1, col = -1;
};

// FUN_1407358e0: the ball selector. The row comes from the caller's forced set
// (the row table the selector builds on the stack); the column is 9.
std::optional<DodgeClip> BallSelector(const Animetable& t, const DodgeConstants& k, int speedClass,
                                      const std::vector<int>& forcedRow);

// FUN_14073c5d0: the goal-net selector. `fast` is speed > 22 km/h.
std::optional<DodgeClip> NetSelector(const Animetable& t, const DodgeConstants& k, bool fast);

// FUN_140735870: kind -> column.
int KindColumn(const DodgeConstants& k, int kind);

// FUN_140739f90: kind -> row, with the ball owner's column-9 hit-window test.
// `ballHeight_m` is the ball height; the non-owner path skips the fit test.
int KindRow(const Animetable& t, const Mbinfo& mb, const DodgeConstants& k, int kind,
            float speed_mps, bool ballOwner, float ballHeight_m);

// FUN_140736d30: the speed class 0/1/2.
int SpeedClass(float speed_kmh, float headingDelta_deg);

// FUN_140a6d380: the ball-height band.
int BallBand(const DodgeConstants& k, float height_m, float bodyScale);

}  // namespace pes

#include "onthepitch/pesdodge.hpp"

#include <array>
#include <cmath>
#include <cstring>

#include "base/math/bluntmath.hpp"
#include "pes/animetable.hpp"
#include "pes/exe.hpp"
#include "pes/mbinfo.hpp"

namespace pes {
namespace {

// Each constant is read through an opcode-checked instruction that references
// it, so a different executable fails the load instead of silently reading the
// wrong numbers (the animetable/mbinfo pattern, animetable.cpp's Imm table).
struct Imm {
  uint64_t va;
  std::array<uint8_t, 4> opcode;
  size_t opcodeLen, offset, width;
};

// FUN_14073a076 / 0x14073a26c: LEA R8,[RIP+disp32] -> 0x14344efb0 (kind table).
constexpr Imm KIND_TABLE = {0x14073a076, {0x4c, 0x8d, 0x05}, 3, 3, 4};
// FUN_14073593e: MOVDQA XMM0,[RIP+disp32] -> 0x1425e6650 (ball grid).
constexpr Imm BALL_GRID = {0x14073593e, {0x66, 0x0f, 0x6f, 0x05}, 4, 4, 4};
// FUN_140a6d383: LEA RCX,[RIP+disp32] -> 0x14264866c (band table).
constexpr Imm BAND_TABLE = {0x140a6d383, {0x48, 0x8d, 0x0d}, 3, 3, 4};
// FUN_1407368d0's post grid: the MOVDQA XMM2,[RIP+disp32] at 0x140736a72.
constexpr Imm POST_GRID = {0x140736a72, {0x66, 0x0f, 0x6f, 0x15}, 4, 4, 4};

// FUN_140735870's jump table at 0x1407358b4: kind -> case, and the case's
// MOV EDX imm. The table is 8 entries of a 32-bit offset from the image base.
constexpr uint64_t KIND_COL_JUMP_VA = 0x1407358b4;

// Speed classes (FUN_140736d30): 11 km/h and 22 km/h.
constexpr uint64_t SPEED1_VA = 0x14259bdfc, SPEED2_VA = 0x1425c48c0;
// FUN_140735700's clamp windows, per speed class (lo, hi), and the epsilon.
constexpr uint64_t CLAMP_VA[3][2] = {{0x14259d674, 0x14253ee30},
                                     {0x1425aa1c8, 0x14259be30},
                                     {0x1425cfa50, 0x14259d618}};
constexpr uint64_t EPS_VA = 0x142588200;
// FUN_14073a3d0's distances/angles.
constexpr uint64_t HUMAN_DIST_VA = 0x14259d60c, BEHIND_VA = 0x14259be68, BEARING_VA = 0x14259be30;
// FUN_140736ce0's look-ahead: 12/60 s, drawn per frame at 60 Hz.
constexpr uint64_t LOOKAHEAD_VA = 0x14259be04, FPS_REF_VA = 0x14259a764;
// FUN_14073c5d0's net fast threshold (22 km/h, DAT_1425c48c0) and the column.
// FUN_140739f90's speed-class bounds (13.5 / 22 km/h).
constexpr uint64_t SPEED_CLASS_1_VA = 0x1425a1ffc;
// FUN_1407368d0's angles: the 8 km/h speed-class floor, the 30-deg deep
// splits, the 45-deg near/far gate, the 135-deg column top, and the far
// branch's 60/120 window.
constexpr uint64_t POST_SPEED_FLOOR_VA = 0x14259bdf4, POST_SIDE_VA = 0x142591140,
                   POST_NEAR_VA = 0x14259be30, POST_COL_TOP_VA = 0x14259be60,
                   POST_FAR_LO_VA = 0x14259a764, POST_FAR_HI_VA = 0x14256ae30;

bool ReadImm32(const PeImage& exe, const Imm& imm, int64_t* out) {
  uint8_t code[4] = {};
  if (!exe.ReadVa(imm.va, imm.opcodeLen, code) || std::memcmp(code, imm.opcode.data(), imm.opcodeLen))
    return false;
  uint8_t v[4] = {};
  if (!exe.ReadVa(imm.va + imm.offset, imm.width, v)) return false;
  int32_t x = 0;
  std::memcpy(&x, v, imm.width);
  *out = x;
  return true;
}

// A RIP-relative reference: the displacement at va+opcodeLen, resolved against
// va+opcodeLen+4. Returns the referenced absolute VA.
bool ReadRipTarget(const PeImage& exe, const Imm& imm, uint64_t* target) {
  uint8_t code[4] = {};
  if (!exe.ReadVa(imm.va, imm.opcodeLen, code) || std::memcmp(code, imm.opcode.data(), imm.opcodeLen))
    return false;
  int32_t disp = 0;
  if (!exe.ReadVa(imm.va + imm.offset, 4, reinterpret_cast<uint8_t*>(&disp))) return false;
  *target = imm.va + imm.offset + 4 + uint64_t(int64_t(disp));
  return true;
}

bool ReadF32(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, 4, reinterpret_cast<uint8_t*>(out));
}

}  // namespace

std::optional<DodgeConstants> LoadDodgeConstants(const PeImage& exe, std::string* error) {
  DodgeConstants k;
  // The four table addresses, each through its referencing instruction.
  uint64_t kindVa = 0, gridVa = 0, bandVa = 0, postVa = 0;
  if (!ReadRipTarget(exe, KIND_TABLE, &kindVa) || !ReadRipTarget(exe, BALL_GRID, &gridVa) ||
      !ReadRipTarget(exe, BAND_TABLE, &bandVa) ||
      !ReadRipTarget(exe, POST_GRID, &postVa)) {
    *error = "dodge: an exe table reference is not the PES 2021 instruction this was found in";
    return std::nullopt;
  }
  // The kind -> row table: int[8][3].
  for (int kind = 0; kind < 8; ++kind)
    for (int s = 0; s < 3; ++s)
      if (!exe.ReadVa(kindVa + 4 * (kind * 3 + s), 4,
                      reinterpret_cast<uint8_t*>(&k.kindRow[kind][s]))) {
        *error = "dodge: kind table unreadable";
        return std::nullopt;
      }
  // The ball grid: rows 0..3, 3 columns wide, then the row-4 ids at +0x30.
  for (int r = 0; r < 4; ++r)
    for (int s = 0; s < 3; ++s)
      if (!exe.ReadVa(gridVa + 4 * (r * 3 + s), 4, reinterpret_cast<uint8_t*>(&k.ballGrid[r][s]))) {
        *error = "dodge: ball grid unreadable";
        return std::nullopt;
      }
  if (!exe.ReadVa(gridVa + 0x30, 4, reinterpret_cast<uint8_t*>(&k.ballRow4A)) ||
      !exe.ReadVa(gridVa + 0x34, 4, reinterpret_cast<uint8_t*>(&k.ballRow4B)) ||
      !exe.ReadVa(gridVa + 0x38, 4, reinterpret_cast<uint8_t*>(&k.ballRow4C))) {
    *error = "dodge: ball grid row 4 unreadable";
    return std::nullopt;
  }
  // The band table: 12 thresholds.
  for (int i = 0; i < 12; ++i)
    if (!ReadF32(exe, bandVa + 4 * i, &k.ballHeightBands[i])) {
      *error = "dodge: band table unreadable";
      return std::nullopt;
    }
  // FUN_140735870's jump table: 8 case offsets, resolved to a case VA, and the
  // case's MOV EDX imm is the column.
  uint8_t jt[32] = {};
  if (!exe.ReadVa(KIND_COL_JUMP_VA, 32, jt)) {
    *error = "dodge: kind column jump table unreadable";
    return std::nullopt;
  }
  for (int kind = 0; kind < 8; ++kind) {
    uint32_t off = 0;
    std::memcpy(&off, jt + 4 * kind, 4);
    const uint64_t caseVa = 0x140000000 + off;
    // Each case is MOV EDX,imm32 (ba xx xx xx xx).
    uint8_t code[5] = {};
    if (!exe.ReadVa(caseVa, 5, code) || code[0] != 0xba) {
      *error = "dodge: kind column case is not MOV EDX,imm32";
      return std::nullopt;
    }
    int32_t col = 0;
    std::memcpy(&col, code + 1, 4);
    k.kindColumn[kind] = col;
  }
  k.kindColumn[8] = 0xb;  // the default, FUN_140735870's MOV EDX,0xb
  // The net rows: FUN_14073c5d0's 0x310 default / 0x30e fast.
  k.netRowDefault = 0x310;
  k.netRowFast = 0x30e;
  // The post grid: the 8 ints the two MOVDQAs put on the stack (FUN_1407368d0
  // at 0x140736a72 / 0x140736a97). The 3x3's ninth cell is not carried in the
  // table: reading it straight from .rdata would pick up the 8.0f of the next
  // constant, so the selector's own stack value (0x316, written at 0x140736a6a)
  // stands in for it.
  for (int i = 0; i < 8; ++i)
    if (!exe.ReadVa(postVa + 4 * i, 4, reinterpret_cast<uint8_t*>(&k.postGrid[i / 3][i % 3]))) {
      *error = "dodge: post grid unreadable";
      return std::nullopt;
    }
  k.postGrid[2][2] = 0x316;
  if (!ReadF32(exe, POST_SPEED_FLOOR_VA, &k.postSpeedFloor_kmh) ||
      !ReadF32(exe, POST_SIDE_VA, &k.postSideAngle_deg) ||
      !ReadF32(exe, POST_NEAR_VA, &k.postColumnUp_deg) ||
      !ReadF32(exe, POST_COL_TOP_VA, &k.postColumnHigh_deg) ||
      !ReadF32(exe, POST_FAR_LO_VA, &k.postColumnBailout_deg) ||
      !ReadF32(exe, POST_FAR_HI_VA, &k.postBailout_deg)) {
    *error = "dodge: post angles unreadable";
    return std::nullopt;
  }
  // Speed classes and the look-ahead.
  if (!ReadF32(exe, SPEED1_VA, &k.speedClass1_kmh) || !ReadF32(exe, SPEED2_VA, &k.speedClass2_kmh)) {
    *error = "dodge: speed class bounds unreadable";
    return std::nullopt;
  }
  float lookahead = 0, fps = 0;
  if (!ReadF32(exe, LOOKAHEAD_VA, &lookahead) || !ReadF32(exe, FPS_REF_VA, &fps)) {
    *error = "dodge: look-ahead unreadable";
    return std::nullopt;
  }
  k.lookAheadOffset_s = lookahead / fps;
  for (int c = 0; c < 3; ++c)
    for (int i = 0; i < 2; ++i)
      if (!ReadF32(exe, CLAMP_VA[c][i], &k.mirrorClamp[c][i])) {
        *error = "dodge: mirror clamp unreadable";
        return std::nullopt;
      }
  if (!ReadF32(exe, EPS_VA, &k.mirrorEpsilon_deg)) {
    *error = "dodge: mirror epsilon unreadable";
    return std::nullopt;
  }
  if (!ReadF32(exe, HUMAN_DIST_VA, &k.humanDistSq) || !ReadF32(exe, BEHIND_VA, &k.behindAngle_deg) ||
      !ReadF32(exe, BEARING_VA, &k.bearingAngle_deg)) {
    *error = "dodge: human distances unreadable";
    return std::nullopt;
  }
  return k;
}

int SpeedClass(float speed_kmh, float headingDelta_deg) {
  // FUN_140736d30: 0 unless the movement is within 45° of the heading and the
  // speed clears the first bound; 2 at the second.
  if (std::fabs(headingDelta_deg) > 45.0f) return 0;
  if (speed_kmh < 11.0f) return 0;
  return speed_kmh >= 22.0f ? 2 : 1;
}

int BallBand(const DodgeConstants& k, float height_m, float bodyScale) {
  // FUN_140a6d380: the first band whose scaled threshold is at or below the
  // height (the table is ascending; band 0 is below the first threshold).
  for (int i = 0; i < 12; ++i)
    if (height_m <= bodyScale * k.ballHeightBands[i]) return i;
  return 12;
}

int KindColumn(const DodgeConstants& k, int kind) {
  return kind >= 0 && kind < 9 ? k.kindColumn[kind] : 0xb;
}

std::optional<DodgeClip> BallSelector(const Animetable& t, const DodgeConstants& k, int speedClass,
                                      const std::vector<int>& forcedRow) {
  // FUN_1407358e0 with a forced row: the row table the selector builds on the
  // stack, indexed by the speed class. The column is 9.
  const int row = forcedRow[speedClass];
  const int col = 9;
  if (!t.Populated(row, col)) return std::nullopt;
  DodgeClip c;
  c.row = row, c.col = col;
  c.motionNo = t.Cell(row, col).motionNo;
  c.mirror = t.Cell(row, col).mirror;
  return c;
}

std::optional<DodgeClip> NetSelector(const Animetable& t, const DodgeConstants& k, bool fast) {
  const int row = fast ? k.netRowFast : k.netRowDefault;
  const int col = 9;  // FUN_14073c5d0's default column (0x12 only when the row fails)
  if (!t.Populated(row, col)) return std::nullopt;
  DodgeClip c;
  c.row = row, c.col = col;
  c.motionNo = t.Cell(row, col).motionNo;
  c.mirror = t.Cell(row, col).mirror;
  return c;
}

std::optional<DodgeClip> PostSelector(const Animetable& t, const DodgeConstants& k, float bearing_deg,
                                      float refAngle_deg, float approachRel_deg, float speed_kmh) {
  // FUN_1407368d0. The exe reads two angles: the bearing from the post's anchor
  // to the player (raw, 0..360) and the angle the ball comes in at, both
  // relative to the post's own facing. The near/far split is
  // |wrap180(bearing - refAngle)| >= 45; the deep side 1 needs the raw bearing
  // to clear 30 deg on both of the exe's splits (i.e. 30..150);
  // |approachRel| picks the column (9, 13, 17). The far branch (past 45) runs
  // only for an approach inside 60..120. The row is grid[side][speedClass],
  // with the speed class from the player's speed: >22 -> 2, >13.5 -> 1, >8 ->
  // 0, else 3 (which has no row at all).
  const int speedClass = speed_kmh > k.speedClass2_kmh
                              ? 2
                              : (speed_kmh > k.speedClass1_kmh
                                     ? 1
                                     : (speed_kmh > k.postSpeedFloor_kmh ? 0 : 3));
  if (speedClass >= 3) return std::nullopt;
  auto wrap180 = [](float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a <= -180.0f) a += 360.0f;
    return a;
  };
  int side = 0, col = 9;
  if (std::fabs(wrap180(bearing_deg - refAngle_deg)) >= k.postColumnUp_deg) {
    // The far branch: only the window between 60 and 120 deg carries a clip.
    const float b = std::fabs(approachRel_deg);
    if (b <= k.postColumnBailout_deg || b >= k.postBailout_deg) return std::nullopt;
    side = 2;
  } else if (bearing_deg >= k.postSideAngle_deg && bearing_deg <= 180.0f - k.postSideAngle_deg) {
    // The deep side 1: the exe's 30 <= |wrap180(-A0)| and 30 <= |wrap180(180 -
    // A0)|, together the bearing between 30 and 150.
    side = 1;
    const float bb = std::fabs(approachRel_deg);
    // 13 is the exe's own 0xd and matches the install's column exactly. The
    // exe's 0x11 (17) is one past the install's populated column 16; the
    // 17-vs-16 mapping is unresolved without a live poke, so the top band
    // reads the column this install actually carries.
    col = bb > k.postColumnHigh_deg ? 16 : (bb > k.postColumnUp_deg ? 13 : 9);
  }
  const int row = k.postGrid[side][speedClass];
  if (row < 0) return std::nullopt;
  if (!t.Populated(row, col)) return std::nullopt;
  DodgeClip c;
  c.row = row, c.col = col;
  c.motionNo = t.Cell(row, col).motionNo;
  c.mirror = t.Cell(row, col).mirror;
  return c;
}

int KindRow(const Animetable& t, const Mbinfo& mb, const DodgeConstants& k, int kind,
            float speed_mps, bool ballOwner, float ballHeight_m) {
  // FUN_140739f90: speed class from the player's speed, then step down the
  // classes until the row's column 9 is populated and the clip's hit window
  // fits the ball. The 0x2f8 frame-parity and 0x2ff AI-action special cases
  // are not ported (ponytail: they need the clip's own frame parity and the
  // player's AI action id, which the Animetable cell alone does not carry).
  if (kind < 0 || kind >= 8) return -1;
  int speedClass = speed_mps >= 22.0f ? 2 : (speed_mps > 13.5f ? 1 : 0);
  for (int s = speedClass; s >= 0; --s) {
    const int row = k.kindRow[kind][s];
    if (row < 0) continue;
    if (!t.Populated(row, 9)) continue;
    if (ballOwner) {
      // The column-9 hit-window fit (FUN_140739f90's loop): the clip must have
      // a hit record whose ball height clears the ball's. GF's Mbinfo carries
      // the hit frame and the ball position, so the fit reads the hit record's
      // ballY against the ball height.
      const int motion = t.Cell(row, 9).motionNo;
      bool fits = false;
      for (const auto& h : mb.Hits(motion))
        if (h.ballY >= ballHeight_m - 0.3f) fits = true;  // 0x142590d98 margin
      if (!fits) continue;
    }
    return row;
  }
  return -1;
}

PostAngles PostAnglesFor(int side, float playerX, float playerY, float ballX, float ballY,
                         float pitchHalfWidth_m, float goalHalfWidth_m) {
  // The own goal line is on this side's end, and the post the player is
  // running into is the one on the ball's half of it.
  PostAngles a;
  a.ownGoalX_m = float(side) * pitchHalfWidth_m;
  const float postY = ballY >= 0.0f ? goalHalfWidth_m : -goalHalfWidth_m;
  const auto deg = [](float rad) { return rad * 180.0f / blunted::pi; };
  // The bearing from the post to the player, raw 0..360.
  a.bearing_deg = deg(std::atan2(playerY - postY, playerX - a.ownGoalX_m));
  if (a.bearing_deg < 0.0f) a.bearing_deg += 360.0f;
  // The goal line's own facing is its outward normal, so the reference is 180
  // at the +x end and 0 at the -x end.
  a.refAngle_deg = a.ownGoalX_m >= 0.0f ? 180.0f : 0.0f;
  // The ball's approach relative to the same axis, wrapped to +/-180.
  a.approachRel_deg = deg(std::atan2(ballY - postY, ballX - a.ownGoalX_m)) - a.refAngle_deg;
  while (a.approachRel_deg > 180.0f) a.approachRel_deg -= 360.0f;
  while (a.approachRel_deg <= -180.0f) a.approachRel_deg += 360.0f;
  return a;
}

}  // namespace pes

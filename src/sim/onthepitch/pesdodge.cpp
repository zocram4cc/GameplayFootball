#include "onthepitch/pesdodge.hpp"

#include <array>
#include <cmath>
#include <cstring>

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
  // The three table addresses, each through its referencing instruction.
  uint64_t kindVa = 0, gridVa = 0, bandVa = 0;
  if (!ReadRipTarget(exe, KIND_TABLE, &kindVa) || !ReadRipTarget(exe, BALL_GRID, &gridVa) ||
      !ReadRipTarget(exe, BAND_TABLE, &bandVa)) {
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

}  // namespace pes

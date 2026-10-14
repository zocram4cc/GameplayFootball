#include "pesjudge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace pes {
namespace {

// .rdata floats (OBS, docs/re/pes-re-duels-fouls.md 2.3/2.4).
constexpr uint64_t ANGLE_FLOOR_VA = 0x14259be2c;        // 40.0, also W
constexpr uint64_t ANGLE_SPAN_VA = 0x14259be64;         // 140.0
constexpr uint64_t SLIDE_ANGLE_SPAN_VA = 0x14256ae30;   // 120.0
constexpr uint64_t SLIDE_ANGLE_KNEE_VA = 0x142596e6c;   // 80.0
constexpr uint64_t NOT_FALLEN_FACTOR_VA = 0x14257a194;  // 0.7
constexpr uint64_t KMH_PER_MPS_VA = 0x14259ce38;        // 3.6
constexpr uint64_t SPEED_FLOOR_VA = 0x142565604;        // 2.0
constexpr uint64_t SPEED_SPAN_VA = 0x1425a2004;         // 28.0
constexpr uint64_t SPEED_MAX_POINTS_VA = 0x142591140;   // 30.0
constexpr uint64_t SPOT_BIAS_VA = 0x1425cc238;          // 0.06
constexpr uint64_t SLIDE_GATE_RAMP_VA = 0x14259222c;    // 5.0
constexpr uint64_t SLIDE_ALIGN_KNEE_VA = 0x14259be30;   // 45.0
constexpr uint64_t SLIDE_ALIGN_RAMP_VA = 0x14259be84;   // -10.0
constexpr uint64_t WHISTLE_HOLD_VA = 0x142572348;       // 0.8 (FUN_14050cd70's mulss)

// The chief referee's restart marks (docs/re/pes-re-protection.md 3.3). Shared
// .rdata literals: one VA serves every handler that uses that value.
constexpr uint64_t HALF_VA = 0x142565600;         // 0.5
constexpr uint64_t TWO_VA = 0x142565604;          // 2.0
constexpr uint64_t THREE_VA = 0x14258c264;        // 3.0
constexpr uint64_t FOUR_VA = 0x1425656c0;         // 4.0
constexpr uint64_t FIVE_VA = 0x14259222c;         // 5.0
constexpr uint64_t EIGHT_VA = 0x14259bdf4;        // 8.0
constexpr uint64_t TEN_VA = 0x142588210;          // 10.0
constexpr uint64_t FIFTEEN_VA = 0x14259be0c;      // 15.0
constexpr uint64_t QUARTER_VA = 0x142592228;      // 0.25
constexpr uint64_t ONE_AND_HALF_VA = 0x14259bddc;  // 1.5
constexpr uint64_t THREE_AND_HALF_VA = 0x14259bde8;  // 3.5
constexpr uint64_t TWO_AND_HALF_VA = 0x14259bde4;  // 2.5
constexpr uint64_t MINUS_FOUR_VA = 0x1425c7678;   // -4.0
constexpr uint64_t MINUS_FOURTEEN_VA = 0x1425c7220;  // -14.0

// Offside detection (docs/re/pes-re-offside.md 3/4). Shared .rdata literals
// reuse the chief-referee constants for the same values.
constexpr uint64_t OFFSIDE_MARGIN_TORSO_VA = 0x1425c48a0;   // 0.15: the head/torso margin, and the tolerance
constexpr uint64_t OFFSIDE_MARGIN_ARM_VA = SPOT_BIAS_VA;    // 0.06
constexpr uint64_t OFFSIDE_MARGIN_TOE_VA = 0x142588200;     // 0.10
constexpr uint64_t OFFSIDE_ARM_TOLERANCE_VA = 0x142590d98;  // 0.30
// The defender rear points' shifts, stored signed towards the own goal.
constexpr uint64_t OFFSIDE_REAR_SHIFT_TORSO_VA = 0x14259d64c;  // -0.15
constexpr uint64_t OFFSIDE_REAR_SHIFT_ARM_VA = 0x14259d644;    // -0.06
constexpr uint64_t OFFSIDE_REAR_SHIFT_TOE_VA = 0x14259d648;    // -0.10
constexpr uint64_t OFFSIDE_NEAR_BAND_VA = 0x14256ae1c;   // 1.0
constexpr uint64_t OFFSIDE_CLEAR_BAND_VA = FOUR_VA;      // 4.0

// Instructions whose immediates carry integer weights: {VA, opcode bytes,
// immediate offset}. The opcode bytes are checked before the immediate is read,
// so a different build fails loudly instead of yielding a wrong number.
struct Imm {
  uint64_t va;
  std::initializer_list<uint8_t> opcode;
  int offset;
};
constexpr Imm RECKLESS_POINTS{0x1405b6e0f, {0x40, 0xb6}, 2};          // mov sil, 35
constexpr Imm TACKLE_POINTS{0x1405b6dfc, {0x40, 0xb6}, 2};            // mov sil, 25
constexpr Imm RECKLESS_FALLEN_BONUS{0x1405b6e5a, {0xb0}, 1};          // mov al, 15
constexpr Imm TACKLE_FALLEN_BONUS{0x1405b6e6a, {0xb8}, 1};            // mov eax, 10
constexpr Imm DOGSO_BONUS{0x1405b6f4a, {0x40, 0x80, 0xc6}, 3};        // add sil, 30
constexpr Imm THRESHOLDS{0x143fa8816, {0x66, 0xc7, 0x07}, 3};         // mov word [rdi], T2:T1
constexpr Imm RED_THRESHOLD{0x143fa87f8, {0xc6, 0x47, 0x02}, 3};      // mov byte [rdi+2], T3
constexpr Imm BOX_WORD_LOAD{0x143fa8802, {0x66, 0x44, 0x8b, 0x3d}, 4};  // mov r15w, [rip+d]
constexpr Imm BOX_WORD_KEY{0x143fa880a, {0x66, 0x41, 0x81, 0xf7}, 4};  // xor r15w, key
constexpr Imm FALLEN_MIN_LOAD{0x143fa8557, {0x8b, 0x35}, 2};          // mov esi, [rip+d]
constexpr Imm FALLEN_MIN_KEY{0x143fa855d, {0x81, 0xf6}, 2};           // xor esi, key
constexpr Imm JOSTLE_MAGIC{0x143fa857c, {0xb8}, 1};                   // mov eax, 2^(32+s)/20
constexpr Imm JOSTLE_SHIFT{0x143fa8583, {0xc1, 0xea}, 2};             // shr edx, s
constexpr Imm ADVANTAGE_SHIFT{0x14050b4fe, {0xc1, 0xe0}, 2};          // shl eax, s (gs << s)

bool ReadFloat(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
}

template <typename T>
bool ReadImm(const PeImage& exe, const Imm& imm, T* out, std::string* error) {
  uint8_t code[8] = {};
  const size_t n = imm.opcode.size();
  if (!exe.ReadVa(imm.va, n, code) || !std::equal(imm.opcode.begin(), imm.opcode.end(), code) ||
      !exe.ReadVa(imm.va + imm.offset, sizeof *out, reinterpret_cast<uint8_t*>(out))) {
    char va[32];
    std::snprintf(va, sizeof va, "%#llx", static_cast<unsigned long long>(imm.va));
    *error = std::string("judge constant: unexpected instruction at ") + va;
    return false;
  }
  return true;
}

// The protected words: `mov reg, [rip+disp]` then `xor reg, key`, exactly as the
// game decodes them.
template <typename T>
bool ReadProtected(const PeImage& exe, const Imm& load, const Imm& key, T* out,
                   std::string* error) {
  int32_t disp = 0;
  T word = 0, xorKey = 0;
  if (!ReadImm(exe, load, &disp, error) || !ReadImm(exe, key, &xorKey, error)) return false;
  const uint64_t next = load.va + load.offset + sizeof disp;
  if (!exe.ReadVa(next + disp, sizeof word, reinterpret_cast<uint8_t*>(&word))) {
    *error = "judge constant: protected word unreadable";
    return false;
  }
  *out = word ^ xorKey;
  return true;
}

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

}  // namespace

std::optional<JudgeConstants> LoadJudgeConstants(const PeImage& exe, std::string* error) {
  JudgeConstants k{};
  bool ok = true;
  for (auto [va, out] : {std::pair{ANGLE_FLOOR_VA, &k.angleFloor_deg},
                         {ANGLE_FLOOR_VA, &k.angleMaxPoints},
                         {ANGLE_SPAN_VA, &k.angleSpan_deg},
                         {SLIDE_ANGLE_SPAN_VA, &k.slideAngleSpan_deg},
                         {SLIDE_ANGLE_KNEE_VA, &k.slideAngleKnee_deg},
                         {NOT_FALLEN_FACTOR_VA, &k.notFallenFactor},
                         {KMH_PER_MPS_VA, &k.kmhPerMetrePerSecond},
                         {SPEED_FLOOR_VA, &k.speedFloor_kmh},
                         {SPEED_SPAN_VA, &k.speedSpan_kmh},
                         {SPEED_MAX_POINTS_VA, &k.speedMaxPoints},
                         {SPOT_BIAS_VA, &k.spotBias_m},
                         {SLIDE_GATE_RAMP_VA, &k.slideGateRamp_deg},
                         {SLIDE_ALIGN_KNEE_VA, &k.slideAlignKnee_deg},
                         {SLIDE_ALIGN_RAMP_VA, &k.slideAlignRamp_deg},
                         {WHISTLE_HOLD_VA, &k.whistleHold_s},
                         {OFFSIDE_MARGIN_TORSO_VA, &k.offsideMarginTorso_m},
                         {OFFSIDE_MARGIN_ARM_VA, &k.offsideMarginArm_m},
                         {OFFSIDE_MARGIN_TOE_VA, &k.offsideMarginToe_m},
                         {OFFSIDE_ARM_TOLERANCE_VA, &k.offsideArmTolerance_m},
                         {OFFSIDE_REAR_SHIFT_TORSO_VA, &k.offsideRearShiftTorso_m},
                         {OFFSIDE_REAR_SHIFT_ARM_VA, &k.offsideRearShiftArm_m},
                         {OFFSIDE_REAR_SHIFT_TOE_VA, &k.offsideRearShiftToe_m},
                         {OFFSIDE_NEAR_BAND_VA, &k.offsideNearBand_m},
                         {OFFSIDE_CLEAR_BAND_VA, &k.offsideClearBand_m},
                         {FIVE_VA, &k.refereeSpot.kickOffBehind_m},
                         {TEN_VA, &k.refereeSpot.kickOffAcross_m},
                         {THREE_VA, &k.refereeSpot.throwInUpfieldGate_m},
                         {FIVE_VA, &k.refereeSpot.throwInTrail_m},
                         {TEN_VA, &k.refereeSpot.throwInLeadDivisor},
                         {EIGHT_VA, &k.refereeSpot.throwInLeadBase_m},
                         {FOUR_VA, &k.refereeSpot.throwInInset_m},
                         {MINUS_FOUR_VA, &k.refereeSpot.goalKickX_m},
                         {TWO_AND_HALF_VA, &k.refereeSpot.goalKickDrop_m},
                         {HALF_VA, &k.refereeSpot.cornerInset_m},
                         {TWO_VA, &k.refereeSpot.cornerOut_m},
                         {FOUR_VA, &k.refereeSpot.penaltyAhead_m},
                         {FIVE_VA, &k.refereeSpot.penaltyOut_m},
                         {HALF_VA, &k.refereeSpot.fkHalf},
                         {MINUS_FOURTEEN_VA, &k.refereeSpot.fkDeepX_m},
                         {TWO_VA, &k.refereeSpot.fkOwnHalfOut_m},
                         {THREE_VA, &k.refereeSpot.fkOwnBoxX_m},
                         {THREE_VA, &k.refereeSpot.fkOwnBoxIn_m},
                         {THREE_VA, &k.refereeSpot.fkWideDivisor},
                         {FOUR_VA, &k.refereeSpot.fkBoxCornerInset_m},
                         {TWO_VA, &k.refereeSpot.fkBoxCornerOut_m},
                         {QUARTER_VA, &k.refereeSpot.fkQuarter},
                         {THREE_VA, &k.refereeSpot.fkEdgeInset_m},
                         {THREE_AND_HALF_VA, &k.refereeSpot.fkEdgeOut_m},
                         {ONE_AND_HALF_VA, &k.refereeSpot.fkWideOut_m},
                         {TEN_VA, &k.refereeSpot.fkFarAcross_m},
                         {TEN_VA, &k.refereeSpot.fkCentreAhead_m},
                         {EIGHT_VA, &k.refereeSpot.fkCentreAcross_m},
                         {FIVE_VA, &k.refereeSpot.fkFlankAhead_m},
                         {FIFTEEN_VA, &k.refereeSpot.fkFlankAcross_m}})
    ok &= ReadFloat(exe, va, out);
  if (!ok) {
    *error = "judge constant: .rdata float unreadable";
    return std::nullopt;
  }

  uint8_t b = 0;
  uint32_t u32 = 0;
  uint16_t thresholds = 0, boxThresholds = 0;
  uint32_t magic = 0;
  uint8_t shift = 0;
  if (!ReadImm(exe, RECKLESS_POINTS, &b, error)) return std::nullopt;
  k.recklessPoints = b;
  if (!ReadImm(exe, TACKLE_POINTS, &b, error)) return std::nullopt;
  k.tacklePoints = b;
  if (!ReadImm(exe, RECKLESS_FALLEN_BONUS, &b, error)) return std::nullopt;
  k.recklessFallenBonus = b;
  if (!ReadImm(exe, TACKLE_FALLEN_BONUS, &u32, error)) return std::nullopt;
  k.tackleFallenBonus = static_cast<int>(u32);
  if (!ReadImm(exe, DOGSO_BONUS, &b, error)) return std::nullopt;
  k.dogsoBonus = b;
  if (!ReadImm(exe, THRESHOLDS, &thresholds, error)) return std::nullopt;
  if (!ReadImm(exe, RED_THRESHOLD, &b, error)) return std::nullopt;
  k.redThreshold = b;
  if (!ReadProtected(exe, BOX_WORD_LOAD, BOX_WORD_KEY, &boxThresholds, error)) return std::nullopt;
  if (!ReadProtected(exe, FALLEN_MIN_LOAD, FALLEN_MIN_KEY, &u32, error)) return std::nullopt;
  k.fallenMinSpeedPoints = static_cast<int>(u32);
  if (!ReadImm(exe, JOSTLE_MAGIC, &magic, error) || !ReadImm(exe, JOSTLE_SHIFT, &shift, error))
    return std::nullopt;
  uint8_t advantageShift = 0;
  if (!ReadImm(exe, ADVANTAGE_SHIFT, &advantageShift, error)) return std::nullopt;
  // gs << s frames at gs frames per second is 2^s seconds.
  k.advantageWindow_s = std::ldexp(1.0f, advantageShift);
  // `mul magic; shr edx, s` divides by round(2^(32+s) / magic).
  k.jostleSpeedDivisor =
      static_cast<int>(std::lround(std::ldexp(1.0, 32 + shift) / static_cast<double>(magic)));

  // The word holds T1 in its low byte and T2 in its high byte.
  constexpr int BYTE_BITS = 8;
  constexpr unsigned BYTE_MASK = 0xff;
  k.foulThreshold = thresholds & BYTE_MASK;
  k.cautionThreshold = thresholds >> BYTE_BITS;
  k.boxFoulThreshold = boxThresholds & BYTE_MASK;
  if ((boxThresholds >> BYTE_BITS) != static_cast<unsigned>(k.cautionThreshold)) {
    *error = "judge constant: the in-box caution threshold differs from the open-play one";
    return std::nullopt;
  }
  return k;
}

int JudgePoints(const JudgeContact& c, const JudgeConstants& k) {
  // P_angle (report 2.3). A jostle without the push flag scores no angle.
  float p = 0.0f;
  if (c.action == JudgeAction::Slide) {
    // Above the knee the angle is pulled back towards it when B slid across
    // A's line (g1) while both ran the same way (g2).
    const float g1 = Clamp01((c.slideOffLine_deg - k.angleFloor_deg) / k.slideGateRamp_deg);
    const float g2 =
        Clamp01((c.headingsDelta_deg - k.slideAlignKnee_deg) / k.slideAlignRamp_deg);
    float theta = c.approachAngle_deg;
    if (theta > k.slideAngleKnee_deg) theta += g1 * g2 * (k.slideAngleKnee_deg - theta);
    p = Clamp01((theta - k.angleFloor_deg) / k.slideAngleSpan_deg);
  } else if (c.action != JudgeAction::Jostle) {
    // The default ramp: tackles of the general sub-kind and plain contacts.
    p = Clamp01((c.approachAngle_deg - k.angleFloor_deg) / k.angleSpan_deg);
  }
  const float w = c.victimFell ? k.angleMaxPoints : k.angleMaxPoints * k.notFallenFactor;
  const int pAngle = static_cast<int>(p * w);

  // P_speed.
  const float v_kmh = c.relativeSpeed_mps * k.kmhPerMetrePerSecond;
  const float s = v_kmh >= k.speedMaxPoints ? 1.0f
                  : v_kmh <= k.speedFloor_kmh ? 0.0f
                                             : (v_kmh - k.speedFloor_kmh) / k.speedSpan_kmh;
  int pSpeed = static_cast<int>(s * k.speedMaxPoints);
  if (c.victimFell) pSpeed = std::max(pSpeed, k.fallenMinSpeedPoints);
  if (c.action == JudgeAction::Jostle) pSpeed /= k.jostleSpeedDivisor;

  // P_action.
  int pAction = 0;
  if (c.action == JudgeAction::Slide) {
    pAction = k.recklessPoints + (c.victimFell ? k.recklessFallenBonus : 0);
  } else if (c.action == JudgeAction::Tackle) {
    pAction = k.tacklePoints + (c.victimFell ? k.tackleFallenBonus : 0);
  } else if (c.victimFell) {
    // A jostle or a plain contact that put the man down: the same +10.
    pAction = k.tackleFallenBonus;
  }
  // DOGSO +30 when the victim fell and the challenge was in the reckless
  // class. PES also fires it on the deliberate-intent flag `ai(B)+0x134`, but
  // that flag means "a pad-controlled player pressed for this challenge"
  // (docs/re/pes-re-defender-decisions.md 4: its only writer is the publisher
  //'s pad-table scan, and AI-vs-AI leaves every slot unbound). It is never
  // set for a CPU player, so in this engine there is no intent bonus at all -
  // the reckless class is the only half of PES's OR that survives.
  if (c.victimFell && c.dogso && c.action == JudgeAction::Slide) pAction += k.dogsoBonus;

  // The game sums into a u8.
  constexpr int U8_MAX = 255;
  return std::min(pAngle + pSpeed + pAction, U8_MAX);
}

int JudgeLevelFromPoints(int points, bool inAttackingBox, const JudgeConstants& k) {
  const int t1 = inAttackingBox ? k.boxFoulThreshold : k.foulThreshold;
  const int t2 = k.cautionThreshold, t3 = k.redThreshold;
  if (points < t1) return 0;
  if (points < t2) return points > t1 + (t2 - t1) / 2 ? 2 : 1;
  return points < t3 ? 3 : 4;
}

int JudgeLevel(const JudgeContact& c, const JudgeConstants& k) {
  int level = JudgeLevelFromPoints(JudgePoints(c, k), c.inAttackingBox, k);
  // Report 2.5 step 6: in the penalty area a foul needs the victim on the
  // ground; no card for a penalty unless DOGSO, and DOGSO red drops to yellow.
  constexpr int YELLOW = 3, RED = 4, FOUL = 1;
  if (c.inAttackingBox && level != 0) {
    if (!c.victimFell) return 0;
    if (!c.dogso && level >= YELLOW) level = FOUL;
    if (c.dogso && level == RED) level = YELLOW;
  }
  return level;
}

bool InAdvantageDecisionWindow(float sinceContact_s, const JudgeConstants& k) {
  constexpr float LAST_THIRD = 2.0f / 3.0f;
  return sinceContact_s >= k.whistleHold_s * LAST_THIRD && sinceContact_s < k.whistleHold_s;
}

bool ShouldPlayAdvantage(int level, bool penalty, bool foulerBooked, float fouledNearest_m,
                         float foulingNearest_m) {
  constexpr int YELLOW = 3, RED = 4;
  if (penalty || level >= RED) return false;
  if (level == YELLOW && foulerBooked) return false;
  // ponytail: PES also runs an attack-chance test (FUN_14050f7e0) for
  // yellow-level fouls and fallen victims; not ported, the distance test alone
  // decides.
  return fouledNearest_m <= foulingNearest_m;
}

float AdvanceAdvantage(float remaining_s, float dt_s, bool fouledTeamHasBall,
                       bool fouledTeamPassed) {
  constexpr float PASS_COST_S = 1.0f;  // `adv_timer -= gs`: one second of frames
  remaining_s -= fouledTeamHasBall ? 2.0f * dt_s : dt_s;
  if (fouledTeamPassed) remaining_s -= PASS_COST_S;
  return std::max(remaining_s, 0.0f);
}

namespace {

// PES's sign: zero counts as positive (`v < 0 ? -1 : 1` in every handler).
float Sign(float v) { return v < 0.0f ? -1.0f : 1.0f; }

// The free kick's zones in the attacking half (0x144161790). Two of the labels
// (4 and 7) come out of XOR-protected words in the game; they are case labels,
// not tuning values.
enum class FreeKickZone { BoxCornerWide = 1, LevelWithTheBox, Touchline, Central, InsideFar,
                          InsideNear, FarWide, FarInsideFar, FarInsideNear };

FreeKickZone ZoneOf(PitchSpot r, const FieldMarks& f, const RefereeSpotConstants& k) {
  const float ax = std::fabs(r.x), az = std::fabs(r.z);
  const bool nearSide = r.z * Sign(r.x) >= 0.0f;
  if (ax >= f.penaltyLineX_m)
    return az < f.penaltyHalfWidth_m ? FreeKickZone::LevelWithTheBox : FreeKickZone::BoxCornerWide;
  if (ax < f.penaltyLineX_m * k.fkHalf) {
    if (az >= f.penaltyHalfWidth_m) return FreeKickZone::FarWide;
    return nearSide ? FreeKickZone::FarInsideNear : FreeKickZone::FarInsideFar;
  }
  if (az >= f.penaltyHalfWidth_m) return FreeKickZone::Touchline;
  if (az <= f.penaltyHalfWidth_m / k.fkWideDivisor) return FreeKickZone::Central;
  return nearSide ? FreeKickZone::InsideNear : FreeKickZone::InsideFar;
}

std::optional<PitchSpot> FreeKickSpot(PitchSpot r, int attackDir, const FieldMarks& f,
                                      const RefereeSpotConstants& k) {
  const float sx = Sign(r.x), sz = Sign(r.z);
  if (static_cast<float>(attackDir) * r.x <= 0.0f) {  // in the kickers' own half
    const float ax = std::fabs(r.x);
    if (ax >= f.penaltyLineX_m)
      return PitchSpot{k.fkOwnBoxX_m * sx, (f.goalAreaHalfWidth_m - k.fkOwnBoxIn_m) * sz};
    // The game's own `S + S` near the centre line.
    const float x = ax < f.penaltyLineX_m * k.fkHalf ? k.fkDeepX_m * sx : sx + sx;
    return PitchSpot{x, (f.goalAreaHalfWidth_m + k.fkOwnHalfOut_m) * sz};
  }
  switch (ZoneOf(r, f, k)) {
    case FreeKickZone::BoxCornerWide:
      return PitchSpot{(f.penaltyLineX_m - k.fkBoxCornerInset_m) * sx,
                       -(f.goalAreaHalfWidth_m + k.fkBoxCornerOut_m) * sx};
    case FreeKickZone::LevelWithTheBox:
      return std::nullopt;
    case FreeKickZone::Touchline:
      return PitchSpot{f.halfLength_m * k.fkQuarter * sx,
                       sz * f.penaltyHalfWidth_m / k.fkWideDivisor};
    case FreeKickZone::Central:
    case FreeKickZone::InsideNear:
      return PitchSpot{(f.penaltyLineX_m - k.fkEdgeInset_m) * sx,
                       -(f.goalAreaHalfWidth_m + k.fkEdgeOut_m) * sx};
    case FreeKickZone::InsideFar:
      return PitchSpot{(f.penaltyLineX_m + k.fkWideOut_m) * sx,
                       -(f.penaltyHalfWidth_m + k.fkWideOut_m) * sx};
    case FreeKickZone::FarWide:
      return PitchSpot{r.x + sx + sx, r.z - k.fkFarAcross_m * sz};
    case FreeKickZone::FarInsideFar:
      return PitchSpot{r.x + k.fkCentreAhead_m * sx, r.z - k.fkCentreAcross_m * sx};
    case FreeKickZone::FarInsideNear:
      return PitchSpot{r.x + k.fkFlankAhead_m * sx, r.z - k.fkFlankAcross_m * sx};
  }
  return std::nullopt;
}

PitchSpot ThrowInSpot(PitchSpot r, int attackDir, const FieldMarks& f,
                      const RefereeSpotConstants& k) {
  const float dir = static_cast<float>(attackDir), sz = Sign(r.z);
  PitchSpot at{r.x * dir > k.throwInUpfieldGate_m
                   ? r.x - dir * k.throwInTrail_m
                   : (std::fabs(r.x) / k.throwInLeadDivisor + k.throwInLeadBase_m) * dir + r.x,
               (f.penaltyHalfWidth_m - k.throwInInset_m) * sz};
  const float boxFront = f.penaltyLineX_m - k.throwInInset_m;
  if (boxFront <= dir * at.x) at = {boxFront * dir, sz * f.goalAreaHalfWidth_m};
  return at;
}

}  // namespace

std::optional<PitchSpot> ChiefRefereeSpot(e_SetPiece kind, PitchSpot r, int attackDir,
                                          const FieldMarks& f, const RefereeSpotConstants& k) {
  const float dir = static_cast<float>(attackDir), sx = Sign(r.x);
  switch (kind) {
    case e_SetPiece_KickOff:
      return PitchSpot{-dir * k.kickOffBehind_m, -dir * k.kickOffAcross_m};
    case e_SetPiece_ThrowIn:
      return ThrowInSpot(r, attackDir, f, k);
    case e_SetPiece_GoalKick:
      return PitchSpot{sx * k.goalKickX_m, Sign(r.z) * f.goalAreaHalfWidth_m - k.goalKickDrop_m};
    case e_SetPiece_Corner:
      return PitchSpot{(f.penaltyLineX_m - k.cornerInset_m) * sx,
                       -(f.goalAreaHalfWidth_m + k.cornerOut_m) * sx};
    case e_SetPiece_FreeKick:
      return FreeKickSpot(r, attackDir, f, k);
    case e_SetPiece_Penalty:
      return PitchSpot{(f.penaltyLineX_m + k.penaltyAhead_m) * sx,
                       -(f.goalAreaHalfWidth_m + k.penaltyOut_m) * sx};
    case e_SetPiece_None:
      break;
  }
  return std::nullopt;
}

}  // namespace pes

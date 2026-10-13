#include "pesstamina.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <utility>

namespace pes {
namespace {

// .rdata floats (OBS, docs/re/pes-re-stamina-drain.md 2-6).
constexpr uint64_t ARROW_TOP_VA = 0x14260b390;         // 6000
constexpr uint64_t ARROW_GREAT_VA = 0x1425cb004;       // 2000
constexpr uint64_t ARROW_POOR_VA = 0x1425cb498;        // -4000
constexpr uint64_t ARROW_TERRIBLE_VA = 0x1425cb4a0;    // -8000
constexpr uint64_t TANK_FLOOR_VA = 0x14259d638;        // 16000
constexpr uint64_t A5_FLOOR_PCT_VA = 0x142588210;      // 10
constexpr uint64_t A5_FULL_PCT_VA = 0x14259be4c;       // 70
constexpr uint64_t A5_SPAN_PCT_VA = 0x14259a764;       // 60
constexpr uint64_t A5_BASE_VA = 0x142572348;           // 0.8
constexpr uint64_t A5_SCALE_VA = 0x142598578;          // 0.2
constexpr uint64_t BREATH_PENALTY_FULL_PCT_VA = 0x14259be1c;  // 20
constexpr uint64_t BREATH_PENALTY_SPAN_VA = 0x1425a946c;      // -20
constexpr uint64_t BREATH_FRAME_VA = 0x142588214;      // 48
constexpr uint64_t BREATH_BASE_VA = 0x14259bdec;       // 6
constexpr uint64_t BREATH_DASH_VA = 0x14259d60c;       // 9
constexpr uint64_t BREATH_MIN_SPEED_VA = 0x14259bdd0;  // 0.694444
constexpr uint64_t BREATH_SPEED_KMH_VA = 0x1425c48c0;  // 22
constexpr uint64_t HALF_TIME_BONUS_VA = 0x142632a6c;   // 9000
constexpr uint64_t HALF_T12_BONUS_VA = 0x142598590;    // 3600
constexpr uint64_t HALF_T36_BONUS_VA = 0x142632a68;    // 7200
constexpr uint64_t EXTRA_TIME_BONUS_VA = 0x142632a64;  // 5400
constexpr uint64_t EXTRA_T12_BONUS_VA = 0x142632a60;   // 1800
constexpr uint64_t CLASS_WALK_VA = 0x1425a8a28;        // 0.05
constexpr uint64_t CLASS_JOG_VA = 0x14259bdf4;         // 8
constexpr uint64_t CLASS_RUN_VA = 0x1425a1ffc;         // 13.5
constexpr uint64_t CLASS_DASH_VA = 0x1425c48c0;        // 22
constexpr uint64_t GAME_SPEED_VA = 0x14351ae50;        // 54, gsInt
constexpr uint64_t KPH_PER_MPS_VA = 0x14259ce38;       // 3.6
constexpr uint64_t INJ_SPEED_SCALE_VA = 0x14259be20;   // 25
constexpr uint64_t INJ_BASE_VA = 0x14257a194;          // 0.7
constexpr uint64_t INJ_SPAN_VA = 0x142590d98;          // 0.3
constexpr uint64_t INJ_BASE_DOWN_VA = 0x14253ee30;     // 100
constexpr uint64_t INJ_BASE_UP_VA = 0x14259be34;       // 50
constexpr uint64_t INJ_ATT8_VA = 0x142572348;          // 0.8
constexpr uint64_t INJ_ATT5_VA = 0x142565600;          // 0.5
constexpr uint64_t INJ_SLOW_VA = 0x14259bdd4;          // 0.2
constexpr uint64_t INJ_ANGLE_VA = 0x142598580;         // 180
constexpr uint64_t INJ_CAP_VA = 0x14258b550;           // 255
constexpr uint64_t INJ_KNOCK_VA = 0x14259be68;         // 150
constexpr uint64_t INJ_INJURED_VA = 0x1425a2668;       // 200
constexpr uint64_t INJ_JACKPOT_VA = 0x14259be58;       // 85

// Instruction immediates (OBS): each is checked against its opcode bytes before
// its value is trusted, so a different build fails loudly.
struct Imm {
  uint64_t va;
  std::initializer_list<uint8_t> opcode;
  int offset;
};

// A rip-relative .rdata load: the opcode bytes, the displacement's offset from
// the instruction start, and the instruction's total length (the displacement
// ends there, which is where the target address is computed from).
struct Rel {
  uint64_t va;
  std::initializer_list<uint8_t> opcode;
  int dispOffset;
  int length;
};

// FUN_14092fad0's per-class -M (0x14092fd1f..0x14092fd49): `mov r14b, imm8`.
constexpr Imm DRAIN_STAND{0x14092fd1f, {0x41, 0xb6}, 2};  // mov r14b, 0xf7  (-9)
constexpr Imm DRAIN_WALK{0x14092fd49, {0xb8}, 1};  // mov eax, 0xfffffff9 (-7)
constexpr Imm DRAIN_JOG{0x14092fd3d, {0x41, 0xb6}, 2};   // mov r14b, 0xfc  (-4)
constexpr Imm DRAIN_RUN{0x14092fd33, {0x41, 0xb6}, 2};   // mov r14b, 4     (+4)
constexpr Imm DRAIN_DASH{0x14092fd29, {0x41, 0xb6}, 2};  // mov r14b, 7     (+7)
// FUN_14092b950's breath terms (0x14092b9ba..0x14092ba8f): rip-relative .rdata
// loads, one each, each checked against its opcode bytes before its
// displacement is followed.
constexpr Rel BREATH_R{0x14092b9ba, {0xf3, 0x44, 0x0f, 0x10}, 5, 9};  // movss xmm10, 48
constexpr Rel BREATH_VEL_FLOOR{0x14092b9d5, {0xf3, 0x0f, 0x10}, 4, 8};  // movss xmm2, 0.694444
constexpr Rel BREATH_BASE{0x14092b9ea, {0xf3, 0x0f, 0x59}, 4, 8};       // mulss xmm0, 6
constexpr Rel BREATH_SPEED_KMH{0x14092ba4e, {0x44, 0x0f, 0x2f}, 4, 8};  // comiss xmm8, 22
constexpr Rel BREATH_DASH_COST{0x14092ba5f, {0xf3, 0x0f, 0x59}, 4, 8};  // mulss xmm0, 9
// FUN_1409556e0's drain: `imul eax, edi, 0x3c` is the *60/gs scale, and the
// per-class gain adds the +P half of spd (0x14092feb3..0x14092fec8).
constexpr Imm DRAIN_CLASS0_GAIN{0x14092feb3, {0x83, 0xc7}, 2};  // add edi, 0xa  (+10)
constexpr Imm DRAIN_CLASS2_GAIN{0x14092febd, {0x83, 0xc7}, 2};  // add edi, 5    (+5)
constexpr Imm DRAIN_CLASS3_LOSS{0x14092fec2, {0x8d, 0x47}, 2};  // lea eax, [rdi-1] (-1)
constexpr Imm DRAIN_CLASS4_LOSS{0x14092fec5, {0x83, 0xef}, 2};  // sub edi, 2    (-2)
constexpr Imm DRAIN_SCALE_TICK{0x14092fed5, {0x6b, 0xc7}, 2};   // imul eax, edi, 0x3c (60)
// FUN_140922dd0's ability scaling and the tank's 400.
constexpr Imm ABILITY_400{0x140922eec, {0x69, 0xc0}, 2};  // imul eax, eax, 0x190
constexpr Imm ABILITY_70{0x140922eca, {0x6b, 0xc8}, 2};   // imul ecx, eax, 0x6e
constexpr Imm ABILITY_80{0x140922ebd, {0x6b, 0xc8}, 2};   // imul ecx, eax, 0x78
constexpr Imm ABILITY_90{0x140922ead, {0x69, 0xc8}, 2};   // imul ecx, eax, 0x82
constexpr Imm ABILITY_95{0x140922e9d, {0x69, 0xc8}, 2};   // imul ecx, eax, 0x8c
// FUN_140481610's resistance roll and the jackpot.
constexpr Imm INJ_RESIST_A{0x140481863, {0xbb}, 1};  // mov ebx, 0x46 (70)
constexpr Imm INJ_RESIST_B{0x14048186a, {0xbb}, 1};  // mov ebx, 0x32 (50)
constexpr Imm INJ_RESIST_C{0x140481871, {0xbb}, 1};  // mov ebx, 0x1e (30)
constexpr Imm BREATH_CAP{0x140a581b0, {0xb8}, 1};  // mov eax, 0x780 (1920)

template <typename T>
bool ReadImm(const PeImage& exe, const Imm& imm, T* out, std::string* error) {
  uint8_t code[8] = {};
  const size_t n = imm.opcode.size();
  if (!exe.ReadVa(imm.va, n, code) ||
      !std::equal(imm.opcode.begin(), imm.opcode.end(), code) ||
      !exe.ReadVa(imm.va + imm.offset, sizeof *out, reinterpret_cast<uint8_t*>(out))) {
    char va[32];
    std::snprintf(va, sizeof va, "%#llx", static_cast<unsigned long long>(imm.va));
    *error = std::string("stamina constant: unexpected instruction at ") + va;
    return false;
  }
  return true;
}

// Reads the .rdata value a rip-relative instruction loads: the displacement is
// added to the address just past the instruction, and the value is read there.
template <typename T>
bool ReadRel(const PeImage& exe, const Rel& rel, T* out, std::string* error) {
  uint8_t code[8] = {};
  const size_t n = rel.opcode.size();
  int32_t disp = 0;
  if (!exe.ReadVa(rel.va, n, code) ||
      !std::equal(rel.opcode.begin(), rel.opcode.end(), code) ||
      !exe.ReadVa(rel.va + rel.dispOffset, sizeof disp,
                  reinterpret_cast<uint8_t*>(&disp))) {
    char va[32];
    std::snprintf(va, sizeof va, "%#llx", static_cast<unsigned long long>(rel.va));
    *error = std::string("stamina constant: unexpected instruction at ") + va;
    return false;
  }
  const uint64_t target = rel.va + rel.length + static_cast<int64_t>(disp);
  return exe.ReadVa(target, sizeof *out, reinterpret_cast<uint8_t*>(out));
}

bool ReadFloatList(const PeImage& exe, std::initializer_list<std::pair<uint64_t, float*>> items) {
  bool ok = true;
  for (auto [va, out] : items)
    ok &= exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
  return ok;
}

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

}  // namespace

std::optional<StaminaConstants> LoadStaminaConstants(const PeImage& exe, std::string* error) {
  StaminaConstants k{};
  if (!ReadFloatList(exe, {{ARROW_TOP_VA, &k.arrowTopBonus},
                           {ARROW_GREAT_VA, &k.arrowGreatBonus},
                           {ARROW_POOR_VA, &k.arrowPoorPenalty},
                           {ARROW_TERRIBLE_VA, &k.arrowTerriblePenalty},
                           {TANK_FLOOR_VA, &k.tankFloor},
                           {A5_FLOOR_PCT_VA, &k.a5FloorPct},
                           {A5_FULL_PCT_VA, &k.a5FullPct},
                           {A5_SPAN_PCT_VA, &k.a5SpanPct},
                           {A5_BASE_VA, &k.a5Base},
                           {A5_SCALE_VA, &k.a5Scale},
                           {BREATH_PENALTY_FULL_PCT_VA, &k.breathPenaltyFullPct},
                           {BREATH_PENALTY_SPAN_VA, &k.breathPenaltyDivisor},
                           {BREATH_FRAME_VA, &k.breathFrameRate},
                           {BREATH_BASE_VA, &k.breathBaseTicks},
                           {BREATH_DASH_VA, &k.breathDashCostTicks},
                           {BREATH_SPEED_KMH_VA, &k.breathDashSpeed_kmh},
                           {HALF_TIME_BONUS_VA, &k.halfTimeBonus},
                           {HALF_T12_BONUS_VA, &k.halfTimeT12Bonus},
                           {HALF_T36_BONUS_VA, &k.halfTimeT36Bonus},
                           {EXTRA_TIME_BONUS_VA, &k.extraTimeBonus},
                           {EXTRA_T12_BONUS_VA, &k.extraTimeT12Bonus},
                           {CLASS_WALK_VA, &k.speedClassThresholdWalk_kmh},
                           {CLASS_JOG_VA, &k.speedClassThresholdJog_kmh},
                           {CLASS_RUN_VA, &k.speedClassThresholdRun_kmh},
                           {CLASS_DASH_VA, &k.speedClassThresholdDash_kmh},
                           {KPH_PER_MPS_VA, &k.breathKmhPerMs},
                           {INJ_SPEED_SCALE_VA, &k.injurySpeedScale_kmh},
                           {INJ_BASE_VA, &k.injurySpeedFloor},
                           {INJ_SPAN_VA, &k.injurySpeedSpan},
                           {INJ_BASE_DOWN_VA, &k.injuryBaseDown},
                           {INJ_BASE_UP_VA, &k.injuryBaseUp},
                           {INJ_ATT8_VA, &k.injuryAttackerSlide},
                           {INJ_ATT5_VA, &k.injuryAttackerSlideFromRun},
                           {INJ_SLOW_VA, &k.injurySlowFactor},
                           {INJ_ANGLE_VA, &k.injuryAngleDivisor},
                           {INJ_CAP_VA, &k.injuryCap},
                           {INJ_KNOCK_VA, &k.injuryKnockAcc},
                           {INJ_INJURED_VA, &k.injuryInjuredAcc},
                           {INJ_JACKPOT_VA, &k.injuryJackpotSev}})) {
    *error = "PES2021.exe: stamina .rdata constants unmapped";
    return std::nullopt;
  }
  // gsInt is a dword the image holds as 54, not a float: FUN_141e532e0 reads
  // it with `mov eax, dword ptr [rip+d]` and the drain divides by it.
  int32_t gameSpeedInt = 0;
  if (!exe.ReadVa(GAME_SPEED_VA, sizeof gameSpeedInt,
                  reinterpret_cast<uint8_t*>(&gameSpeedInt)) ||
      gameSpeedInt <= 0) {
    *error = "PES2021.exe: game speed divisor unmapped";
    return std::nullopt;
  }
  k.gameSpeedIntDivisor = static_cast<float>(gameSpeedInt);
  // The breath cap's 1920 (`mov eax, 0x780` at FUN_140a581b0), read out of code.
  if (!ReadImm(exe, BREATH_CAP, &k.breathCapScale, error)) return std::nullopt;
  // imul eax, eax, 0x190 carries an imm32; the other four ability scalers
  // and the drain's *60 are `imul r32, r32, imm8` and carry an imm8, so they
  // are read as a byte - reading four bytes there would take the following
  // instruction's bytes into the number.
  // `imul r32, r32, imm8` (the 0x6b form) carries a byte; `imul r32, r32,
  // imm32` (the 0x69 form) carries four. 110 and 120 are the 0x6b form, 130
  // and 140 the 0x69 form, so their widths differ.
  int ability400 = 0, ability90 = 0, ability95 = 0;
  int8_t ability70 = 0, ability80 = 0;
  int8_t drain60 = 0;
  int resistA = 0, resistB = 0, resistC = 0;
  int8_t drainStand = 0, drainWalk = 0, drainJog = 0, drainRun = 0, drainDash = 0;
  int8_t class0 = 0, class2 = 0, class3 = 0, class4 = 0;
  // The breath terms: 48/gs's frame rate, the 6 base, the 9 dash cost, the
  // 2 ball-holder cost, the 0.694444 m/tick velocity floor and the 22 km/h
  // dash test, each read out of the instruction that carries it.
  float breathFrame = 0.0f, breathBase = 0.0f, breathDash = 0.0f;
  float breathKmhPerMs = 0.0f;
  if (!ReadRel(exe, BREATH_R, &breathFrame, error) ||
      !ReadRel(exe, BREATH_BASE, &breathBase, error) ||
      !ReadRel(exe, BREATH_DASH_COST, &breathDash, error) ||
      !ReadRel(exe, BREATH_VEL_FLOOR, &breathKmhPerMs, error) ||
      !ReadRel(exe, BREATH_SPEED_KMH, &k.breathDashSpeed_kmh, error) ||
      !ReadImm(exe, ABILITY_400, &ability400, error) ||
      !ReadImm<int8_t>(exe, ABILITY_70, &ability70, error) ||
      !ReadImm<int8_t>(exe, ABILITY_80, &ability80, error) ||
      !ReadImm(exe, ABILITY_90, &ability90, error) ||
      !ReadImm(exe, ABILITY_95, &ability95, error) ||
      !ReadImm<int8_t>(exe, DRAIN_SCALE_TICK, &drain60, error) ||
      !ReadImm(exe, INJ_RESIST_A, &resistA, error) ||
      !ReadImm(exe, INJ_RESIST_B, &resistB, error) ||
      !ReadImm(exe, INJ_RESIST_C, &resistC, error) ||
      !ReadImm<int8_t>(exe, DRAIN_STAND, &drainStand, error) ||
      !ReadImm<int8_t>(exe, DRAIN_WALK, &drainWalk, error) ||
      !ReadImm<int8_t>(exe, DRAIN_JOG, &drainJog, error) ||
      !ReadImm<int8_t>(exe, DRAIN_RUN, &drainRun, error) ||
      !ReadImm<int8_t>(exe, DRAIN_DASH, &drainDash, error) ||
      !ReadImm<int8_t>(exe, DRAIN_CLASS0_GAIN, &class0, error) ||
      !ReadImm<int8_t>(exe, DRAIN_CLASS2_GAIN, &class2, error) ||
      !ReadImm<int8_t>(exe, DRAIN_CLASS3_LOSS, &class3, error) ||
      !ReadImm<int8_t>(exe, DRAIN_CLASS4_LOSS, &class4, error)) {
    return std::nullopt;
  }
  k.breathFrameRate = breathFrame;
  k.breathBaseTicks = breathBase;
  k.breathDashCostTicks = breathDash;
  k.drainScale_tick = static_cast<float>(drain60);
  // The ball-holder cost: 0x14092ba7e's `addss xmm10, xmm10` doubles the
  // frame rate r into 2*r for the ball holder, which is the report's
  // "-2*r" term. Doubling r is the cost, so the coefficient is 2.
  k.breathBallHolderCost = 2.0f;
  // The velocity floor PES's motion object applies, in km/h as the breath
  // test compares it: the read value is 0.694444 m per tick, which the km/h
  // conversion puts at 2.5 km/h. A player who only shuffles his feet still
  // refills.
  k.breathMinSpeed_kmh = breathKmhPerMs * k.breathKmhPerMs;
  k.injuryResistanceThr[0] = resistC;
  k.injuryResistanceThr[1] = resistB;
  k.injuryResistanceThr[2] = resistA;
  // The arrow bonuses are magnitudes in .rdata; 0x140922f12/0x140922f1f's
  // `subss` makes the 1 and 3 rows negative, 0x140922f33/0x140922f40's
  // `addss` the 0 and 4 rows positive. Applied here so the tank's switch
  // above only has to add.
  k.arrowPoorPenalty = -k.arrowPoorPenalty;
  k.arrowTerriblePenalty = -k.arrowTerriblePenalty;
  // spd = -M + P, the per-class speed term:
  //   class 0/1/2 gain +10 / +10 / +5 (add edi, imm)
  //   class 3/4 lose -1 / -2 (lea eax,[rdi-1] / sub edi,2, chosen by
  //               `cmp ebp,3 / cmovle`)
  // P is the immediate of the instruction the class lands on, signed as the
  // instruction's own form writes it: class 3's `lea` carries -1, class 4's
  // `sub` carries +2, so both are added to -M and the sign comes along.
  k.drainPerSpeed[0] = -drainStand + class0;
  k.drainPerSpeed[1] = -drainWalk + class0;
  k.drainPerSpeed[2] = -drainJog + class2;
  k.drainPerSpeed[3] = -drainRun + class3;
  k.drainPerSpeed[4] = -drainDash - class4;
  // The structural integers of the report, each PES's own literal at the site
  // named in the comment.
  k.abilityScaleUnit = ability400;
  k.abilityScale70_79 = ability70;
  k.abilityScale80_89 = ability80;
  k.abilityScale90_94 = ability90;
  k.abilityScale95_99 = ability95;
  k.abilityScaleDivisor = 100;   // the sar edx, 5 divide of the ability scaling
  k.abilityFloor = 5;            // cmp dl, 5
  k.freshnessFloor = 10;         // mov r8d, 0xa
  k.breathCapFloor_pct = 20;     // the max(pct, 20) of the breath cap
  k.breathCapDivisor = 100;      // * 1920 / 100
  k.drainBreathFullBonus = 5;    // the breath term's 5x in R
  k.drainNoBallControlCost = 2;  // F20: own side controls the ball
  k.drainAiFlagCost = 3;         // -3 * F14 (sub edx, 3), unmodelled below
  k.drainEmptyFloor = 10;        // cmp edi, 0xa
  k.breathPenaltyScale = 10.0f;  // the breath penalty's 10
  k.injuryJackpotSevCap = 200;   // sev = 200
  k.injuryJackpotThr[0] = 5;    // C: 5 %
  k.injuryJackpotThr[1] = 2;    // B: 2 %
  k.injuryJackpotThr[2] = 0;    // A: never
  k.injuryRngSlot = 2;          // RangeRaw(rng, 100, 2)
  k.injuryRngModulus = 100;     // the draw is Range(rng, 100, 2)
  return k;
}

void InitStamina(const StaminaConstants& k, int staminaAbility, int conditionArrow, int fitness,
                 float matchLengthFactor, int freshnessPct, StaminaState* s) {
  // s' (report 2): the ability's tiered scaling, integer.
  int scaled = staminaAbility;
  if (scaled < k.abilityFloor) {
    scaled = k.abilityFloor;
  } else if (scaled >= 95) {
    scaled = scaled * k.abilityScale95_99 / k.abilityScaleDivisor;
  } else if (scaled >= 90) {
    scaled = scaled * k.abilityScale90_94 / k.abilityScaleDivisor;
  } else if (scaled >= 80) {
    scaled = scaled * k.abilityScale80_89 / k.abilityScaleDivisor;
  } else if (scaled >= 70) {
    scaled = scaled * k.abilityScale70_79 / k.abilityScaleDivisor;
  }
  scaled = scaled & 0xff;

  // max = max(16000*F, s'*400*F + a*F + (T == 0 ? 8000*F : 0)).
  const float f = matchLengthFactor;
  float max = static_cast<float>(scaled * k.abilityScaleUnit) * f;
  switch (conditionArrow) {
    case 0:
      max += f * k.arrowTerriblePenalty;
      break;
    case 1:
      max += f * k.arrowPoorPenalty;
      break;
    case 2:
      break;
    case 3:
      max += f * k.arrowGreatBonus;
      break;
    default:
      max += f * k.arrowTopBonus;
      break;
  }
  if (fitness == 0) max += f * k.fitnessBonus;
  max = std::max(f * k.tankFloor, max);

  s->max = static_cast<int>(max);
  s->cur = static_cast<int>(s->max * std::max(freshnessPct, k.freshnessFloor) / 100);
  if (s->cur > s->max) s->cur = s->max;
  const int pct = s->max == 0 ? 0 : s->cur * 100 / s->max;
  s->breath = std::max(pct, k.breathCapFloor_pct) * k.breathCapScale / k.breathCapDivisor;
  s->lastSeverity = 0;
  s->accumulation = 0;
  s->injuryLevel = 0;
}

int BreathCap(const StaminaConstants& k, const StaminaState& s) {
  const int pct = s.max == 0 ? 0 : s.cur * 100 / s.max;
  return std::max(pct, k.breathCapFloor_pct) * k.breathCapScale / k.breathCapDivisor;
}

void UpdateBreath(const StaminaConstants& k, const StaminaState& s, float speed_kmh,
                  bool isBallHolder, int* breath) {
  // Report 3.2: r = 48 / gs, d = (int)(6*r). The speed test is
  // gs * 60 * 60 * |v| / 1000 > 22 km/h on the 3-D velocity, whose magnitude
  // is floored at 0.694444/gs m/tick. PES works in metres per tick, this
  // engine in metres per second: the gs in the floor and the gs in the test
  // cancel, so the test is simply `speed_kmh > 22` with the floor standing at
  // 2.5 km/h - a player who only shuffles his feet still refills.
  const float r = k.breathFrameRate / k.gameSpeedIntDivisor;
  int d = static_cast<int>(k.breathBaseTicks * r);
  const float speedKmh = std::max(speed_kmh, k.breathMinSpeed_kmh);
  if (speedKmh > k.breathDashSpeed_kmh)
    d = static_cast<int>(d - k.breathDashCostTicks * r);
  if (isBallHolder) d = static_cast<int>(d - k.breathBallHolderCost * r);
  // One PES tick's delta per 1/54 s scales to one GF tick's per 0.01 s the
  // same way the drain does, truncated toward zero.
  const double perPesTick = static_cast<double>(d);
  const int perGfTick = static_cast<int>(std::trunc(perPesTick * kGfTick_s / kPesTick_s));
  const int next = std::max(0, *breath + perGfTick);
  *breath = std::min(next, BreathCap(k, s));
}

int SpeedClass(const StaminaConstants& k, float speed_kmh) {
  if (speed_kmh <= k.speedClassThresholdWalk_kmh) return 0;
  if (speed_kmh <= k.speedClassThresholdJog_kmh) return 1;
  if (speed_kmh <= k.speedClassThresholdRun_kmh) return 2;
  if (speed_kmh <= k.speedClassThresholdDash_kmh) return 3;
  return 4;
}

int DrainTick(const StaminaConstants& k, const StaminaState& s, int speedClass, int breathPct,
              bool sideHasBall) {
  const int cls = std::clamp(speedClass, 0, kSpeedClassCount - 1);
  // The breath term: `(signed char)bp / 100`, 1 only when the breath is full.
  const int breathTerm = breathPct / 100;
  // R, read off 0x14092fe21..0x14092feb8 term by term. Flags F12, F13, F14,
  // F16, G and K are the ones the report leaves INF; none is modelled here,
  // so S = 0 and only F20/F68 survive.
  //   edx = (int)(gs * 50 + 0.5) * (teamFlag == 1)   -> K = 0 (the per-slot
  //        "exhausted" marker is never set for a normal outfield player)
  //   ecx = G + 5*F12 + (st == 0x34) + F16 + K = 0
  //   r8d = F34 + ecx = 0, then r8d += F16 = 0, r8d *= 2 -> 2*S = 0
  //   eax = F20 + F13 = f20, eax *= 2 -> 2*f20
  //   edx = ecx + edx - 2*S - 3 = -3            (report's `-3`)
  //   edi = 5*edx - 2*(F20 + F13) - 3*F14 - M
  //       - F68 + P                                (0x14092feae class gain)
  // F20 is the action id 3 (this player controls the ball) and F68 the "+1"
  // drain the live series shows holding even for the attacking side; the three
  // states 0x14092fc51..0x14092fc7c leave both 0 only when the side neither
  // holds the ball nor is tagged, so the modellable pair is:
  //   sideHasBall: F20 = 1, F68 = 0
  //   otherwise:   F20 = 0, F68 = 1
  const int f20 = sideHasBall ? 1 : 0;
  const int f68 = sideHasBall ? 0 : 1;
  // -M: the per-class magnitude, +P the class gain at 0x14092feb3..0x14092fec8.
  // The table already holds -M + P, so the two are applied as one term.
  const int spd = k.drainPerSpeed[cls];
  const int r = 5 * (breathTerm - 3) - 2 * f20 - f68 + spd;
  // `R * 60 / gs` is the PES tick's delta in tank units per 1/54 s, truncated
  // toward zero exactly as the C idiv does. One GF tick carries
  // kGfTick_s / kPesTick_s = 0.54 of a PES tick's rate, so a second of GF
  // ticks is a second of PES ticks (100 * 0.54 = 54), and the second trunc
  // lands on the scaled integer.
  const int perPesTick = static_cast<int>(
      std::trunc(static_cast<double>(r) * k.drainScale_tick / k.gameSpeedIntDivisor));
  const int scaled = static_cast<int>(std::trunc(perPesTick * kGfTick_s / kPesTick_s));
  const int next = s.cur + scaled;
  // cur < 10 -> 0, and SetCurrent clamps to the tank.
  return next < k.drainEmptyFloor ? 0 : std::min(next, s.max);
}

float FatigueFactor(const StaminaConstants& k, const StaminaState& s) {
  // An empty tank has no percentage: a player whose state was never
  // initialised (the officials, who have no tank at all) reads as fresh.
  if (s.max == 0) return 1.0f;
  const int pct = s.cur * 100 / s.max;
  float t = 0.0f;
  if (pct > static_cast<int>(k.a5FloorPct)) {
    t = pct >= static_cast<int>(k.a5FullPct)
            ? 1.0f
            : (static_cast<float>(pct) - k.a5FloorPct) / k.a5SpanPct;
  }
  return k.a5Base + k.a5Scale * t;
}

int BreathPenalty(const StaminaConstants& k, const StaminaState& s) {
  const int bp = s.breath;
  if (bp <= 0) return static_cast<int>(k.breathPenaltyScale);
  if (bp >= static_cast<int>(k.breathPenaltyFullPct)) return 0;
  return static_cast<int>((bp - k.breathPenaltyFullPct) / k.breathPenaltyDivisor *
                          k.breathPenaltyScale);
}

int BreakBonus(const StaminaConstants& k, const StaminaState& s, int period, int fitness,
               float matchLengthFactor) {
  // PES keeps a running float and adds the base times F, then the fitness
  // bonus times F, casting down at the very end (FUN_140923060: `ebx` starts
  // as the current tank, `mulss` on a float accumulator, `cvttss2si` once).
  // With F = 10 those are 90000 and 36000/72000, which PES's live jumps of
  // ~90k / 126k / 162k confirm. The tank itself is over 2^24 here, so the
  // same two-step dance runs in double: a float's 24-bit mantissa would round
  // 552000 + 90000 up instead of down.
  double cur = static_cast<double>(s.cur);
  if (period == 2) {
    cur = cur + static_cast<double>(k.halfTimeBonus) * matchLengthFactor;
    if (fitness >= 1 && fitness <= 2)
      cur = cur + static_cast<double>(k.halfTimeT12Bonus) * matchLengthFactor;
    else if (fitness >= 3 && fitness <= 6)
      cur = cur + static_cast<double>(k.halfTimeT36Bonus) * matchLengthFactor;
  } else if (period == 4) {
    cur = cur + static_cast<double>(k.extraTimeBonus) * matchLengthFactor;
    if (fitness >= 1 && fitness <= 2)
      cur = cur + static_cast<double>(k.extraTimeT12Bonus) * matchLengthFactor;
  }
  return static_cast<int>(std::min(std::trunc(cur), static_cast<double>(s.max)));
}

float ClampRamp(float x, float floor, float span) {
  if (x < floor) return 0.0f;
  return x >= span ? 1.0f : (x - floor) / span;
}

int ContactSeverity(const StaminaConstants& k, int resistance, bool victimDown,
                    float victimSpeed_kmh, float attackerSpeed_kmh, bool attackerSlid,
                    bool attackerSlidFromRun, float approachAngle_deg, Random* rng) {
  float sev = victimDown ? k.injuryBaseDown : k.injuryBaseUp;
  // Both speeds use the same 0.7 + 0.3 * clamp(speed / 25, 0, 1) ramp.
  sev *= (k.injurySpeedFloor +
          k.injurySpeedSpan * ClampRamp(attackerSpeed_kmh, 0.0f, k.injurySpeedScale_kmh));
  sev *= (k.injurySpeedFloor +
          k.injurySpeedSpan * ClampRamp(victimSpeed_kmh, 0.0f, k.injurySpeedScale_kmh));
  // The attacker's action: 11..12 slides 0.8, 13..14 slides from a run 0.5,
  // anything else 0.5 (the _DAT_142565600 spill on both branches).
  if (attackerSlid)
    sev *= k.injuryAttackerSlide;
  else
    sev *= k.injuryAttackerSlideFromRun;
  // The approach angle, and the standing-still attacker's flat 0.2: below
  // 1 km/h the angle is skipped (the jbe keeps the 0.2), a negative angle is
  // kept (the jae only divides back a positive one over 180), an angle past
  // 180 divides back over it (a lunge past the man hurts no worse than 180).
  if (attackerSpeed_kmh >= 1.0f) {
    const float angle = approachAngle_deg >= 180.0f ? 1.0f : approachAngle_deg / 180.0f;
    sev *= k.injurySlowFactor + k.injuryHalf * angle;
  } else {
    sev *= k.injurySlowFactor;
  }
  // The resistance roll halves the severity.
  const int thr = k.injuryResistanceThr[std::clamp(resistance, 0, 2)];
  if (rng->RangeRaw(k.injuryRngSlot, k.injuryRngModulus) < thr) sev *= 0.5f;
  sev = std::clamp(sev, 0.0f, k.injuryCap);
  return static_cast<int>(sev);
}

int RecordContact(const StaminaConstants& k, StaminaState* s, int resistance, bool victimDown,
                  float victimSpeed_kmh, float attackerSpeed_kmh, bool attackerSlid,
                  bool attackerSlidFromRun, float approachAngle_deg, Random* rng) {
  int sev = ContactSeverity(k, resistance, victimDown, victimSpeed_kmh, attackerSpeed_kmh,
                            attackerSlid, attackerSlidFromRun, approachAngle_deg, rng);
  // The jackpot: a heavy contact, and the second roll fails for C or B.
  if (sev > static_cast<int>(k.injuryJackpotSev)) {
    const int thr = k.injuryJackpotThr[std::clamp(resistance, 0, 2)];
    if (thr > 0 && rng->RangeRaw(k.injuryRngSlot, k.injuryRngModulus) < thr) sev = k.injuryJackpotSevCap;
  }
  s->lastSeverity = sev;
  s->accumulation = std::min(static_cast<int>(k.injuryCap), s->accumulation + sev);
  if (s->injuryLevel == 0) {
    if (s->accumulation >= static_cast<int>(k.injuryInjuredAcc))
      s->injuryLevel = 3;
    else if (s->accumulation >= static_cast<int>(k.injuryKnockAcc))
      s->injuryLevel = 2;
  } else if (s->injuryLevel == 2 && s->accumulation >= static_cast<int>(k.injuryInjuredAcc)) {
    s->injuryLevel = 3;
  }
  return sev;
}

}  // namespace pes

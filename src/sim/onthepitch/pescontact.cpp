#include "onthepitch/pescontact.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "onthepitch/pesdefenderdecisions.hpp"

namespace pes {
namespace {

// contact.o payload offsets (pes-re-contact-2.md §2.2).
constexpr uint32_t FROM_BEHIND_ENABLE_OFF = 0x00;
constexpr uint32_t FORCED_SPEED_BODY_OFF = 0x04;
constexpr uint32_t DF_LEG_PLAIN_OFF = 0x08, DF_LEG_SLIDE_OFF = 0x0c, DF_LEG_TACKLE_OFF = 0x10;
constexpr uint32_t DS_LEG_PLAIN_OFF = 0x14, DS_LEG_SLIDE_OFF = 0x18, DS_LEG_TACKLE_OFF = 0x1c;
constexpr uint32_t FORCED_DISP_OFF = 0x24, FORCED_DISP_TACKLING_OFF = 0x2c;
constexpr uint32_t VF_OFF = 0x30, VS_OFF = 0x34;
constexpr uint32_t DF_BODY_PLAIN_OFF = 0x38, DF_BODY_TACKLE_OFF = 0x3c;
constexpr uint32_t DS_BODY_PLAIN_OFF = 0x40, DS_BODY_TACKLE_OFF = 0x44;

// .rdata.
constexpr uint64_t LEVEL_TABLE_VA = 0x1425efbf0;
constexpr uint64_t LEVEL_ACTION_VA = 0x1425efc18;
constexpr uint64_t KEEPER_DS_VA = 0x142588200, KEEPER_DF_VA = 0x142598578;
constexpr uint64_t KEEPER_VS_VA = 0x14259bdf0, KEEPER_VF_VA = 0x14259be0c;
constexpr uint64_t COMMITTED_SCALE_VA = 0x1425ab128;
constexpr uint64_t ABILITY_FLOOR_VA = 0x14259be2c, ABILITY_SPAN_VA = 0x1425cb2fc;
constexpr uint64_t CLASH_GAIN_VA = 0x14258c264;
constexpr uint64_t CLASH_TRAP_FALL_VA = 0x14259be20, CLASH_TRAP_STAGGER_VA = 0x1425c48c0;
constexpr uint64_t CLASH_PARTNER_VA = 0x14259d5d0, CLASH_BALL_CLEAR_VA = 0x142592228;
constexpr uint64_t APPROACH_ANGLE_VA = 0x1425cb30c, BEHIND_ANGLE_VA = 0x14259be60;
constexpr uint64_t RUNNER_SPEED_VA = 0x1425a1ffc;
constexpr uint64_t VETO_REL_VA = 0x142588210, VETO_STILL_EXTRA_VA = 0x14259be04;
constexpr uint64_t VETO_STILL_VA = 0x14259222c, VETO_HOLD_EXTRA_VA = 0x14259be7c;
constexpr uint64_t VETO_FACING_VA = 0x14259a764;
constexpr uint64_t GAUGE_SIDE_MIN_VA = 0x14259be30, GAUGE_SIDE_MAX_VA = 0x14259be60;
constexpr uint64_t GAUGE_SIDE_SPAN_VA = 0x14259be5c, GAUGE_BASE_VA = 0x14259bddc;
constexpr uint64_t GAUGE_DIVISOR_VA = 0x14256ae30, GAUGE_HALF_VA = 0x142565600;
constexpr uint64_t GAUGE_ONE_VA = 0x14256ae1c, KMH_PER_MPS_VA = 0x14259ce38;
// FUN_1408caf60's per-stat (min, max) byte pairs; stat 0x28 = Physical Contact.
constexpr uint64_t STAT_BOUNDS_VA = 0x142613c60;
constexpr uint64_t PHYSICAL_CONTACT_STAT = 0x28;

// Instruction immediates of FUN_1406f9b70, opcode-checked.
struct Imm {
  uint64_t va;
  std::array<uint8_t, 3> opcode;
  size_t opcodeLen;
  size_t offset;
};
constexpr Imm GAUGE_FULL_IMM = {0x1406f9df2, {0x41, 0xbc}, 2, 2};   // mov r12d, 1e6
constexpr Imm GAUGE_DRAIN_IMM = {0x1406fa122, {0xb8}, 1, 1};        // mov eax, -1e6
// lea ecx, [rax + rax*2]: the refill divisor 3*fps is an addressing scale,
// not an immediate, so the bytes are checked and the scale (SIB 0x40 = *2,
// plus the base) is the number.
constexpr Imm GAUGE_REFILL_LEA = {0x1406f9ded, {0x8d, 0x0c, 0x40}, 3, 0};
constexpr int GAUGE_REFILL_LEA_SCALE = 3;

bool Matches(const PeImage& exe, const Imm& imm) {
  uint8_t code[3] = {};
  return exe.ReadVa(imm.va, imm.opcodeLen, code) &&
         std::equal(imm.opcode.begin(), imm.opcode.begin() + imm.opcodeLen, code);
}

bool ReadImm32(const PeImage& exe, const Imm& imm, int* out) {
  return Matches(exe, imm) &&
         exe.ReadVa(imm.va + imm.offset, sizeof *out, reinterpret_cast<uint8_t*>(out));
}

inline float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

bool IsLeg(ContactPart p) { return p != ContactPart::kBody; }

}  // namespace

std::optional<ContactConstants> LoadContactConstants(const PeImage& exe, const PesVfs& vfs,
                                                     std::string* error) {
  ContactConstants k;
  std::vector<uint8_t> rec;
  if (!ReadConstantPlayerObject(vfs, "contact.o", &rec, error)) return std::nullopt;
  if (rec.size() < DS_BODY_TACKLE_OFF + sizeof(float)) {
    *error = "contact.o: record shorter than its threshold block";
    return std::nullopt;
  }
  auto f = [&rec](uint32_t off) {
    float v;
    std::memcpy(&v, &rec[off], sizeof v);
    return v;
  };
  k.fromBehindFallEnabled = rec[FROM_BEHIND_ENABLE_OFF] != 0;
  k.forcedFallSpeedBody_kmh = f(FORCED_SPEED_BODY_OFF);
  // t = (body) + 2*mode; t5 (body slide) overruns into the body plain pair
  // (report §1.4, the 12-float array at 0x140842fe6).
  const uint32_t dsOff[6] = {DS_LEG_PLAIN_OFF, DS_BODY_PLAIN_OFF, DS_LEG_TACKLE_OFF,
                             DS_BODY_TACKLE_OFF, DS_LEG_SLIDE_OFF, DS_BODY_PLAIN_OFF};
  const uint32_t dfOff[6] = {DF_LEG_PLAIN_OFF, DF_BODY_PLAIN_OFF, DF_LEG_TACKLE_OFF,
                             DF_BODY_TACKLE_OFF, DF_LEG_SLIDE_OFF, DF_BODY_PLAIN_OFF};
  for (int t = 0; t < 6; ++t) {
    k.dS_m[t] = f(dsOff[t]);
    k.dF_m[t] = f(dfOff[t]);
  }
  k.vF_kmh = f(VF_OFF);
  k.vS_kmh = f(VS_OFF);
  k.forcedFallDisplacement_m = f(FORCED_DISP_OFF);
  k.forcedFallDisplacementTackling_m = f(FORCED_DISP_TACKLING_OFF);

  bool ok = exe.ReadVa(LEVEL_TABLE_VA, sizeof k.levelTable,
                       reinterpret_cast<uint8_t*>(k.levelTable)) &&
            exe.ReadVa(LEVEL_ACTION_VA, sizeof k.levelAction,
                       reinterpret_cast<uint8_t*>(k.levelAction));
  const std::pair<uint64_t, float*> floats[] = {
      {KEEPER_DS_VA, &k.keeperDS_m},
      {KEEPER_DF_VA, &k.keeperDF_m},
      {KEEPER_VS_VA, &k.keeperVS_kmh},
      {KEEPER_VF_VA, &k.keeperVF_kmh},
      {COMMITTED_SCALE_VA, &k.committedScale},
      {ABILITY_FLOOR_VA, &k.abilityFloor},
      {ABILITY_SPAN_VA, &k.abilitySpan},
      {CLASH_GAIN_VA, &k.clashAbilityGain_kmh},
      {CLASH_TRAP_FALL_VA, &k.clashTrapFall_kmh},
      {CLASH_TRAP_STAGGER_VA, &k.clashTrapStagger_kmh},
      {CLASH_PARTNER_VA, &k.clashPartner_m},
      {CLASH_BALL_CLEAR_VA, &k.clashBallClear_m},
      {APPROACH_ANGLE_VA, &k.approachAngle_deg},
      {BEHIND_ANGLE_VA, &k.behindAngle_deg},
      {RUNNER_SPEED_VA, &k.runnerSpeed_kmh},
      {VETO_REL_VA, &k.vetoRelativeSpeed_kmh},
      {VETO_STILL_EXTRA_VA, &k.vetoStillExtra_kmh},
      {VETO_STILL_VA, &k.vetoStill_kmh},
      {VETO_HOLD_EXTRA_VA, &k.vetoHoldExtra_kmh},
      {VETO_FACING_VA, &k.vetoFacing_deg},
      {GAUGE_SIDE_MIN_VA, &k.gaugeSideMin_deg},
      {GAUGE_SIDE_MAX_VA, &k.gaugeSideMax_deg},
      {GAUGE_SIDE_SPAN_VA, &k.gaugeSideSpan_deg},
      {GAUGE_BASE_VA, &k.gaugeBase_s},
      {GAUGE_DIVISOR_VA, &k.gaugeAbilityDivisor},
      {GAUGE_HALF_VA, &k.gaugeHalf},
      {GAUGE_ONE_VA, &k.gaugeOne},
      {KMH_PER_MPS_VA, &k.kmhPerMps},
  };
  for (const auto& [va, out] : floats)
    ok &= exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
  uint8_t bounds[2] = {};
  ok &= exe.ReadVa(STAT_BOUNDS_VA + PHYSICAL_CONTACT_STAT * 2, sizeof bounds, bounds);
  k.gaugeContactMin = bounds[0];
  k.gaugeContactMax = bounds[1];
  ok &= ReadImm32(exe, GAUGE_FULL_IMM, &k.gaugeFull) &&
        ReadImm32(exe, GAUGE_DRAIN_IMM, &k.gaugeDrainNumerator) &&
        Matches(exe, GAUGE_REFILL_LEA);
  k.gaugeRefillSeconds = GAUGE_REFILL_LEA_SCALE;
  if (!ok) {
    *error = "contact: PES2021.exe constants unmapped or unexpected instruction";
    return std::nullopt;
  }
  k.fallAction = k.levelAction[3];
  return k;
}

namespace {

// FUN_140844540 (the branches GF can express): true = level 3, no veto.
bool ForcedFall(const ContactConstants& k, const ContactInput& in, int tableLevel) {
  // A slide whose leg (or body) meets the self above the knee or leg on leg,
  // coming from within the approach window of its direction.
  if (in.mode == ContactOpponentMode::kSlide) {
    const bool aboveKnee = in.selfPart == ContactPart::kBody;
    const bool legOnLeg = IsLeg(in.selfPart) && IsLeg(in.oppPart);
    if ((aboveKnee || legOnLeg) && in.slideApproach_deg < k.approachAngle_deg) return true;
  }
  // From behind: switched by contact.o's byte (0 in stock and the 4cc mod).
  if (k.fromBehindFallEnabled && in.oppTackling && (IsLeg(in.selfPart) || tableLevel > 0) &&
      in.oppBehind_deg > k.behindAngle_deg)
    return true;
  const float bar =
      in.selfTackling ? k.forcedFallDisplacementTackling_m : k.forcedFallDisplacement_m;
  return in.displacement_m > bar;
}

// The nine predicates of 0x14348cab0 that GF can express; true = level 0.
bool Vetoed(const ContactConstants& k, const ContactInput& in) {
  // #3 0x140841ac0: the opponent's tackle is not yet at its contact frame.
  if (in.oppTackling && in.oppBeforeContactFrame) return true;
  // #6 0x140842330: the opponent is running away.
  if (!in.oppTackling && in.oppAway_deg > k.approachAngle_deg &&
      in.oppSpeed_kmh > k.runnerSpeed_kmh)
    return true;
  // #7 0x1408419e0: both in locomotion.
  if (in.selfLocomotion && in.oppLocomotion) return true;
  // #8 0x1408424c0: a slow touch.
  bool still = false;
  float threshold = k.vetoRelativeSpeed_kmh;
  if (in.selfTrapping) {
    threshold += k.vetoHoldExtra_kmh;
  } else if (in.selfSpeed_kmh < k.vetoStill_kmh) {
    threshold += k.vetoStillExtra_kmh;
    still = true;
  }
  if (in.relativeSpeed_kmh < threshold) {
    if (still) return true;
    const bool alike =
        in.headingDelta_deg < k.vetoFacing_deg || in.movementDelta_deg < k.vetoFacing_deg;
    if (alike && (in.relativeSpeed_kmh < k.vetoStill_kmh ||
                  (in.selfLocomotion && in.oppLocomotion)))
      return true;
  }
  return false;
}

}  // namespace

int ContactLevel(const ContactConstants& k, const ContactInput& in) {
  const bool body = in.selfPart == ContactPart::kBody;
  const int t = (body ? 1 : 0) + 2 * static_cast<int>(in.mode);
  float dS = k.dS_m[t], dF = k.dF_m[t], vS = k.vS_kmh, vF = k.vF_kmh;
  float forced = k.forcedFallSpeedBody_kmh;
  if (in.oppKeeperHandling) {
    dS = k.keeperDS_m;
    dF = k.keeperDF_m;
    vS = k.keeperVS_kmh;
    vF = k.keeperVF_kmh;
  }
  if (in.mode != ContactOpponentMode::kSlide && in.selfCommitted) {
    const float s = k.committedScale;
    dS *= s;
    dF *= s;
    vS *= s;
    vF *= s;
    forced += forced * s;
  }
  int dl = in.displacement_m < dS ? 0 : (in.displacement_m < dF ? 1 : 2);
  const int vl = in.closingSpeed_kmh < vS ? 0 : (in.closingSpeed_kmh < vF ? 1 : 2);
  if (body && in.closingSpeed_kmh > forced) dl = 2;
  int level = k.levelTable[dl * 3 + vl];

  // Receiving-body clash: Physical Contact raises the speed a receiver absorbs.
  if (in.selfTrapping && body && !in.selfStaggering && in.partnerDistance_m < k.clashPartner_m &&
      in.selfBallDistance_m > k.clashBallClear_m) {
    const float p =
        k.clashAbilityGain_kmh * Clamp01((in.selfContactAbility - k.abilityFloor) / k.abilitySpan);
    level = in.closingSpeed_kmh > p + k.clashTrapFall_kmh
                ? 3
                : (in.closingSpeed_kmh > p + k.clashTrapStagger_kmh ? 2 : 0);
  }

  if (ForcedFall(k, in, level)) return 3;

  // A running carrier alongside an opponent heading his way is only staggered.
  if (in.selfOwnsBall && in.headingDelta_deg < k.approachAngle_deg &&
      in.selfSpeed_kmh > k.runnerSpeed_kmh)
    level = std::min(level, 2);

  if (level > 0 && Vetoed(k, in)) level = 0;
  return std::clamp(level, 0, 3);
}

ContactReaction ReactionForLevel(const ContactConstants& k, int level) {
  if (level <= 0) return ContactReaction::kNone;
  return k.levelAction[std::min(level, 3)] == k.fallAction ? ContactReaction::kFall
                                                           : ContactReaction::kStagger;
}

int UpdateBalanceGauge(const ContactConstants& k, const BalanceGaugeInput& in, int gauge,
                       int framesPerSecond) {
  int delta = k.gaugeFull / (k.gaugeRefillSeconds * framesPerSecond);
  const auto side = [&k](float bearing) {
    return bearing > k.gaugeSideMin_deg && bearing < k.gaugeSideMax_deg;
  };
  if (in.touching && in.bothLocomotion && in.selfSpeed_kmh > k.runnerSpeed_kmh &&
      in.partnerSpeed_kmh > k.runnerSpeed_kmh && side(in.selfBearing_deg) &&
      side(in.partnerBearing_deg)) {
    const float a = (in.selfBearing_deg - k.gaugeSideMin_deg) / k.gaugeSideSpan_deg;
    const float r = k.gaugeContactMax > k.gaugeContactMin
                        ? float(in.selfContactAbility - k.gaugeContactMin) /
                              float(k.gaugeContactMax - k.gaugeContactMin)
                        : 0.0f;
    const float seconds =
        k.gaugeBase_s +
        float(in.selfContactAbility - in.partnerContactAbility) / k.gaugeAbilityDivisor +
        (a + a - k.gaugeOne) * k.gaugeHalf * (k.gaugeOne - r * k.gaugeHalf);
    const int frames = static_cast<int>(framesPerSecond * seconds + k.gaugeHalf);
    if (frames > 0) delta = k.gaugeDrainNumerator / frames;
  }
  return std::min(gauge + delta, k.gaugeFull);
}

}  // namespace pes

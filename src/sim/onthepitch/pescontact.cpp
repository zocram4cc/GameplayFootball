#include "onthepitch/pescontact.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
constexpr uint32_t FORCED_DISP_OFF = 0x24, FORCED_DISP_UPPER_OFF = 0x28;
constexpr uint32_t FORCED_DISP_TACKLING_OFF = 0x2c;
constexpr uint32_t VF_OFF = 0x30, VS_OFF = 0x34;
constexpr uint32_t DF_BODY_PLAIN_OFF = 0x38, DF_BODY_TACKLE_OFF = 0x3c;
constexpr uint32_t DS_BODY_PLAIN_OFF = 0x40, DS_BODY_TACKLE_OFF = 0x44;

// .rdata.
constexpr uint64_t LEVEL_TABLE_VA = 0x1425efbf0;
constexpr uint64_t LEVEL_ACTION_VA = 0x1425efc18;
// FUN_1408caf60's per-stat (min, max) byte pairs; stat 0x28 = Physical Contact.
constexpr uint64_t STAT_BOUNDS_VA = 0x142613c60;
constexpr uint64_t PHYSICAL_CONTACT_STAT = 0x28;

// Instruction immediates, opcode-checked.
struct Imm {
  uint64_t va;
  std::array<uint8_t, 3> opcode;
  size_t opcodeLen;
  size_t offset;
};
constexpr Imm GAUGE_FULL_IMM = {0x1406f9df2, {0x41, 0xbc}, 2, 2};  // mov r12d, 1e6
constexpr Imm GAUGE_DRAIN_IMM = {0x1406fa122, {0xb8}, 1, 1};       // mov eax, -1e6
// lea ecx, [rax + rax*2]: the refill divisor 3*fps is an addressing scale,
// not an immediate, so the bytes are checked and the scale is the number.
constexpr Imm GAUGE_REFILL_LEA = {0x1406f9ded, {0x8d, 0x0c, 0x40}, 3, 0};
constexpr int GAUGE_REFILL_LEA_SCALE = 3;
constexpr Imm HOLD_UP_OFFSET_IMM = {0x14084331f, {0x8d, 0x4b}, 2, 2};  // lea ecx,[rbx+0x3c]
constexpr Imm SHOULDER_MARGIN_IMM = {0x140780c55, {0x83, 0xc1}, 2, 2};  // add ecx, 5
constexpr Imm CONTACT_WINDOW_IMM = {0x140845852, {0x8d, 0x41}, 2, 2};  // lea eax,[rcx+4]

bool Matches(const PeImage& exe, const Imm& imm) {
  uint8_t code[3] = {};
  return exe.ReadVa(imm.va, imm.opcodeLen, code) &&
         std::equal(imm.opcode.begin(), imm.opcode.begin() + imm.opcodeLen, code);
}

bool ReadImm32(const PeImage& exe, const Imm& imm, int* out) {
  return Matches(exe, imm) &&
         exe.ReadVa(imm.va + imm.offset, sizeof *out, reinterpret_cast<uint8_t*>(out));
}

bool ReadImm8(const PeImage& exe, const Imm& imm, int* out) {
  int8_t v = 0;
  if (!Matches(exe, imm) || !exe.ReadVa(imm.va + imm.offset, 1, reinterpret_cast<uint8_t*>(&v)))
    return false;
  *out = v;
  return true;
}

inline float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

bool IsLegBone(int bone) { return PartClass(bone) != ContactPart::kBody; }

bool IsLoco(ContactAction a) { return a == ContactAction::kLoco; }
// FUN_140a6e0d0 / flag 16: standing tackle, slide, keeper save.
bool IsTackling(ContactAction a) {
  return a == ContactAction::kTackle || a == ContactAction::kSlide ||
         a == ContactAction::kKeeperHandle;
}
bool IsStaggerOrFall(ContactAction a) {
  return a == ContactAction::kStagger || a == ContactAction::kFall;
}
// FUN_140a6e050: an action that plays the ball.
bool IsBallAction(ContactAction a) {
  return a == ContactAction::kKick || a == ContactAction::kTrap || a == ContactAction::kDribble;
}

// Frames at PES's 60 fps authoring base -> seconds.
float AtSixty(const ContactConstants& k, float frames) { return frames / k.framesPerSecondRef; }

// 1 - clamp((PC - 40) / 59): the factor the direct triggers widen by.
float ContactWeakness(const ContactConstants& k, int contact) {
  return 1.0f - Clamp01((contact - k.abilityFloor) / k.abilitySpan);
}

}  // namespace

ContactPart PartClass(int bone) {
  if (bone >= kBoneThighL && bone <= kBoneFootL) return ContactPart::kLeftLeg;
  if (bone >= kBoneThighR && bone <= kBoneFootR) return ContactPart::kRightLeg;
  return ContactPart::kBody;
}

bool IsArmBone(int bone) {
  return bone == kBoneUpperArmL || bone == kBoneForearmL || bone == kBoneUpperArmR ||
         bone == kBoneForearmR;
}

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
  k.forcedFallDisplacementUpper_m = f(FORCED_DISP_UPPER_OFF);
  k.forcedFallDisplacementTackling_m = f(FORCED_DISP_TACKLING_OFF);

  bool ok = exe.ReadVa(LEVEL_TABLE_VA, sizeof k.levelTable,
                       reinterpret_cast<uint8_t*>(k.levelTable)) &&
            exe.ReadVa(LEVEL_ACTION_VA, sizeof k.levelAction,
                       reinterpret_cast<uint8_t*>(k.levelAction));
  float clashStaggerMargin = 0.0f;
  const std::pair<uint64_t, float*> floats[] = {
      {0x142565600, &k.forcedFallPart3_m},
      {0x142588200, &k.keeperDS_m},
      {0x142598578, &k.keeperDF_m},
      {0x14259bdf0, &k.keeperVS_kmh},
      {0x14259be0c, &k.keeperVF_kmh},
      {0x14259be50, &k.frontalHeading_deg},
      {0x1425a8a28, &k.frontalDF_m},
      {0x1425ab128, &k.committedScale},
      {0x14259bdf4, &k.pushAwayFrom_frames},
      {0x14259be10, &k.pushAwayTo_frames},
      {0x14259bddc, &k.pushAwayScale},
      {0x14259a764, &k.framesPerSecondRef},
      {0x14259be1c, &k.holdUpFloor},
      {0x14253ee30, &k.holdUpCeil},
      {0x142596e6c, &k.holdUpSpan},
      {0x14256ae30, &k.holdUpFrames},
      {0x1425b8708, &k.holdUpDSScale},
      {0x14259d654, &k.holdUpVS_kmh},
      {0x142565600, &k.holdUpHalf},
      {0x14259be2c, &k.abilityFloor},
      {0x1425c48cc, &k.abilityTop},
      {0x1425cb2fc, &k.abilitySpan},
      {0x14258c264, &k.clashAbilityGain_kmh},
      {0x14259bdf0, &k.clashFall_kmh},
      {0x14259bdec, &clashStaggerMargin},
      {0x14259be20, &k.clashTrapFall_kmh},
      {0x1425c48c0, &k.clashTrapStagger_kmh},
      {0x14259d5d0, &k.clashPartner_m},
      {0x142592228, &k.clashFeetHeight_m},
      {0x1425cb30c, &k.approachAngle_deg},
      {0x14259be60, &k.behindAngle_deg},
      {0x14259be30, &k.sideAngle_deg},
      {0x1425a1ffc, &k.runnerSpeed_kmh},
      {0x1425c48ec, &k.footTrail_m},
      {0x142588210, &k.vetoRelativeSpeed_kmh},
      {0x14259be04, &k.vetoStillExtra_kmh},
      {0x14259222c, &k.vetoStill_kmh},
      {0x14259be7c, &k.vetoHoldExtra_kmh},
      {0x14259a764, &k.vetoFacing_deg},
      {0x142588210, &k.vetoPastContact_frames},
      {0x142598578, &k.slideLegDisplacement_m},
      {0x14259bdec, &k.slideFlag_frames},
      {0x14259bddc, &k.directRange_m},
      {0x142588200, &k.directFootHead_m},
      {0x142598578, &k.directFootHeadGamesmanship_m},
      {0x14259d5d4, &k.directNear_m},
      {0x14259be10, &k.studsRunSpeed_kmh},
      {0x142588210, &k.studsContest_frames},
      {0x14257a194, &k.studsBallLateral_m},
      {0x142590d98, &k.studsBase_m},
      {0x142598578, &k.studsGain_m},
      {0x14256ae1c, &k.tripRange_m},
      {0x142565600, &k.tripLateral_m},
      {0x142598578, &k.tackleStudBase_m},
      {0x142588200, &k.tackleStudGain_m},
      {0x142590d98, &k.pushTripFoot_m},
      {0x14256ae1c, &k.pushTripLateral_m},
      {0x14259be30, &k.gaugeSideMin_deg},
      {0x14259be60, &k.gaugeSideMax_deg},
      {0x14259be5c, &k.gaugeSideSpan_deg},
      {0x14259bddc, &k.gaugeBase_s},
      {0x14256ae30, &k.gaugeAbilityDivisor},
      {0x142565600, &k.gaugeHalf},
      {0x14256ae1c, &k.gaugeOne},
      {0x14259ce38, &k.kmhPerMps},
      {0x1425ab124, &k.staggerRateBase},
      {0x1425c48a0, &k.staggerRateGain},
      {0x1425ab124, &k.riseRateLow},
      {0x1425a3590, &k.riseRateHigh},
      {0x14259a764, &k.riseAbilitySpan},
      {0x142590d98, &k.riseBlend_s},
      {0x142565600, &k.pushFoot_m},
      {0x142590d98, &k.pushBallAhead_m},
      {0x142565600, &k.pushBallAcross_m},
      {0x14259be5c, &k.pushAngle_deg},
      {0x14259bdf4, &k.pushOppSpeed_kmh},
      {0x1425cca34, &k.pushBallDir_deg},
      {0x14259d648, &k.pushOppAlong_m},
      {0x1425ab4a8, &k.contactAllowance_m},
  };
  for (const auto& [va, out] : floats)
    ok &= exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
  // The clash's stagger cut is p minus the 6 of 0x14259bdec (subss).
  k.clashStagger_kmh = -clashStaggerMargin;
  uint8_t bounds[2] = {};
  ok &= exe.ReadVa(STAT_BOUNDS_VA + PHYSICAL_CONTACT_STAT * 2, sizeof bounds, bounds);
  k.gaugeContactMin = bounds[0];
  k.gaugeContactMax = bounds[1];
  ok &= ReadImm32(exe, GAUGE_FULL_IMM, &k.gaugeFull) &&
        ReadImm32(exe, GAUGE_DRAIN_IMM, &k.gaugeDrainNumerator) &&
        Matches(exe, GAUGE_REFILL_LEA) &&
        ReadImm8(exe, HOLD_UP_OFFSET_IMM, &k.holdUpContactOffset) &&
        ReadImm8(exe, SHOULDER_MARGIN_IMM, &k.shoulderThroughMargin) &&
        ReadImm8(exe, CONTACT_WINDOW_IMM, &k.tackleContactWindow_frames);
  k.gaugeRefillSeconds = GAUGE_REFILL_LEA_SCALE;
  if (!ok) {
    *error = "contact: PES2021.exe constants unmapped or unexpected instruction";
    return std::nullopt;
  }
  k.fallAction = k.levelAction[3];
  return k;
}

float JostleHoldUp_s(const ContactConstants& k, int carrierContact, int jostlerContact) {
  const float x = float(carrierContact + k.holdUpContactOffset - jostlerContact);
  const float t = x <= k.holdUpFloor ? 0.0f
                  : x >= k.holdUpCeil ? 1.0f
                                      : (x - k.holdUpFloor) / k.holdUpSpan;
  return AtSixty(k, k.holdUpFrames + k.holdUpFrames * t);
}

bool ShoulderThrough(const ContactConstants& k, int jostlerContact, int carrierContact) {
  return jostlerContact > carrierContact + k.shoulderThroughMargin;
}

Displacement TrackedDisplacement(const ContactConstants& k, float offX, float offY, float sepX,
                                 float sepY, bool slideLegMatches) {
  if (slideLegMatches) return {k.slideLegDisplacement_m, k.slideLegDisplacement_m};
  return {std::hypot(offX - sepX, offY - sepY), std::hypot(offX, offY)};
}

float ClosingSpeed(const ContactConstants& k, float selfVx, float selfVy, float oppVx, float oppVy,
                   float dirX, float dirY) {
  return k.kmhPerMps * ((selfVx * dirX + selfVy * dirY) - (oppVx * dirX + oppVy * dirY));
}

namespace {

ContactOpponentMode Mode(const ContactInput& in) {
  const ContactPart co = PartClass(in.oppBone);
  const bool legMatch = co == ContactPart::kBody || co == PartClass(in.oppSlideLegBone);
  if (in.oppAction == ContactAction::kSlide)
    return legMatch ? ContactOpponentMode::kSlide : ContactOpponentMode::kPlain;
  // A standing tackle past half its clip; never against a dodge or a jostle.
  // ponytail: PES also needs tackle sub-kind != 1 (human(opp)+0x44); GF
  // tackles carry no sub-kind and count as the general one.
  if (in.oppAction == ContactAction::kTackle && in.oppClipProgress > 0.5f &&
      in.selfAction != ContactAction::kDodge && in.selfAction != ContactAction::kJostle &&
      legMatch)
    return ContactOpponentMode::kTackle;
  return ContactOpponentMode::kPlain;
}

// The "committed" flag of 0x1408430d6..0x140843232.
bool Committed(const ContactInput& in, ContactOpponentMode m) {
  if (IsStaggerOrFall(in.oppAction) && IsTackling(in.selfAction)) return true;
  if (in.oppAction == ContactAction::kDodge && !IsTackling(in.selfAction)) return true;
  if (in.selfAction == ContactAction::kDodge && !IsTackling(in.oppAction)) return true;
  const bool tackleMode = m != ContactOpponentMode::kPlain;
  if ((in.selfAction == ContactAction::kJostle || in.oppAction == ContactAction::kJostle) &&
      tackleMode)
    return true;
  return in.selfAction == ContactAction::kKick && in.selfBeforeContactFrame;
}

// FUN_140844540 (all but the second-hit branches): true = level 3, no veto.
bool ForcedFall(const ContactConstants& k, const ContactInput& in, int tableLevel) {
  if (in.selfAction == ContactAction::kFall) return false;
  if (in.selfAction == ContactAction::kJostle && in.oppAction == ContactAction::kJostle)
    return false;
  // A slide whose leg (or body) meets the self above the knee, or leg on leg,
  // coming from within the approach window of its direction.
  if (in.oppAction == ContactAction::kSlide && in.oppSlidingFlag) {
    const ContactPart co = PartClass(in.oppBone);
    const bool slidePart = co == PartClass(in.oppSlideLegBone) || co == ContactPart::kBody;
    const bool reaches = in.contactHeight_m > in.selfKneeHeight_m ||
                         (IsLegBone(in.selfBone) && co != ContactPart::kBody);
    if (slidePart && reaches && in.slideApproach_deg < k.approachAngle_deg) return true;
  }
  // From behind: switched by contact.o's byte (0 in stock and the 4cc mod).
  if (k.fromBehindFallEnabled && IsTackling(in.oppAction) &&
      (IsLegBone(in.selfBone) || tableLevel > 0) && in.oppBehind_deg > k.behindAngle_deg)
    return true;
  const float bar = IsTackling(in.selfAction) ? k.forcedFallDisplacementTackling_m
                                              : k.forcedFallDisplacement_m;
  // ponytail: parts 1-2 (bar +0x28) also need FUN_140843e30, untraced; GF's
  // rigid body gives them part 3's length, which the 0.5 bar already covers.
  return in.displacement_m > bar || in.displacementLegs_m > k.forcedFallPart3_m;
}

// 0x14348cab0[0..8], in order; true = level 0.
bool Veto0(const ContactConstants& k, const ContactInput& in) {
  if (IsArmBone(in.selfBone)) return true;
  // The partner's arm: vetoed unless both of the self's shins are in the
  // contact too (record flags +0x628+part, parts 2 / 5 = LEG_L / LEG_R, the
  // per-part flags FUN_1408c31c0 sets and the injury roll reads).
  if (IsArmBone(in.oppBone))
    return in.oppAction != ContactAction::kKeeperHandle && !in.selfBothShinsTouching;
  // An opponent's body part, or the self already down: only a goalkeeper
  // (FUN_140a638c0) running about without the ball in his hands is spared.
  if (!IsLegBone(in.oppBone) || IsStaggerOrFall(in.selfAction))
    return in.selfIsKeeper && in.selfAction != ContactAction::kKeeperHandle && IsLoco(in.selfAction);
  const ContactAction o = in.oppAction;
  const bool legsBusy = o == ContactAction::kKick || IsTackling(o) || o == ContactAction::kTrap ||
                        o == ContactAction::kJostle;
  if (!legsBusy && IsLegBone(in.selfBone)) return true;
  if (in.selfSpeed_kmh < k.runnerSpeed_kmh) return false;
  return in.touchedFootForward_m < k.footTrail_m;
}

bool Veto1(const ContactConstants& k, const ContactInput& in) {
  if (in.oppAction == ContactAction::kKeeperDistribute) return true;
  const bool selfBall = in.selfAction == ContactAction::kKick || IsTackling(in.selfAction);
  const bool oppBall = IsBallAction(in.oppAction) || IsTackling(in.oppAction);
  if (selfBall && !oppBall) return in.selfBeforeContactFrame;
  return in.selfToOppBearing_deg > k.sideAngle_deg && in.selfAction == ContactAction::kDodge &&
         !IsTackling(in.oppAction);
}

bool Veto2(const ContactInput& in) {
  return in.selfAction == ContactAction::kFall || (IsLoco(in.selfAction) && in.selfRecovering);
}

bool Veto3(const ContactInput& in) {
  if (in.selfAction == ContactAction::kKeeperHandle && in.selfBeforeContactFrame) return true;
  return IsTackling(in.oppAction) && in.oppBeforeContactFrame;
}

bool Veto4(const ContactInput& in) {
  // ponytail: the window-tail tests (leg classes with zero separation, head
  // height) after the contact frame are not ported.
  const ContactAction a = in.selfAction;
  return (a == ContactAction::kKick || a == ContactAction::kTrap ||
          a == ContactAction::kKeeperHandle) &&
         in.selfBeforeContactFrame;
}

bool Veto5(const ContactInput& in) {
  return in.selfAction == ContactAction::kDodge && !IsTackling(in.oppAction);
}

bool Veto6(const ContactConstants& k, const ContactInput& in) {
  return !IsBallAction(in.oppAction) && in.oppAway_deg > k.approachAngle_deg &&
         in.oppSpeed_kmh > k.runnerSpeed_kmh;
}

bool Veto7(const ContactInput& in) {
  if (!IsLoco(in.selfAction)) return false;
  return IsLoco(in.oppAction) || in.oppPastLastHit;
}

bool Veto8(const ContactConstants& k, const ContactInput& in) {
  if (IsTackling(in.oppAction) || in.oppAction == ContactAction::kKick ||
      in.oppAction == ContactAction::kJostle)
    return false;
  bool still = false;
  float threshold = k.vetoRelativeSpeed_kmh;
  if (in.selfInHitWindow) {
    threshold += k.vetoHoldExtra_kmh;
  } else if (in.selfSpeed_kmh < k.vetoStill_kmh) {
    threshold += k.vetoStillExtra_kmh;
    still = true;
  }
  if (in.relativeSpeed_kmh >= threshold) return false;
  if (still) return true;
  const bool alike =
      in.headingDelta_deg < k.vetoFacing_deg || in.movementDelta_deg < k.vetoFacing_deg;
  if (!alike) return false;
  if (in.relativeSpeed_kmh < k.vetoStill_kmh) return true;
  if (IsLoco(in.selfAction)) return IsLoco(in.oppAction);
  if (!IsLoco(in.oppAction)) return false;
  return in.selfPastContact_s > AtSixty(k, k.vetoPastContact_frames);
}

}  // namespace

int ContactLevel(const ContactConstants& k, const ContactInput& in) {
  const bool body = PartClass(in.selfBone) == ContactPart::kBody;
  const ContactOpponentMode m = Mode(in);
  const int t = (body ? 1 : 0) + 2 * static_cast<int>(m);
  float dS = k.dS_m[t], dF = k.dF_m[t], vS = k.vS_kmh, vF = k.vF_kmh;
  float forced = k.forcedFallSpeedBody_kmh;
  if (in.oppAction == ContactAction::kKeeperHandle) {
    dS = k.keeperDS_m;
    dF = k.keeperDF_m;
    vS = k.keeperVS_kmh;
    vF = k.keeperVF_kmh;
  }
  // A frontal standing tackle always at least staggers.
  if (m == ContactOpponentMode::kTackle && in.oppTackleFrontal &&
      in.headingDelta_deg < k.frontalHeading_deg) {
    dS = 0.0f;
    dF = k.frontalDF_m;
  }
  auto scale = [&](float s) {
    forced += forced * s;
    dS *= s;
    dF *= s;
    vS *= s;
    vF *= s;
  };
  if (m != ContactOpponentMode::kSlide && Committed(in, m)) scale(k.committedScale);
  const float pushAwayFrames = in.selfPushAway_s * k.framesPerSecondRef;
  if (in.selfPushAway_s >= 0.0f && pushAwayFrames > k.pushAwayFrom_frames &&
      pushAwayFrames < k.pushAwayTo_frames)
    scale(k.pushAwayScale);
  // Jostle hold-up: past it the carrier's thresholds collapse.
  if (in.oppAction == ContactAction::kJostle && in.selfOwnsBall &&
      !(in.selfAction == ContactAction::kKick && in.selfBeforeContactFrame) &&
      in.oppJostleContact_s > JostleHoldUp_s(k, in.selfContactAbility, in.oppContactAbility)) {
    dS *= k.holdUpDSScale;
    vS = k.holdUpVS_kmh;
    forced += forced * k.holdUpHalf;
    dF *= k.holdUpHalf;
    vF *= k.holdUpHalf;
  }
  // Leg hits read the largest tracked part, body hits part 0 only.
  const float d = body ? in.displacement_m : std::max(in.displacement_m, in.displacementLegs_m);
  int dl = d < dS ? 0 : (d < dF ? 1 : 2);
  const int vl = in.closingSpeed_kmh < vS ? 0 : (in.closingSpeed_kmh < vF ? 1 : 2);
  if (body && in.closingSpeed_kmh > forced) dl = 2;
  int level = k.levelTable[dl * 3 + vl];

  // Receiving-body clash: a receiver with both feet off the ground inside his
  // clip's hold window; Physical Contact raises the speed he absorbs.
  if (in.selfInHitWindow && in.selfFeetHeight_m > k.clashFeetHeight_m &&
      in.selfAction != ContactAction::kDodge && in.partnerDistance_m < k.clashPartner_m && body &&
      in.selfAction != ContactAction::kStagger) {
    const float p =
        k.clashAbilityGain_kmh * Clamp01((in.selfContactAbility - k.abilityFloor) / k.abilitySpan);
    const bool lenient = in.selfBeforeContactFrame || in.selfAction == ContactAction::kTrap ||
                         in.oppAction == ContactAction::kTrap;
    const float fall = p + (lenient ? k.clashTrapFall_kmh : k.clashFall_kmh);
    const float stagger = p + (lenient ? k.clashTrapStagger_kmh : k.clashStagger_kmh);
    level = in.closingSpeed_kmh > fall ? 3 : (in.closingSpeed_kmh > stagger ? 2 : 0);
  }

  if (ForcedFall(k, in, level)) return 3;

  // A running carrier heading the opponent's way is only staggered.
  if (in.selfOwnsBall && in.headingDelta_deg < k.approachAngle_deg &&
      in.selfSpeed_kmh > k.runnerSpeed_kmh)
    level = std::min(level, 2);

  if (level > 0 &&
      (Veto0(k, in) || Veto1(k, in) || Veto2(in) || Veto3(in) || Veto4(in) || Veto5(in) ||
       Veto6(k, in) || Veto7(in) || Veto8(k, in)))
    level = 0;
  return std::clamp(level, 0, 3);
}

ContactReaction ReactionForLevel(const ContactConstants& k, int level) {
  if (level <= 0) return ContactReaction::kNone;
  return k.levelAction[std::min(level, 3)] == k.fallAction ? ContactReaction::kFall
                                                           : ContactReaction::kStagger;
}

bool PushAwayStarts(const ContactConstants& k, const PushAwayInput& in) {
  return in.dribbling && !in.alreadyPushing && in.oppFootToSelfBelly_m < k.pushFoot_m &&
         in.ballAhead_m > k.pushBallAhead_m && std::fabs(in.ballAcross_m) < k.pushBallAcross_m &&
         in.headingVsOppHeading_deg < k.pushAngle_deg && in.oppSpeed_kmh > k.pushOppSpeed_kmh &&
         in.headingVsOppMove_deg < k.pushAngle_deg && in.ballDirVsHeading_deg < k.pushBallDir_deg &&
         in.oppAlong_m < k.pushOppAlong_m;
}

DirectTrigger DirectTriggers(const ContactConstants& k, const DirectTriggerInput& in) {
  const bool selfDown = in.selfAction == ContactAction::kStagger;
  const bool oppTackling =
      in.oppAction == ContactAction::kTackle || in.oppAction == ContactAction::kSlide;
  DirectTrigger out;
  if (in.distance_m >= k.directRange_m) return out;
  // Cause 4: a tackle's foot at the self's bone 10; Gamesmanship (skill bit
  // 21) doubles the radius.
  if (oppTackling && !selfDown &&
      in.oppFootToSelfHead_m <
          (in.selfGamesmanship ? k.directFootHeadGamesmanship_m : k.directFootHead_m))
    return {ContactReaction::kFall, 4};
  // Cause 5: studs on a ball-owning runner's bone 8.
  // ponytail: two distance tests (0.8 / 0.7 m) and the 0.35 m foot-height
  // test of the same branch are untraced and not applied.
  if (in.selfOwnsBall && in.distance_m < k.directNear_m && !selfDown && oppTackling &&
      in.selfSpeed_kmh > k.studsRunSpeed_kmh &&
      in.selfContested_s > AtSixty(k, k.studsContest_frames) && in.oppPastContactFrame &&
      in.headingDelta_deg < k.sideAngle_deg && std::fabs(in.ballLateral_m) < k.studsBallLateral_m &&
      in.oppFootToSelfChest_m <
          k.studsBase_m + k.studsGain_m * ContactWeakness(k, in.selfContactAbility))
    return {ContactReaction::kStagger, 5};
  // Causes 3 / 6: a feint-through cut with the other directly in line.
  if (in.distance_m < k.tripRange_m && !selfDown) {
    if (in.selfFeintBeforeContact && in.selfMoveVsHeading_deg < k.sideAngle_deg &&
        (in.headingDelta_deg > k.behindAngle_deg || !IsLoco(in.oppAction))) {
      if (in.oppAlongSelf_m > 0.0f && std::fabs(in.oppAcrossSelf_m) < k.tripLateral_m)
        return {ContactReaction::kFall, 3};
      return out;
    }
    if (in.oppFeintBeforeContact && in.oppMoveVsHeading_deg < k.sideAngle_deg &&
        (in.headingDelta_deg > k.behindAngle_deg || !IsLoco(in.selfAction)) &&
        in.selfAlongOpp_m > 0.0f && std::fabs(in.selfAcrossOpp_m) < k.tripLateral_m)
      return {ContactReaction::kFall, 6};
  }
  if (in.distance_m >= k.directNear_m) return out;
  // Cause 7: a sub-kind-1 standing tackle at its contact frame.
  if (in.oppAction == ContactAction::kTackle && in.oppStandingTackleKind1 &&
      in.oppFromContact_frames < k.tackleContactWindow_frames &&
      in.oppFootToSelfBelly_m <
          k.tackleStudBase_m + k.tackleStudGain_m * ContactWeakness(k, in.selfContactAbility))
    return {ContactReaction::kFall, 7};
  // Cause 1: a push-away jostle clip's foot at the bone 7 of the man in front.
  const float pushFrames = in.oppPushAway_s * k.framesPerSecondRef;
  if (!selfDown && in.oppPushAway_s >= 0.0f && pushFrames > k.pushAwayFrom_frames &&
      pushFrames < k.pushAwayTo_frames && in.headingDelta_deg < k.sideAngle_deg &&
      in.oppFootToSelfBelly_m < k.pushTripFoot_m && in.selfAlongOpp_m < k.pushTripFoot_m &&
      std::fabs(in.selfAcrossOpp_m) < k.pushTripLateral_m)
    return {ContactReaction::kStagger, 1};
  return out;
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

float StaggerRate(const ContactConstants& k, int balance) {
  const float b = Clamp01((balance - k.abilityFloor) / k.abilitySpan);
  return k.staggerRateBase + k.staggerRateGain * b * b;
}

float RiseUpRate(const ContactConstants& k, int balance) {
  const float b = Clamp01((balance - k.abilityFloor) / k.riseAbilitySpan);
  return k.riseRateLow * (1.0f - b) + k.riseRateHigh * b;
}

}  // namespace pes

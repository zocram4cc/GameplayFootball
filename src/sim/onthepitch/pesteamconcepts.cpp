#include "pesteamconcepts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

#include "gametypes.hpp"
#include "pes/uniform.hpp"

namespace pes {
namespace {

// {VA, expected opcode bytes}: the instruction is checked before its operand
// is trusted, so another build fails loudly.
struct Code {
  uint64_t va;
  std::initializer_list<uint8_t> bytes;
};

std::string VaText(uint64_t va) {
  char s[32];
  std::snprintf(s, sizeof s, "%#llx", static_cast<unsigned long long>(va));
  return s;
}

bool Matches(const PeImage& exe, const Code& c, std::string* error) {
  uint8_t got[16] = {};
  if (!exe.ReadVa(c.va, c.bytes.size(), got) || !std::equal(c.bytes.begin(), c.bytes.end(), got)) {
    *error = "team concept constant: unexpected instruction at " + VaText(c.va);
    return false;
  }
  return true;
}

bool ReadByteImm(const PeImage& exe, const Code& c, int* out, std::string* error) {
  uint8_t v = 0;
  if (!Matches(exe, c, error) || !exe.ReadVa(c.va + c.bytes.size(), 1, &v)) {
    if (error->empty()) *error = "team concept constant: unreadable at " + VaText(c.va);
    return false;
  }
  *out = v;
  return true;
}

bool ReadF32(const PeImage& exe, uint64_t va, float* out, std::string* error) {
  if (!exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out))) {
    *error = "team concept constant: .rdata unreadable at " + VaText(va);
    return false;
  }
  return true;
}

// `lea r32,[rax+rax*s]` multiplies by s+1 (the SIB scale field, bits 6-7).
float LeaMultiplier(uint8_t sib) { return static_cast<float>((1 << (sib >> 6)) + 1); }

// --- the image ---------------------------------------------------------------
// Sheet ctor FUN_1408c5390: `mov byte [rcx+rax+k], imm8` per slider k.
constexpr Code SHEET_SUPPORT_RANGE{0x1408c5418, {0xc6, 0x44, 0x01, 0x04}};
constexpr Code SHEET_DEFENSIVE_LINE{0x1408c5454, {0xc6, 0x44, 0x01, 0x06}};
constexpr Code SHEET_COMPACTNESS{0x1408c5471, {0xc6, 0x44, 0x01, 0x07}};
constexpr Code SHEET_TOGGLES_ZERO{0x1408c53bc, {0x33, 0xf6}};  // xor esi,esi: the toggle default
constexpr uint64_t END_TEST_AFTER_VA = 0x142588200;           // 0.1 (selector, x fps)

// TransitionChase start FUN_140575510 / execute FUN_140575b60.
constexpr Code CHASE_WINDOW_LEA{0x1405755c3, {0x8d, 0x0c, 0x40}};  // lea ecx,[rax+rax*2]: 3 x fps
constexpr uint64_t CHASE_THIRD_VA = 0x142588210;     // 10
constexpr uint64_t CHASE_WIDE_VA = 0x142565604;      // 2
constexpr uint64_t CHASE_RANGE_SQ_VA = 0x14253ee30;  // 100: the d^2 test and the 100 deg test
constexpr uint64_t CHASE_INSIDE_VA = 0x14259be5c;    // 90
constexpr uint64_t CHASE_SECOND_SQ_VA = 0x1425a2670; // 400
constexpr uint64_t CHASE_SLACK_VA = 0x14259be0c;     // 15
constexpr uint64_t CHASE_TARGET_VA = 0x14259bdf4;    // 8
constexpr uint64_t CHASE_TURN_VA = 0x14259be6c;      // 160

// SideCounter start FUN_140540780.
constexpr uint64_t COUNTER_WINDOW_VA = 0x14258c264;   // 3
constexpr uint64_t COUNTER_KICKOFF_VA = 0x14259bdec;  // 6
constexpr uint64_t COUNTER_FACING_VA = 0x14259be38;   // 55
constexpr uint64_t COUNTER_DEPTH_VA = 0x14259d664;    // -12
constexpr uint64_t COUNTER_LINE_VA = 0x14259be84;     // -10
constexpr uint64_t COUNTER_ABORT_LINE_VA = 0x1425c7220;  // -14
constexpr uint64_t COUNTER_ABORT_WIDE_VA = 0x1425656c0;  // 4
constexpr Code COUNTER_MAX_GOAL_SIDE{0x140540bfd, {0x3c}};  // cmp al, imm8
// Runner pick FUN_14053e9a0 and target FUN_14053f510.
constexpr uint64_t RUNNER_UNMARKED_VA = 0x1425c721c;  // 42.25
constexpr uint64_t RUNNER_SHARE_NUM_VA = 0x14259be34; // 50
constexpr uint64_t RUNNER_SHARE_DEN_VA = 0x14253ee30; // 100
constexpr uint64_t RUNNER_SLACK_VA = 0x14259bdfc;     // 11
constexpr uint64_t RUNNER_CENTRE_VA = 0x14259d5d4;    // 1.2
constexpr uint64_t RUNNER_AREA_VA = 0x14257a194;      // 0.7
constexpr uint64_t RUNNER_LANE_VA = 0x14259d614;      // 17
// Supporters FUN_140541010.
constexpr uint64_t SUPPORT_AHEAD_VA = 0x14259be0c;  // 15
constexpr uint64_t SUPPORT_SIDE_VA = 0x14259be04;   // 12
constexpr uint64_t SUPPORT_TURN_VA = 0x1425c48c0;   // 22
constexpr uint64_t SUPPORT_MIN_AHEAD_VA = 0x14259bdf4;  // 8
constexpr uint64_t SUPPORT_HOLD_VA = 0x14259be7c;   // -5
// End test FUN_1405404a0.
constexpr Code COUNTER_GRACE_ADD{0x14054056a, {0x03, 0xc0}};      // add eax,eax: 2 x fps
constexpr Code COUNTER_MAX_LEA{0x140540609, {0x8d, 0x0c, 0x40}};  // lea ecx,[rax+rax*2]
constexpr Code COUNTER_MAX_ADD{0x14054060c, {0x03, 0xc9}};        // add ecx,ecx: 6 x fps
constexpr uint64_t COUNTER_FAR_VA = 0x1425c48e4;  // 1369
constexpr uint64_t COUNTER_DX_VA = 0x142591140;   // 30
// ForeCheckPress start FUN_1405747f0 / continue FUN_140574710 / count FUN_140574200.
constexpr uint64_t FORECHECK_WINDOW_VA = 0x14259222c;  // 5 (x fps)
constexpr uint64_t FORECHECK_HOLD_VA = 0x142565604;    // 2 (x fps)
constexpr uint64_t FORECHECK_FLOOR_VA = 0x14259be84;   // -10
constexpr uint64_t PRESS_GOAL_SIDE_VA = 0x14259be4c;   // 70
constexpr uint64_t PRESS_RAMP_FROM_VA = 0x14253ee30;   // 100
constexpr uint64_t PRESS_RAMP_TO_VA = 0x1425c964c;     // 170
constexpr uint64_t PRESS_RADIUS_VA = 0x14259bdf4;      // 8
constexpr uint64_t PRESS_RADIUS_RAMP_VA = 0x1425656c0; // 4
constexpr uint64_t PRESS_BONUS_ABOVE_VA = 0x142598578; // 0.2
constexpr Code PRESS_COUNT{0x140574654, {0x41, 0x80, 0xfc}};  // cmp r12b, imm8
// ForeCheckLineDown FUN_140574a30 / FUN_140574ab0 / FUN_140574940.
constexpr uint64_t LINEDOWN_DIVISOR_VA = 0x1425c74d0;  // -2.5
constexpr uint64_t LINEDOWN_MAX_VA = 0x1425c50fc;      // 27
constexpr uint64_t LINEDOWN_GAP_VA = 0x14258c264;      // 3
constexpr uint64_t LINEDOWN_FACING_VA = 0x14259be4c;   // 70
// RetreatBlockCreate end FUN_140575db0.
constexpr uint64_t RETREAT_GAP_VA = 0x14259222c;       // 5
constexpr uint64_t RETREAT_BACK_VA = 0x14259bdf4;      // 8
constexpr uint64_t RETREAT_MID_VA = 0x14259be18;       // 18
// Band push-up FUN_140687fb0 and the measured line FUN_140687820.
constexpr uint64_t PUSHUP_BEHIND_VA = 0x1425c7678;     // -4
constexpr uint64_t PUSHUP_BALL_VA = 0x14259bdec;       // 6
constexpr uint64_t PUSHUP_CAP_VA = 0x14259be7c;        // -5
constexpr uint64_t MEASURED_CLAMP_VA = 0x1425656c0;    // 4

// constant_team.bin's subConcept.o (constant-team 2.3): blob u32 0 = the offset
// of the 48-entry i32 list.
constexpr const char* CONSTANT_TEAM_PATH = "common/match/constant/constant_team.bin";
constexpr const char* SUB_CONCEPT_OBJECT = "subConcept.o";

bool LoadEnables(const PesVfs& vfs, std::array<bool, kConceptCount>* out, std::string* error) {
  auto blob = vfs.Read(CONSTANT_TEAM_PATH, error);
  if (!blob) return false;
  auto table = ParseUniformTable(*blob, error);
  if (!table) return false;
  auto span = table->Find(SUB_CONCEPT_OBJECT);
  if (!span) {
    *error = std::string(SUB_CONCEPT_OBJECT) + " missing from " + CONSTANT_TEAM_PATH;
    return false;
  }
  uint32_t listAt = 0;
  if (span->size() < sizeof listAt) {
    *error = "subConcept.o truncated";
    return false;
  }
  std::memcpy(&listAt, span->data(), sizeof listAt);
  if (static_cast<size_t>(listAt) + kConceptCount * sizeof(int32_t) > span->size()) {
    *error = "subConcept.o list out of range";
    return false;
  }
  for (int id = 0; id < kConceptCount; ++id) {
    int32_t v = 0;
    std::memcpy(&v, span->data() + listAt + id * sizeof v, sizeof v);
    (*out)[id] = v > 0;  // the selector skips `<= 0` (jle, 0x140579a1f)
  }
  return true;
}

// --- geometry ----------------------------------------------------------------
constexpr float kDegPerRad = 180.0f / 3.14159265358979f;
constexpr float kFullTurn_deg = 360.0f;
constexpr float kHalfTurn_deg = 180.0f;
constexpr float kPlusX_deg = 90.0f;   // FUN_140a4f210: dir +1 -> 90
constexpr float kMinusX_deg = 270.0f; // dir -1 -> 270
constexpr float kPlusY_deg = 0.0f;

float Wrap360(float a) {
  if (a < 0.0f) return a + kFullTurn_deg;
  if (a >= kFullTurn_deg) return a - kFullTurn_deg;
  return a;
}

// FUN_14043a0a0: |a - b| folded to 0..180.
float AngleDiff(float a, float b) {
  float d = std::fabs(Wrap360(b) - Wrap360(a));
  return d > kHalfTurn_deg ? kFullTurn_deg - d : d;
}

float AttackHeading(float dir) { return dir > 0.0f ? kPlusX_deg : kMinusX_deg; }

// Bearing a -> b (FUN_140a92d40 on (dx, dy), 0 when they coincide).
float Bearing(const Vector3& a, const Vector3& b) {
  const float dx = b.coords[0] - a.coords[0], dy = b.coords[1] - a.coords[1];
  if (dx == 0.0f && dy == 0.0f) return 0.0f;
  return Wrap360(std::atan2(dx, dy) * kDegPerRad);
}

// FUN_140a92950: step `from` toward `to` by at most `max`.
float TurnToward(float from, float to, float max) {
  float d = Wrap360(to) - Wrap360(from);
  if (d > kHalfTurn_deg) d -= kFullTurn_deg;
  if (d < -kHalfTurn_deg) d += kFullTurn_deg;
  const float step = std::min(std::fabs(d), max);
  return Wrap360(Wrap360(from) + (d < 0.0f ? -step : step));
}

// FUN_140a8f7c0: p + r * (sin h, cos h).
Vector3 Advance(const Vector3& p, float heading_deg, float r) {
  const float h = heading_deg / kDegPerRad;
  return Vector3(p.coords[0] + r * std::sin(h), p.coords[1] + r * std::cos(h), 0.0f);
}

// PES's side sign: `0 > z ? -1 : 1`.
float SideSign(float y) { return y < 0.0f ? -1.0f : 1.0f; }

float DistSq(const Vector3& a, const Vector3& b) {
  const float dx = a.coords[0] - b.coords[0], dy = a.coords[1] - b.coords[1];
  return dx * dx + dy * dy;
}

// FUN_140a4c720(a, b, dir, c): `(dir*c + a)*dir < dir*b` - b lies beyond a + c
// in the team's attack frame.
bool Beyond(float a, float b, float dir, float c) { return (dir * c + a) * dir < dir * b; }

}  // namespace

std::optional<TeamConceptConstants> LoadTeamConceptConstants(const PeImage& exe, const PesVfs& vfs,
                                                             std::string* error) {
  TeamConceptConstants k{};
  bool ok = ReadByteImm(exe, SHEET_SUPPORT_RANGE, &k.defaultSupportRange, error) &&
            ReadByteImm(exe, SHEET_DEFENSIVE_LINE, &k.defaultDefensiveLine, error) &&
            ReadByteImm(exe, SHEET_COMPACTNESS, &k.defaultCompactness, error) &&
            Matches(exe, SHEET_TOGGLES_ZERO, error) && Matches(exe, CHASE_WINDOW_LEA, error) &&
            ReadByteImm(exe, COUNTER_MAX_GOAL_SIDE, &k.counterMaxGoalSide, error) &&
            Matches(exe, COUNTER_GRACE_ADD, error) && Matches(exe, COUNTER_MAX_LEA, error) &&
            Matches(exe, COUNTER_MAX_ADD, error) && ReadByteImm(exe, PRESS_COUNT, &k.pressCount, error);
  if (!ok) return std::nullopt;
  const float leaTriple = LeaMultiplier(*(CHASE_WINDOW_LEA.bytes.begin() + 2));
  constexpr float kAddDoubles = 2.0f;  // add r,r
  k.chaseWindow_s = leaTriple;
  k.counterRunGrace_s = kAddDoubles;
  k.counterMaxAge_s = LeaMultiplier(*(COUNTER_MAX_LEA.bytes.begin() + 2)) * kAddDoubles;

  float shareNum = 0, shareDen = 0;
  const std::pair<uint64_t, float*> floats[] = {
      {END_TEST_AFTER_VA, &k.endTestAfter_s},
      {CHASE_THIRD_VA, &k.chaseThirdMargin_m},
      {CHASE_WIDE_VA, &k.chaseWideMargin_m},
      {CHASE_RANGE_SQ_VA, &k.chaseRangeSq_m2},
      {CHASE_RANGE_SQ_VA, &k.chaseGoalSideMax_deg},
      {CHASE_INSIDE_VA, &k.chaseInsideMin_deg},
      {CHASE_SECOND_SQ_VA, &k.chaseSecondRangeSq_m2},
      {CHASE_SLACK_VA, &k.chaseSecondWidthSlack_m},
      {CHASE_TARGET_VA, &k.chaseTargetDistance_m},
      {CHASE_TURN_VA, &k.chaseTargetTurn_deg},
      {COUNTER_WINDOW_VA, &k.counterWindow_s},
      {COUNTER_KICKOFF_VA, &k.counterKickoffGuard_s},
      {COUNTER_FACING_VA, &k.counterFacingMax_deg},
      {COUNTER_DEPTH_VA, &k.counterBallMaxDepth_m},
      {COUNTER_LINE_VA, &k.counterLineMargin_m},
      {COUNTER_ABORT_LINE_VA, &k.counterAbortLineMargin_m},
      {COUNTER_ABORT_WIDE_VA, &k.counterAbortWideMargin_m},
      {RUNNER_UNMARKED_VA, &k.runnerUnmarkedSq_m2},
      {RUNNER_SHARE_NUM_VA, &shareNum},
      {RUNNER_SHARE_DEN_VA, &shareDen},
      {RUNNER_SLACK_VA, &k.runnerChannelSlack_m},
      {RUNNER_CENTRE_VA, &k.runnerCentreFactor},
      {RUNNER_AREA_VA, &k.runnerAreaFactor},
      {RUNNER_LANE_VA, &k.runnerWideLane_m},
      {SUPPORT_AHEAD_VA, &k.supportAhead_m},
      {SUPPORT_SIDE_VA, &k.supportSide_m},
      {SUPPORT_TURN_VA, &k.supportTurn_deg},
      {SUPPORT_MIN_AHEAD_VA, &k.supportMinAhead_m},
      {SUPPORT_HOLD_VA, &k.supportHoldBonus_m},
      {COUNTER_FAR_VA, &k.counterRunnerFarSq},
      {COUNTER_DX_VA, &k.counterRunnerMaxDx_m},
      {FORECHECK_WINDOW_VA, &k.foreCheckWindow_s},
      {FORECHECK_HOLD_VA, &k.foreCheckHold_s},
      {FORECHECK_FLOOR_VA, &k.foreCheckBallFloor_m},
      {PRESS_GOAL_SIDE_VA, &k.pressGoalSide_deg},
      {PRESS_RAMP_FROM_VA, &k.pressRampFrom_deg},
      {PRESS_RAMP_TO_VA, &k.pressRampTo_deg},
      {PRESS_RADIUS_VA, &k.pressRadius_m},
      {PRESS_RADIUS_RAMP_VA, &k.pressRadiusRamp_m},
      {PRESS_BONUS_ABOVE_VA, &k.pressBonusAbove},
      {LINEDOWN_DIVISOR_VA, &k.lineDownBallDivisor},
      {LINEDOWN_MAX_VA, &k.lineDownBallMax_m},
      {LINEDOWN_GAP_VA, &k.lineDownGap_m},
      {LINEDOWN_FACING_VA, &k.lineDownLongPassFacing_deg},
      {RETREAT_GAP_VA, &k.retreatGap_m},
      {RETREAT_BACK_VA, &k.retreatBackFront_m},
      {RETREAT_MID_VA, &k.retreatMidFront_m},
      {PUSHUP_BEHIND_VA, &k.pushUpBehind_m},
      {PUSHUP_BALL_VA, &k.pushUpBallGap_m},
      {PUSHUP_CAP_VA, &k.pushUpCap_m},
      {MEASURED_CLAMP_VA, &k.measuredLineClamp_m},
  };
  for (const auto& [va, out] : floats)
    if (!ReadF32(exe, va, out, error)) return std::nullopt;
  if (shareDen == 0.0f) {
    *error = "team concept constant: zero channel divisor";
    return std::nullopt;
  }
  k.runnerChannelShare = shareNum / shareDen;
  if (!LoadEnables(vfs, &k.enabled, error)) return std::nullopt;
  return k;
}

TeamSheet DefaultTeamSheet(const TeamConceptConstants& k) {
  TeamSheet s;
  s.supportRange = k.defaultSupportRange;
  s.defensiveLine = k.defaultDefensiveLine;
  s.compactness = k.defaultCompactness;
  return s;  // toggles 0, the ctor's default
}

bool ConceptGate(int id, const TeamSheet& sheet) {
  switch (id) {
    case kConceptSideCounter:  // 0x140543f40: Attacking Style == Counter Attack
      return sheet.toggles[kAttackingStyle] == 0;
    case kConceptForeCheckPress:  // 0x140574910: Defensive Style == Frontline Pressure
    case kConceptForeCheckLineDown:
    case kConceptTransitionChase:
      return sheet.toggles[kDefensiveStyle] == 0;
    case kConceptRetreatBlockCreate:  // 0x1405767f0: All-out Defence
      return sheet.toggles[kDefensiveStyle] == 1;
    default:
      return false;
  }
}

bool ForeCheckPressCount(const TeamConceptConstants& k, const DefenceConceptInput& in) {
  // FUN_140574200(ctx, 0): the ball not held by the opponents -> press on.
  if (!in.opponentsHoldBall) return true;
  // Bearings from the ball, against the heading toward our own goal
  // (FUN_140a4db20). ponytail: the candidate filter FUN_140639830 /
  // FUN_140639a20 (637 + 519 insns of per-slot state tests) is not reduced:
  // every outfield man is a candidate; the +5 m lane bonus (FUN_140a53da0 ->
  // FUN_140a53df0) is not applied.
  const float toOwnGoal = AttackHeading(-in.dir);
  int count = 0;
  bool close = false;
  for (const Vector3& m : in.mates) {
    const float ang = AngleDiff(Bearing(in.ball, m), toOwnGoal);
    const float w = ang <= k.pressRampFrom_deg ? 0.0f
                    : ang >= k.pressRampTo_deg ? 1.0f
                                               : (ang - k.pressRampFrom_deg) / k.pressGoalSide_deg;
    const float r = w * k.pressRadiusRamp_m + k.pressRadius_m;
    if (DistSq(m, in.ball) > r * r) continue;
    if (ang < k.pressGoalSide_deg) close = true;
    if (++count >= k.pressCount && close) return true;
  }
  return false;
}

bool ForeCheckPressStart(const TeamConceptConstants& k, const DefenceConceptInput& in) {
  if (!in.inPlay || in.sincePossessionChange_s > k.foreCheckWindow_s) return false;
  // Ball in the opponents' half. ponytail: the "< 12 m and inside the
  // corridor" veto reads MA+0x3c+0x14*side, a per-team float whose writer is
  // not found (tried: its only displacement readers are this test).
  if (in.dir * in.ball.coords[0] < 0.0f) return false;
  return ForeCheckPressCount(k, in);  // instruction 0xd is never armed in GF
}

bool ForeCheckPressEnd(const TeamConceptConstants& k, const DefenceConceptInput& in) {
  if (!in.inPlay || in.sincePossessionChange_s > k.foreCheckWindow_s) return true;
  if (k.foreCheckBallFloor_m > in.dir * in.ball.coords[0]) return true;
  if (in.sincePossessionChange_s <= k.foreCheckHold_s) return false;
  return !ForeCheckPressCount(k, in);
}

namespace {
// FUN_140574a30 + FUN_140574ab0 + the trigger ORs, shared by start and end.
bool LineDownHolds(const TeamConceptConstants& k, const DefenceConceptInput& in) {
  const float b = in.dir * in.ball.coords[0];
  if (pitchHalfW / k.lineDownBallDivisor > b || b > k.lineDownBallMax_m) return false;
  // |T+0x89a0 - T+0x875c|: a world x against an own-frame band, as written.
  if (std::fabs(in.measuredBack - in.backBand) < k.lineDownGap_m) return false;
  // FUN_140574940: the holder in LONG_PASS facing within 70 deg of our attack
  // heading. ponytail: FUN_140574bb0 (the uncovered-carrier test) ends in
  // FUN_140a4fea0 -> FUN_140a4fbc0, a cone-occupancy walk not reduced; false.
  const bool longPass = in.holderLongPass &&
                        AngleDiff(in.holderHeading_deg, AttackHeading(in.dir)) <= k.lineDownLongPassFacing_deg;
  return in.attackLevelZero || longPass;
}
}  // namespace

bool ForeCheckLineDownStart(const TeamConceptConstants& k, const DefenceConceptInput& in) {
  return !in.keeperHolds && in.inPlay && LineDownHolds(k, in);
}

bool ForeCheckLineDownEnd(const TeamConceptConstants& k, const DefenceConceptInput& in) {
  return !in.inPlay || !LineDownHolds(k, in);
}

bool RetreatBlockCreateEnd(const TeamConceptConstants& k, const DefenceConceptInput& in) {
  if (!in.inPlay) return true;
  const float a = in.dir * in.measuredBack, b = in.dir * in.measuredMid;
  if (std::fabs(a - in.backBand) < k.retreatGap_m && std::fabs(b - in.midBand) < k.retreatGap_m) return true;
  return k.retreatBackFront_m - pesPenaltyAreaLineX > a && k.retreatMidFront_m - pesPenaltyAreaLineX > b;
}

float BandPushUp_m(const TeamConceptConstants& k, float measuredBackOwn, float ballOwn, float backBand) {
  // ponytail: the -15 m variant behind ctx+0x138 (a flag with no writer found
  // in the shape pass) is not taken; FUN_14063fdc0's kick-prediction point is
  // the current ball.
  float x = measuredBackOwn + k.pushUpBehind_m;
  x = std::min(x, ballOwn - k.pushUpBallGap_m);
  x = std::min(x, k.pushUpCap_m);
  return x > backBand ? x - backBand : 0.0f;
}

std::optional<ChasePick> TransitionChaseStart(const TeamConceptConstants& k, const TransitionChaseInput& in) {
  if (!in.inPlay || in.sincePossessionChange_s > k.chaseWindow_s || in.carrierKicking) return std::nullopt;
  const Vector3& c = in.carrier;
  // Our attacking third and wide.
  if (pesPenaltyAreaLineX - k.chaseThirdMargin_m > in.dir * c.coords[0]) return std::nullopt;
  if (pesPenaltyAreaHalfWidth - k.chaseWideMargin_m > std::fabs(c.coords[1])) return std::nullopt;
  if (in.chaser < 0 || in.chaser >= static_cast<int>(in.men.size())) return std::nullopt;
  const ChaseMan& u = in.men[in.chaser];
  if (u.falling || DistSq(u.pos, c) > k.chaseRangeSq_m2) return std::nullopt;
  // Goal-side of the carrier (toward where the opponents attack) and inside him.
  const float bearing = Bearing(c, u.pos);
  if (AngleDiff(AttackHeading(-in.dir), bearing) > k.chaseGoalSideMax_deg) return std::nullopt;
  const float outward = c.coords[1] > 0.0f ? kPlusY_deg : kHalfTurn_deg;
  if (k.chaseInsideMin_deg > AngleDiff(outward, bearing)) return std::nullopt;
  // FUN_140575930: the widest available man in the carrier's distance list,
  // wider than |u.y| - 15, until the list passes 20 m.
  int second = -1;
  float widest = std::fabs(u.pos.coords[1]) - k.chaseSecondWidthSlack_m;
  for (int i = 0; i < static_cast<int>(in.men.size()); ++i) {
    const ChaseMan& m = in.men[i];
    if (DistSq(m.pos, c) > k.chaseSecondRangeSq_m2) break;
    if (i == in.chaser || !m.available || m.falling) continue;
    const float w = std::fabs(m.pos.coords[1]);
    if (w > widest) widest = w, second = i;
  }
  if (second < 0) return std::nullopt;
  return ChasePick{in.chaser, second};
}

Vector3 TransitionChaseTarget(const TeamConceptConstants& k, const Vector3& chaser, const Vector3& carrier,
                              float dir) {
  // From the chaser (record +0x1c), the own attack heading turned 160 deg so
  // the point lies behind him on the carrier's touchline side.
  const float turn = SideSign(carrier.coords[1]) * dir < 0.0f ? 1.0f : -1.0f;
  Vector3 p = Advance(chaser, Wrap360(AttackHeading(dir) + turn * k.chaseTargetTurn_deg), k.chaseTargetDistance_m);
  if (std::fabs(p.coords[0]) > pitchHalfW) p.coords[0] = SideSign(p.coords[0]) * pitchHalfW;
  if (std::fabs(p.coords[1]) > pitchHalfH) p.coords[1] = SideSign(p.coords[1]) * pitchHalfH;
  return p;
}

std::optional<int> SideCounterStart(const TeamConceptConstants& k, const SideCounterInput& in) {
  if (!in.attackAllowed || in.sincePossessionChange_s > k.counterWindow_s ||
      in.sinceKickoff_s < k.counterKickoffGuard_s || in.holder < 0 || in.keeperHolds || in.ownKeeperPunting)
    return std::nullopt;
  if (AngleDiff(in.holderHeading_deg, AttackHeading(in.dir)) > k.counterFacingMax_deg) return std::nullopt;
  // ponytail: FUN_140a4e720's forward-cone occupancy test (150, 4 m) is not
  // reduced; the goal-side count below carries the "is it on" judgement.
  const float depth = in.dir * in.ball.coords[0];
  if (depth > k.counterBallMaxDepth_m || -pesPenaltyAreaLineX > depth) return std::nullopt;
  if (std::fabs(in.ball.coords[1]) > pesPenaltyAreaHalfWidth) return std::nullopt;
  int goalSide = 0;
  for (const Vector3& o : in.opponents) {
    if (Beyond(in.oppLineX, o.coords[0], in.dir, k.counterLineMargin_m)) ++goalSide;
    if (SideSign(o.coords[1]) == SideSign(in.ball.coords[1]) &&
        Beyond(in.oppLineX, o.coords[0], in.dir, k.counterAbortLineMargin_m) &&
        pesPenaltyAreaHalfWidth - k.counterAbortWideMargin_m <= std::fabs(o.coords[1]))
      return std::nullopt;
  }
  if (goalSide > k.counterMaxGoalSide) return std::nullopt;
  // Runner (FUN_14053e9a0). ponytail: the run-style multipliers, the
  // overridable-state vetoes and the covered-channel flags are not reduced;
  // the scorer keeps its spine - a free wide/forward man, distance to his
  // channel point, the Attacking Area lane factor - lowest wins.
  int runner = -1;
  float best = std::numeric_limits<float>::max();
  for (int i = 0; i < static_cast<int>(in.men.size()); ++i) {
    const CounterMan& m = in.men[i];
    if (i == in.holder || !m.wideOrForward || m.nearestOpponentSq_m2 <= k.runnerUnmarkedSq_m2) continue;
    float score = std::sqrt(DistSq(m.pos, SideCounterRunTarget(k, m.pos, in.oppLineX)));
    const bool wideMan = std::fabs(m.pos.coords[1]) >= k.runnerWideLane_m;
    if (wideMan) score *= in.attackingArea == 0 ? k.runnerAreaFactor : k.runnerCentreFactor;
    if (score < best) best = score, runner = i;
  }
  if (runner < 0) return std::nullopt;
  return runner;
}

Vector3 SideCounterRunTarget(const TeamConceptConstants& k, const Vector3& runner, float oppLineX) {
  const float channel =
      pesPenaltyAreaHalfWidth + (pitchHalfH - pesPenaltyAreaHalfWidth) * k.runnerChannelShare;
  float y = SideSign(runner.coords[1]) * channel;
  if (std::fabs(runner.coords[1]) > std::fabs(y)) y = runner.coords[1];
  return Vector3(oppLineX, y, 0.0f);
}

CounterSupport SideCounterSupporters(const TeamConceptConstants& k, const SideCounterInput& in, int runner,
                                     const std::array<int, 3>& previous) {
  CounterSupport s;
  const float attack = AttackHeading(in.dir);
  s.points[0] = Advance(in.ball, attack, k.supportAhead_m);
  s.points[1] = Advance(in.ball, TurnToward(kHalfTurn_deg, attack, k.supportTurn_deg), k.supportSide_m);
  s.points[2] = Advance(in.ball, TurnToward(kPlusY_deg, attack, k.supportTurn_deg), k.supportSide_m);
  constexpr float kNoneScore = 100000.0f;  // 0x1425c65fc
  for (int slot = 0; slot < 3; ++slot) {
    float best = kNoneScore;
    for (int i = 0; i < static_cast<int>(in.men.size()); ++i) {
      const CounterMan& m = in.men[i];
      if (i == in.holder || i == runner || !m.supportEligible) continue;
      if (std::find(s.men.begin(), s.men.begin() + slot, i) != s.men.begin() + slot) continue;
      if (slot == 0 && !Beyond(in.ball.coords[0], m.pos.coords[0], in.dir, k.supportMinAhead_m)) continue;
      if (slot == 1 && m.pos.coords[1] > in.ball.coords[1]) continue;
      if (slot == 2 && m.pos.coords[1] < in.ball.coords[1]) continue;
      float d = std::sqrt(DistSq(m.pos, s.points[slot]));
      if (previous[slot] == i) d += k.supportHoldBonus_m;
      if (d < best) best = d, s.men[slot] = i;
    }
  }
  return s;
}

bool SideCounterEnd(const TeamConceptConstants& k, bool inPlay, bool runnerStillRunning, float age_s,
                    const Vector3& ball, const Vector3& runner) {
  if (!inPlay || !runnerStillRunning) return true;
  if (age_s > k.counterRunGrace_s) {
    // PES compares the root distance against 1369 (37 squared) as written.
    if (std::sqrt(DistSq(ball, runner)) > k.counterRunnerFarSq ||
        std::fabs(ball.coords[0] - runner.coords[0]) > k.counterRunnerMaxDx_m)
      return true;
  }
  // ponytail: the trailing overlap test (a wider team-mate within 8 m of the
  // runner's lane, FUN_1405a4830/FUN_140596c30) is not reduced.
  return age_s > k.counterMaxAge_s;
}

}  // namespace pes

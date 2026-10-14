#include "onthepitch/pesdefenderdecisions.hpp"

#include <cmath>
#include <cstring>

#include "base/math/bluntmath.hpp"
#include "onthepitch/pesballphysics.hpp"
#include "pes/inflate.hpp"
#include "pes/uniform.hpp"

namespace pes {
namespace {

template <typename T>
T Load(const uint8_t* p) {
  T v;
  std::memcpy(&v, p, sizeof v);
  return v;
}

bool ReadRecordFloat(const std::vector<uint8_t>& rec, uint32_t at, float* out) {
  if (at + sizeof(float) > rec.size()) return false;
  *out = Load<float>(&rec[at]);
  return true;
}

bool ReadVa(const PeImage& exe, uint64_t va, float* out, std::string* error) {
  if (!exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out))) {
    *error = "defender decisions: .rdata unreadable";
    return false;
  }
  return true;
}

// One object's record out of the WESYS container constant_player.bin, the way
// LoadBallRecordConstants reads ball.o out of constant_match.bin.
bool ReadObject(const PesVfs& vfs, const char* name, std::vector<uint8_t>* rec,
                std::string* error) {
  auto blob = vfs.Read(kConstantPlayerPath, error);
  if (!blob) return false;
  auto table = ParseUniformTable(*blob, error);
  if (!table) return false;
  auto span = table->Find(name);
  if (!span) {
    *error = std::string(name) + " missing from " + kConstantPlayerPath;
    return false;
  }
  rec->assign(span->begin(), span->end());
  return true;
}

// The angle fold (0x140481b00): wrap at 360, fold at 180.
inline float WrapDeg(float a) {
  while (a > 180.0f) a -= 360.0f;
  while (a < -180.0f) a += 360.0f;
  return a;
}

// PES's logic frames per second: the tick rate the report's fps getter reads
// (FUN_141e532d0, live 54).
inline constexpr float kPesFramesPerSecond = 1.0f / pesball::kPesTick_s;

// PES frame count -> this engine's 10 ms ticks, the conversion
// pesballphysics.hpp uses for the ball's per-tick factors: one GF tick is
// 0.54 of a PES tick, so a PES frame count shrinks to that share.
inline int FramesToTicks(float pesFrames) {
  return static_cast<int>(pesFrames * (pesball::kPesTick_s / pesball::kGfTick_s) + 0.5f);
}

inline int ClampLevel(int level) { return level < 0 ? 0 : (level > 3 ? 3 : level); }

}  // namespace

std::optional<DefenderDecisionConstants> LoadDefenderDecisionConstants(
    const PeImage& exe, const PesVfs& vfs, std::string* error) {
  DefenderDecisionConstants c;
  bool ok = true;
  auto f = [&ok, &exe](uint64_t va, float* out, std::string* err) {
    ok &= ReadVa(exe, va, out, err);
  };
  // Standing tackle geometry.
  f(0x14259d60c, &c.tackleBallDistanceSq_m2, error);
  f(0x14259d618, &c.tackleBallHeadingAngle_deg, error);
  f(0x14258c264, &c.tackleContactHeight_m, error);
  f(0x1425cb30c, &c.tackleEngageAngle_deg, error);
  f(0x142565604, &c.tackleEngageGap_m, error);
  f(0x14259be60, &c.tackleBehindAngle_deg, error);
  f(0x14259be30, &c.tackleBehindTolerance_deg[0], error);
  f(0x1425cc680, &c.tackleBehindTolerance_deg[1], error);
  c.tackleBehindTolerance_deg[2] = 0.0f;  // A = 2: the register zeroed at 0x140970793
  f(0x14259be20, &c.tackleBehindExtraAngle_deg, error);
  f(0x14259da90, &c.tackleTouchLead_s[0], error);
  f(0x142588200, &c.tackleTouchLead_s[1], error);
  f(0x14259bdcc, &c.tackleTouchLead_s[2], error);
  f(0x142572348, &c.tackleLateralOffset_m, error);
  f(0x14259222c, &c.tackleLateralSpeed_kmh, error);
  f(0x142588210, &c.safetyCoverRadius_m, error);
  f(0x14259a764, &c.safetyCoverAngle_deg, error);
  // Slide.
  f(0x1425656c0, &c.slideDistanceBase_m, error);
  f(0x142572348, &c.slideDistancePerLevel_m, error);
  f(0x142565600, &c.slideBallHeight_m, error);
  // The slide's engagement and direction windows. The 112.5 deg slot is the
  // tackle's own, read twice because PES reads it twice; the 90 deg is the
  // slide's own direction window.
  f(0x1425cb30c, &c.slideEngageAngle_deg, error);
  f(0x14259be5c, &c.slideDirectionAngle_deg, error);
  // Block.
  f(0x14259bdd8, &c.blockRadiusNormal_m, error);
  f(0x14258c264, &c.blockRadiusPressing_m, error);
  f(0x14259bdf0, &c.blockRadiusLate_m, error);
  f(0x14259bdd8, &c.blockShotRadiusScale, error);
  f(0x14259d5d4, &c.blockFeintRadiusScale, error);
  f(0x14257a194, &c.blockClipProgress, error);
  f(0x142572348, &c.blockClipProgressLongPass, error);
  f(0x142565600, &c.blockClipProgressHighScale, error);
  f(0x142565600, &c.blockHighBall_m, error);
  f(0x14259be4c, &c.blockLooseSpeedBall_kmh, error);
  if (!ok) {
    *error = "defender decisions: PES2021.exe .rdata constants unmapped";
    return std::nullopt;
  }

  // The json records. Their disk offsets are the deserializer's own store
  // offsets: the record is a flat field array and the layout is the same in the
  // stock and the modded file (checked against both, report 12.5). Nothing is
  // pinned - the VFS resolves whichever constant_player.bin the install mounts.
  std::vector<uint8_t> rec;
  if (!ReadObject(vfs, "tackle.o", &rec, error)) return std::nullopt;
  if (!ReadRecordFloat(rec, 0x0c, &c.tackleSafetyRateLastLine) ||
      !ReadRecordFloat(rec, 0x10, &c.tackleSafetyRateNoCover)) {
    *error = "tackle.o: safety-rate slots past end";
    return std::nullopt;
  }
  if (!ReadObject(vfs, "block.o", &rec, error)) return std::nullopt;
  if (!ReadRecordFloat(rec, 0x40, &c.blockNormal_s) ||
      !ReadRecordFloat(rec, 0x44, &c.blockSpeedBall_s)) {
    *error = "block.o: window slots past end";
    return std::nullopt;
  }
  return c;
}

DefenderReaction ChooseReaction(const DefenderReactionInput& in) {
  // The walk, in PES's table order: the first predicate that returns true is
  // adopted and the rest of the table is not even read.
  if (in.receive) return DefenderReaction::kReceive;
  if (in.tackle) return DefenderReaction::kTackle;
  if (in.slide) return DefenderReaction::kSlide;
  if (in.block) return DefenderReaction::kBlock;
  if (in.jostleForPass) return DefenderReaction::kJostleForPass;
  return DefenderReaction::kNone;
}

bool StandingTackleBallGate(const DefenderDecisionConstants& c, float ballAheadDistance_m,
                            float ballAheadHeadingAngle_deg) {
  const float d2 = ballAheadDistance_m * ballAheadDistance_m;
  if (d2 > c.tackleBallDistanceSq_m2) return false;
  return std::fabs(WrapDeg(ballAheadHeadingAngle_deg)) <= c.tackleBallHeadingAngle_deg;
}

bool StandingTackleFromBehindAllowed(const DefenderDecisionConstants& c, bool carrierBehind,
                                     float approachOffFacing_deg, float headingDelta_deg,
                                     int aggressionLevel) {
  if (!carrierBehind) return true;
  const int level = aggressionLevel < 0 ? 0
                     : (aggressionLevel > 2 ? 2 : aggressionLevel);
  const float tolerance = c.tackleBehindTolerance_deg[level];
  const float approach = std::fabs(WrapDeg(approachOffFacing_deg));
  if (approach >= tolerance) return true;
  const float delta = std::fabs(WrapDeg(headingDelta_deg));
  return delta > c.tackleBehindExtraAngle_deg;
}

bool StandingTackleEngageWindow(const DefenderDecisionConstants& c, float engageAngle_deg,
                                float engagedGap_m) {
  if (std::fabs(WrapDeg(engageAngle_deg)) > c.tackleEngageAngle_deg) return false;
  return engagedGap_m <= c.tackleEngageGap_m;
}

float SafetyProgressSine(float clipProgress01) {
  return std::sin(90.0f * clipProgress01 * blunted::pi / 180.0f);
}

bool StandingTackleSafetyVeto(const DefenderDecisionConstants& c, float sf, bool noCover,
                              bool lastLine, float eventAgeTicks) {
  if (!noCover && !lastLine) return false;
  const int gateTicks = FramesToTicks(kPesFramesPerSecond / static_cast<float>(
                                                          c.safetyEventFramesDivisor));
  if (eventAgeTicks <= static_cast<float>(gateTicks)) return false;
  if (noCover && sf < c.tackleSafetyRateNoCover) return true;
  if (lastLine && sf < c.tackleSafetyRateLastLine) return true;
  return false;
}

int TackleTouchLeadTicks(const DefenderDecisionConstants& c, int aggressionLevel) {
  const int level = aggressionLevel < 0 ? 0
                     : (aggressionLevel > 2 ? 2 : aggressionLevel);
  return FramesToTicks(c.tackleTouchLead_s[level] * kPesFramesPerSecond);
}

int SlideAggressionLevel(const SlideAggressionInput& in) {
  int L = 0;
  if (in.enforcerStyle) ++L;
  if (in.aggressiveTeamSheet) ++L;
  if (in.deepDefensiveMode) L += 2;
  if (in.staminaUnder30) ++L;
  return ClampLevel(L);
}

float SlideReach_m(const DefenderDecisionConstants& c, int aggressionLevel) {
  return c.slideDistanceBase_m + c.slideDistancePerLevel_m * static_cast<float>(aggressionLevel);
}

bool SlideEngageWindow(const DefenderDecisionConstants& c, float targetBearing_deg,
                       float heading_deg) {
  // The raw refuses when the angle is >= the limit, so the boundary is allowed.
  return std::fabs(WrapDeg(targetBearing_deg - heading_deg)) < c.slideEngageAngle_deg;
}

bool SlideDirectionWindow(const DefenderDecisionConstants& c, float slideBearing_deg,
                          float heading_deg) {
  return std::fabs(WrapDeg(slideBearing_deg - heading_deg)) < c.slideDirectionAngle_deg;
}

int SlideAggressionOwnBox(const DefenderDecisionConstants& c, bool inOwnBox, bool deepDefensiveMode,
                          int aggressionLevel) {
  if (!inOwnBox) return aggressionLevel;
  if (aggressionLevel < 3) return -1;  // refuse the slide inside his own box below L = 3
  return deepDefensiveMode ? 2 : 0;
}

float BlockRadius_m(const DefenderDecisionConstants& c, bool pressing, bool shot, bool feint) {
  float radius = pressing ? c.blockRadiusPressing_m : c.blockRadiusNormal_m;
  if (shot) radius *= c.blockShotRadiusScale;
  if (feint) radius *= c.blockFeintRadiusScale;
  return radius;
}

float BlockClipProgressFraction(const DefenderDecisionConstants& c, bool longPass, bool highBall) {
  float fraction = longPass ? c.blockClipProgressLongPass : c.blockClipProgress;
  if (highBall) fraction *= c.blockClipProgressHighScale;
  return fraction;
}

bool BlockClipWindowOpen(float clipProgress01, float fraction) {
  return clipProgress01 >= fraction;
}

float BlockLooseWindow_s(const DefenderDecisionConstants& c, float ballSpeed_kmh) {
  return ballSpeed_kmh >= c.blockLooseSpeedBall_kmh ? c.blockSpeedBall_s : c.blockNormal_s;
}

}  // namespace pes

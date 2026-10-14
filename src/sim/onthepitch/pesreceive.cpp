#include "onthepitch/pesreceive.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "onthepitch/pesdefenderdecisions.hpp"

namespace pes {
namespace {

constexpr float kDegPerRad = 57.29578f;

// The angle fold (0x140481b00): wrap at 360, fold at 180.
float WrapDeg(float a) {
  while (a > 180.0f) a -= 360.0f;
  while (a < -180.0f) a += 360.0f;
  return a;
}
float AbsDiffDeg(float a, float b) { return std::fabs(WrapDeg(a - b)); }

// Interception age: `imul edx, eax, imm8` on the integer fps, then the
// 0x51eb851f / shr 5 divide, which is / 100: the immediate is a percentage.
constexpr uint64_t kInterceptAgeVa = 0x1409fa1d0;
constexpr uint8_t kInterceptAgeOpcode[] = {0x6b, 0xd0};
constexpr uint8_t kPercentDivide[] = {0xb8, 0x1f, 0x85, 0xeb, 0x51};  // mov eax, 0x51eb851f
constexpr int kPercent = 100;

// trap.o disk-record offsets. The deserializer (FUN_141e681c0) stores these
// fields at +0xd8..+0xe8 and +0x1b4..+0x1c4; the disk record is laid out
// differently (the 4-byte slots run 0x1a0..0x1b0 and 0x3c0..0x3d0), checked
// against the mounted constant_player.bin by the field values the report's
// live dump gives.
constexpr uint32_t kTrapBusyTime = 0x1a0;
constexpr uint32_t kTrapCheckMyBallPass = 0x1a4;
constexpr uint32_t kTrapRunTurnAngle = 0x1a8;
constexpr uint32_t kTrapStopTurnAngle = 0x1ac;
constexpr uint32_t kTrapTurnClampUse = 0x1b0;
constexpr uint32_t kTrapDefence = 0x3c0;
constexpr uint32_t kTrapDefenceKeep = 0x3c4;
constexpr uint32_t kTrapOffence = 0x3c8;
constexpr uint32_t kTrapShoot = 0x3cc;
constexpr uint32_t kTrapShootSpeedBall = 0x3d0;

bool RecordFloat(const std::vector<uint8_t>& rec, uint32_t at, float* out) {
  if (at + sizeof *out > rec.size()) return false;
  std::memcpy(out, &rec[at], sizeof *out);
  return true;
}
bool RecordFlag(const std::vector<uint8_t>& rec, uint32_t at, bool* out) {
  if (at >= rec.size()) return false;
  *out = rec[at] != 0;
  return true;
}

bool IsKick(BallEventKind k) { return k == BallEventKind::kPass || k == BallEventKind::kHighPass; }

}  // namespace

float HeadingDeg(float x, float y) { return std::atan2(y, x) * kDegPerRad; }

std::optional<ReceiveConstants> LoadReceiveConstants(const PeImage& exe, const PesVfs& vfs,
                                                     std::string* error) {
  ReceiveConstants c;
  bool ok = true;
  auto f = [&](uint64_t va, float* out) {
    ok &= exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
  };
  f(0x142588200, &c.lockStatCoefficient_s);
  f(0x14259be2c, &c.statFloor);
  f(0x1425c48cc, &c.statCeil);
  f(0x1425cb2fc, &c.statSpan);
  f(0x14259bdd4, &c.highBallLock_m);
  f(0x142596e6c, &c.shootLockSpeed_kmh);
  f(0x14259ce38, &c.mpsToKmh);
  f(0x14258c264, &c.ownKickLockScale);
  f(0x142591140, &c.shotThroughAngle_deg);
  f(0x14259a760, &c.shotThroughSpeed_kmh);
  f(0x1425f1f24, &c.interceptSpeedSq_m2s2);
  f(0x14259222c, &c.plannedTouch_m);
  f(0x14256ae1c, &c.lineMargin_m);
  f(0x14259be20, &c.lineGuardBallAngle_deg);
  f(0x14259be60, &c.lineGuardBodyAngle_deg);
  f(0x14259bddc, &c.lineGuardAhead_m);
  f(0x14263e390, &c.clampBallSpeedSq_m2s2);
  f(0x14259be30, &c.runningFacing_deg);
  f(0x14263e394, &c.runningSpeedSq);
  f(0x14259be60, &c.arbitrationAngle_deg);
  f(0x14258c264, &c.arbitrationRadius_m);
  f(0x14259222c, &c.arbitrationLength_m);
  f(0x14253ee30, &c.ballDistanceTableMaxSq_m2);
  f(0x1425e5ab8, &c.receivingPointMargin_m);
  if (!ok) {
    *error = "receive: PES2021.exe .rdata constants unmapped";
    return std::nullopt;
  }
  uint8_t code[sizeof kInterceptAgeOpcode + 1 + sizeof kPercentDivide] = {};
  if (!exe.ReadVa(kInterceptAgeVa, sizeof code, code) ||
      !std::equal(std::begin(kInterceptAgeOpcode), std::end(kInterceptAgeOpcode), code) ||
      !std::equal(std::begin(kPercentDivide), std::end(kPercentDivide),
                  code + sizeof kInterceptAgeOpcode + 1)) {
    *error = "receive: unexpected instruction at 0x1409fa1d0";
    return std::nullopt;
  }
  c.interceptAgePercent = code[sizeof kInterceptAgeOpcode];

  std::vector<uint8_t> rec;
  if (!ReadConstantPlayerObject(vfs, "trap.o", &rec, error)) return std::nullopt;
  if (!RecordFloat(rec, kTrapBusyTime, &c.busyTime_s) ||
      !RecordFlag(rec, kTrapCheckMyBallPass, &c.checkMyBallPass) ||
      !RecordFloat(rec, kTrapRunTurnAngle, &c.runTrapStopTurnAngle_deg) ||
      !RecordFloat(rec, kTrapStopTurnAngle, &c.stopTrapStopTurnAngle_deg) ||
      !RecordFlag(rec, kTrapTurnClampUse, &c.turnClampUse) ||
      !RecordFloat(rec, kTrapDefence, &c.lockDefence_s) ||
      !RecordFloat(rec, kTrapDefenceKeep, &c.lockDefenceKeep_s) ||
      !RecordFloat(rec, kTrapOffence, &c.lockOffence_s) ||
      !RecordFloat(rec, kTrapShoot, &c.lockShoot_s) ||
      !RecordFloat(rec, kTrapShootSpeedBall, &c.lockShootSpeedBall_s)) {
    *error = "trap.o: field past end";
    return std::nullopt;
  }
  return c;
}

float RetouchLockout_s(const ReceiveConstants& c, const LockoutInput& in) {
  // A held ball: offence for its holder, defenceKeep for everyone else, and
  // nothing else applies (FUN_1409fa720's first branch).
  if (in.ballOwned) return in.ownerIsMe ? c.lockOffence_s : c.lockDefenceKeep_s;

  float s = 0.0f;
  if (in.ballStat > c.statFloor) s = in.ballStat < c.statCeil ? (in.ballStat - c.statFloor) / c.statSpan : 1.0f;
  const float statTerm = c.lockStatCoefficient_s - s * s * c.lockStatCoefficient_s;

  const bool kick = IsKick(in.eventKind);
  float L = c.lockOffence_s;
  if (!(kick && in.eventByMyTeam)) L = statTerm + c.lockDefence_s;
  if (in.eventKind == BallEventKind::kShot ||
      (in.eventKind == BallEventKind::kHighPass && in.ballHeight_m > c.highBallLock_m)) {
    L = statTerm + (in.ballSpeed_mps * c.mpsToKmh <= c.shootLockSpeed_kmh ? c.lockShoot_s
                                                                          : c.lockShootSpeedBall_s);
  }
  if (kick && in.eventByMe) {
    L *= c.ownKickLockScale;
  } else if (in.previousEventByMe && in.previousEventAge_s < L + L) {
    L += L;
  }
  return L;
}

bool RetouchAllowed(const ReceiveConstants& c, const LockoutInput& in, float eventAge_s,
                    float fps) {
  const long ageFrames = std::lround(eventAge_s * fps);
  return ageFrames >= static_cast<long>(fps * RetouchLockout_s(c, in));
}

bool ShotRunsThrough(const ReceiveConstants& c, const ShotThroughInput& in) {
  if (!in.ownTeamShot) return false;
  const float toPostA = HeadingDeg(in.goalX - in.ballX, in.goalHalfWidth - in.ballY);
  const float toPostB = HeadingDeg(in.goalX - in.ballX, -in.goalHalfWidth - in.ballY);
  const float mouth = AbsDiffDeg(toPostB, toPostA);
  const float mid = toPostA + WrapDeg(toPostB - toPostA) * 0.5f;
  if (AbsDiffDeg(HeadingDeg(in.ballVelX, in.ballVelY), mid) >= mouth + c.shotThroughAngle_deg)
    return false;
  const float speed = std::hypot(in.ballVelX, in.ballVelY);
  return speed * c.mpsToKmh >= c.shotThroughSpeed_kmh;
}

bool PassGetSelected(const ReceiveConstants&, const PassGetInput& in) {
  if (!in.iAmPassTarget || in.newestEventIsMyKick || in.inKickBeforeContact) return false;
  return in.arbitrationWins || !in.mateNearBallOwnsIt;
}

bool ArbitrationWins(const ReceiveConstants& c, const ArbitrationInput& in) {
  if (!in.otherTargetValid) return false;
  if (AbsDiffDeg(in.ballHeading_deg, in.otherHeading_deg) <= c.arbitrationAngle_deg) return false;
  // The capsule (FUN_140a8ff10): from him along his facing, me within the
  // radius of that segment.
  const float dx = std::cos(in.otherHeading_deg / kDegPerRad);
  const float dy = std::sin(in.otherHeading_deg / kDegPerRad);
  const float mx = in.myX - in.otherX, my = in.myY - in.otherY;
  const float along = std::clamp(mx * dx + my * dy, 0.0f, c.arbitrationLength_m);
  return std::hypot(mx - along * dx, my - along * dy) <= c.arbitrationRadius_m;
}

bool WithinBallDistanceTable(const ReceiveConstants& c, float ballDistanceSq_m2) {
  return ballDistanceSq_m2 <= c.ballDistanceTableMaxSq_m2;
}

Point2 ReceivingPoint(const ReceiveConstants& c, const std::vector<Point2>& ballPath, Point2 me,
                      float halfLength, float halfWidth) {
  Point2 p = me;
  float best = -1.0f;
  for (const Point2& b : ballPath) {
    const float d = (b.x - me.x) * (b.x - me.x) + (b.y - me.y) * (b.y - me.y);
    if (best < 0.0f || d < best) {
      best = d;
      p = b;
    }
  }
  if (std::fabs(p.x) > halfLength + c.receivingPointMargin_m) {
    const float x = p.x < 0.0f ? -halfLength : halfLength;
    p.y = p.y - (p.y - me.y) * (p.x - x) / (p.x - me.x);
    p.x = x;
  }
  if (std::fabs(p.y) > halfWidth + c.receivingPointMargin_m) {
    const float y = p.y < 0.0f ? -halfWidth : halfWidth;
    p.x = p.x - (p.y - y) * (p.x - me.x) / (p.y - me.y);
    p.y = y;
  }
  return p;
}

FirstTouch ChooseFirstTouch(const ReceiveConstants& c, const FirstTouchInput& in, float fps) {
  const FirstTouch control{TouchKind::kControl, in.bodyHeading_deg, c.lineMargin_m};
  const long ageFrames = std::lround(in.eventAge_s * fps);

  // The interception rule (FUN_1409fa0b0, the AI path): a fresh, fast ball
  // from the other team is a plain trap, and the request is final.
  const long interceptFrames = static_cast<long>(fps) * c.interceptAgePercent / kPercent;
  if (!in.eventByMyTeam && ageFrames < interceptFrames &&
      in.ballSpeedSq_m2s2 > c.interceptSpeedSq_m2s2)
    return control;

  // A DELAY / PRESS defender receiving just controls it.
  if (in.pressOrDelay) return control;

  FirstTouch out = control;
  if (in.hasPlannedDirection)
    out = {TouchKind::kDirectional, in.plannedHeading_deg, c.plannedTouch_m};

  // The touch-line guard: a ball running along the touch, a body turned away
  // from it, and the point 1.5 m along it inside the margin of a line.
  if (AbsDiffDeg(in.ballHeading_deg, out.heading_deg) < c.lineGuardBallAngle_deg &&
      AbsDiffDeg(in.bodyHeading_deg, out.heading_deg) > c.lineGuardBodyAngle_deg) {
    const float ax = in.posX + std::cos(out.heading_deg / kDegPerRad) * c.lineGuardAhead_m;
    const float ay = in.posY + std::sin(out.heading_deg / kDegPerRad) * c.lineGuardAhead_m;
    if (std::fabs(ax) >= in.halfLength - c.lineMargin_m ||
        std::fabs(ay) >= in.halfWidth - c.lineMargin_m)
      out = control;
  }

  // The turn clamp (FUN_1409fbce0).
  if (!c.turnClampUse) return out;
  if (!c.checkMyBallPass && in.eventIsKick && in.eventByMyTeam) return out;
  if (in.eventByMe) return out;
  if (ageFrames > static_cast<long>(fps * c.busyTime_s)) return out;
  if (in.ballSpeedSq_m2s2 < c.clampBallSpeedSq_m2s2) return out;
  float allowance = c.stopTrapStopTurnAngle_deg;
  const float scaled = in.moveSpeed_mps * fps;
  if (AbsDiffDeg(in.bodyHeading_deg, in.moveHeading_deg) < c.runningFacing_deg &&
      scaled * scaled > c.runningSpeedSq)
    allowance = c.runTrapStopTurnAngle_deg;
  if (AbsDiffDeg(out.heading_deg, in.bodyHeading_deg) > allowance) out = control;
  return out;
}

}  // namespace pes

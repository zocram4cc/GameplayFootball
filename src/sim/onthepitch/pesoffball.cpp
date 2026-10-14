#include "onthepitch/pesoffball.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pes/uniform.hpp"

namespace pes::offball {
namespace {

// --- literals the reports give without an .rdata VA (provenance per line) ---
// fps multiples use PES's 54 fps (FUN_141e532d0 = [0x14351ae54]).
constexpr float kPesFps = 54.0f;
// Minimum run time before the Chance stop may fire: int(fps·0.2+0.5) (0x140607a60).
constexpr float kChanceMinFrames = 11.0f;
// The common test's late line margin switches at int(0.8·fps+0.5) (0x140688c00 step 14).
constexpr float kLateFrames = 43.0f;
constexpr float kLineMarginEarly_m = 1.5f;  // step 14
constexpr float kLineMarginLate_m = 0.7f;   // step 14
constexpr float kTargetReached_m = 1.0f;     // head: |pos - AI target| < 1 m
constexpr float kGoalLineMargin_m = 2.5f;    // head: s·x > F4 + 2.5
constexpr float kFarBase_m = 40.0f;          // step 8: d² > (k + 40)²
constexpr float kFarBehind_m = 8.0f;         // step 8: 8 m behind the ball (13 for Counter)
constexpr float kFarBehindCounter_m = 13.0f;
constexpr float kNearDx_m = 4.0f;            // step 6: |dx| <= 4 -> k = 10
constexpr float kNearDxFar_m = 10.0f;        // step 6: ramp to 0 at 10 m
constexpr float kNearK_m = 10.0f;
constexpr float kWideDz_m = 43.0f;           // step 13: |P.z - ball.z| > 43
// FieldInfo F0, F4, F5, F6 (pes-re-fieldinfo.md; also gametypes.hpp).
constexpr float kFieldHalfLength_m = 52.56f;
constexpr float kGoalAreaLineX_m = 47.06f;
constexpr float kGoalAreaHalfWidth_m = 9.16f;
constexpr float kPenaltyAreaLineX_m = 36.14f;
constexpr float kPenaltyAreaHalfWidth_m = 20.12f;  // F7
// Lanes (FUN_140a71990): z_k = F1 - 2 - 2k, k = 0..32.
constexpr int kLaneCount = 33;
constexpr float kLaneSpacing_m = 2.0f;
// Blocked-lane weight w: 1 within 55 deg of T.dir, linear to 0 at 88 (59ba60 pre-pass).
constexpr float kBlockFullAngle_deg = 55.0f;
constexpr float kBlockZeroAngle_deg = 88.0f;
// The S table's "empty" value (0xff counted as 100, FUN_140462470).
constexpr int kLaneEmpty = 100;
constexpr float kLaneDiscRadius_m = 1.0f;  // FUN_140a77b80 disc r <= 1
// Moving-runner heading gate: speed > 3000/(fps·3600) m/frame, dev > 130 deg skips.
constexpr float kMovingSpeed_mps = 3000.0f / 3600.0f;
constexpr float kMovingHeadingMax_deg = 130.0f;
// Receiver-side bonus (0x14059cd42): d > 2, ramp over 3 m, +50, +100.
constexpr float kRecvGap_m = 2.0f;
constexpr float kRecvRamp_m = 3.0f;
constexpr int kRecvRampScore = 50;
constexpr int kRecvBonus = 100;
// Neighbour-lane budget FUN_140592fd0: need limit >= 15; reject a neighbour
// under limit - 15 and under 85.
constexpr int kBudgetMinLimit = 15;
constexpr int kBudgetMargin = 15;
constexpr int kBudgetNeighbourMax = 85;
// Cone penalties a4e720(id, bearing, 20/40 deg, 12 m) -> -50/-25.
constexpr float kConeNarrow_deg = 20.0f, kConeWide_deg = 40.0f, kConeRadius_m = 12.0f;
constexpr int kConeNarrowPen = -50, kConeWidePen = -25;
// Three nearest opponents within 15 m (225 m²) on the lane: |dz| <= 3, dev <= 90, perp² < 1.
constexpr int kLaneOppCount = 3;
constexpr float kLaneOppRangeSq_m2 = 225.0f;
constexpr float kLaneOppDz_m = 3.0f;
constexpr float kLaneOppDev_deg = 90.0f;
constexpr float kLaneOppPerpSq_m2 = 1.0f;
constexpr float kLaneOppFull_m = 1.0f, kLaneOppZero_m = 2.5f;
constexpr int kLaneOppPen = -50;
// Wide penalty: |z_k| > 14 -> -50·ramp(s·ball.x, 10 -> F6)·ramp(|ball.z - z_k|, 15 -> 30).
constexpr float kWideLane_m = 14.0f;
constexpr float kWideBallFrom_m = 10.0f;
constexpr float kWideDzFrom_m = 15.0f, kWideDzTo_m = 30.0f;
constexpr float kWidePen = 50.0f;
// Commit FUN_140664ee0 first gates: |fold(bearing(ball->C) - T.dir)| <= 90, |ball.z - C.z| <= 48.
constexpr float kCommitDev_deg = 90.0f;
constexpr float kCommitDz_m = 48.0f;
// FUN_14066d720 (far lane in open play): outside the box zone (|ball.z| < F7
// and s·ball.x > F6 - 5), a runner more than 10 m ahead of the ball or a lane
// more than 20 m across rejects lanes over 27 m (729 m²) from the ball.
constexpr float kFarLaneBoxMargin_m = 5.0f;
constexpr float kFarLaneAhead_m = 10.0f;
constexpr float kFarLaneAcross_m = 20.0f;
constexpr float kFarLaneBallSq_m2 = 729.0f;
// Supporter (FUN_1405eb980): 0.7·angle + distance + 15 under 10 m.
constexpr float kSupporterAngleWeight = 0.7f;
constexpr float kSupporterClose_m = 10.0f;
constexpr float kSupporterClosePen = 15.0f;
// Support distance (FUN_1405e0c60, pes-re-tactics.md 4.1).
constexpr float kSupportBase_m = 5.0f;
constexpr float kSupportRangeGain_m = 3.0f;
constexpr float kSupportRangeSpan = 9.0f;  // (SR - 1)/9, 9 = 0x14259d60c
constexpr float kSupportDeep_m = 7.0f;
constexpr float kSupportLong_m = 4.0f;
constexpr float kSupportLineMargin_m = 10.0f;
constexpr float kSupportFwdFull_deg = 45.0f, kSupportFwdZero_deg = 90.0f;
constexpr float kSupportWide_m = 3.0f;
constexpr float kSupportWideBand_m = 5.0f;
constexpr float kSupportBoxBand_m = 10.0f;
constexpr float kSupportFloor_m = 6.0f;
constexpr float kSupportDead_m = 8.0f;
constexpr float kFieldHalfWidthHalf = 0.5f;  // -0.5·F1

float Fold180(float a) {
  a = std::fmod(a, 360.0f);
  if (a > 180.0f) a -= 360.0f;
  if (a < -180.0f) a += 360.0f;
  return a;
}

float Bearing(Vec2f from, Vec2f to) {
  return std::atan2(to.z - from.z, to.x - from.x) * (180.0f / 3.14159265f);
}

float Dist2(Vec2f a, Vec2f b) { return (a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z); }

// ramp(v; a -> 0, b -> 1), either direction.
float Ramp(float v, float a, float b) {
  if (a == b) return v >= b ? 1.0f : 0.0f;
  return std::clamp((v - a) / (b - a), 0.0f, 1.0f);
}

float LaneZ(int k) { return kFieldHalfWidth_m - kLaneSpacing_m - kLaneSpacing_m * k; }

bool ReadF(const PeImage& exe, uint64_t va, float* out, bool* ok) {
  *ok &= exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
  return *ok;
}

// The S table cell (FUN_140a77130 + FUN_140462470): the 1 m disc around
// (x, z) on team t's occupancy grid, mean of the cells, 100 when empty.
int LaneFreedom(const OffBallConstants& k, const std::vector<Vec2f>& players, float x, float z) {
  const float cw = 2.0f * kFieldHalfLength_m / k.gridCellsX;
  const float ch = 2.0f * kFieldHalfWidth_m / k.gridCellsZ;
  const int nx = static_cast<int>(k.gridCellsX), nz = static_cast<int>(k.gridCellsZ);
  const float cx = std::clamp(x, -kFieldHalfLength_m, kFieldHalfLength_m);
  int sum = 0, count = 0;
  const int i0 = std::max(0, static_cast<int>((cx + kFieldHalfLength_m) / cw) - 2);
  const int j0 = std::max(0, static_cast<int>((z + kFieldHalfWidth_m) / ch) - 2);
  for (int i = i0; i < std::min(nx, i0 + 5); ++i) {
    for (int j = j0; j < std::min(nz, j0 + 5); ++j) {
      const Vec2f c{-kFieldHalfLength_m + (i + 0.5f) * cw, -kFieldHalfWidth_m + (j + 0.5f) * ch};
      if (Dist2(c, {cx, z}) > kLaneDiscRadius_m * kLaneDiscRadius_m) continue;
      int v = kLaneEmpty;
      for (const Vec2f& p : players) {
        const float d2 = Dist2(c, p);
        if (d2 >= k.gridRadiusSq_m2 || v <= 0) continue;
        v = std::max(0, static_cast<int>(v - (k.gridFalloff_m - std::sqrt(d2)) * kLaneEmpty *
                                                 k.gridScale));
      }
      sum += v;
      ++count;
    }
  }
  return count ? sum / count : kLaneEmpty;
}

// a4e720 / a4e3f0: players inside the cone (|dev| <= width/2) within radius.
int InCone(Vec2f from, float bearing_deg, float width_deg, float radius_m,
           const std::vector<Vec2f>& players) {
  int n = 0;
  for (const Vec2f& p : players) {
    if (Dist2(from, p) >= radius_m * radius_m) continue;
    if (std::fabs(Fold180(Bearing(from, p) - bearing_deg)) <= width_deg * 0.5f) ++n;
  }
  return n;
}

}  // namespace

std::optional<OffBallConstants> LoadOffBallConstants(const PeImage& exe, const PesVfs& vfs,
                                                     std::string* error) {
  OffBallConstants k{};
  bool ok = true;
  ReadF(exe, 0x14253ee30, &k.chanceScoreBase, &ok);
  ReadF(exe, 0x142598578, &k.laneAhead_m, &ok);
  ReadF(exe, 0x14257a190, &k.blockConeFlat, &ok);
  ReadF(exe, 0x14259b89c, &k.blockConeSlope, &ok);
  ReadF(exe, 0x14259bdf0, &k.blockRadius_m, &ok);
  ReadF(exe, 0x14259be04, &k.reachDhFrom_m, &ok);
  ReadF(exe, 0x142591140, &k.reachDhTo_m, &ok);
  ReadF(exe, 0x1425c7e70, &k.reachDhDiv_m, &ok);
  ReadF(exe, 0x14259be0c, &k.reachBase_m, &ok);
  ReadF(exe, 0x14259222c, &k.reachDhGain_m, &ok);
  ReadF(exe, 0x1425cd82c, &k.reachCap_m, &ok);
  ReadF(exe, 0x1425aa1bc, &k.awareFrom, &ok);
  ReadF(exe, 0x1425c594c, &k.awareTo, &ok);
  ReadF(exe, 0x14259be1c, &k.awareSpan, &ok);
  ReadF(exe, 0x14259be18, &k.awareBonus_m, &ok);
  ReadF(exe, 0x1425cb308, &k.gridCellsX, &ok);
  ReadF(exe, 0x1425cf878, &k.gridCellsZ, &ok);
  ReadF(exe, 0x14259be10, &k.gridRadiusSq_m2, &ok);
  ReadF(exe, 0x1425656c0, &k.gridFalloff_m, &ok);
  ReadF(exe, 0x142592228, &k.gridScale, &ok);
  ReadF(exe, 0x142588210, &k.fallbackSide_m, &ok);
  ReadF(exe, 0x1425c4c18, &k.predNearSq_m2, &ok);
  ReadF(exe, 0x1425cb48c, &k.predMidSq_m2, &ok);
  ReadF(exe, 0x1425cb46c, &k.predNear_s, &ok);
  ReadF(exe, 0x14259b89c, &k.predMid_s, &ok);
  ReadF(exe, 0x14257a190, &k.predFar_s, &ok);
  ReadF(exe, 0x142588200, &k.predLead_s, &ok);
  ReadF(exe, 0x142590d98, &k.predOffBall_s, &ok);
  uint8_t window = 0;
  ok &= exe.ReadVa(0x143449d88, 1, &window);
  k.laneWindow = window;
  if (!ok) {
    *error = "off-ball: PES2021.exe constants unmapped";
    return std::nullopt;
  }

  auto blob = vfs.Read(kConstantTeamPath, error);
  if (!blob) return std::nullopt;
  auto table = ParseUniformTable(*blob, error);
  if (!table) return std::nullopt;
  auto vision = table->Find("selectorVision.o");
  auto space = table->Find("spaceRun.o");
  if (!vision || !space) {
    *error = std::string("selectorVision.o / spaceRun.o missing from ") + kConstantTeamPath;
    return std::nullopt;
  }
  // selectorVision.o: six u32 offsets (sorted keys) to 5 × i32 lists
  // (pes-re-constant-team.md 2.3).
  for (int r = 0; r < kCapRunCount; ++r) {
    uint32_t at = 0;
    if (vision->size() < 4u * (r + 1)) return *error = "selectorVision.o truncated", std::nullopt;
    std::memcpy(&at, vision->data() + 4 * r, 4);
    if (at + 4u * kAttackLevels > vision->size())
      return *error = "selectorVision.o list past end", std::nullopt;
    for (int l = 0; l < kAttackLevels; ++l) {
      int32_t v = 0;
      std::memcpy(&v, vision->data() + at + 4 * l, 4);
      k.offerNum[r][l] = static_cast<uint8_t>(v);
    }
  }
  // spaceRun.o chanceScore: record +0x1c = blob +0x14; the builder passes its
  // low byte (pes-re-run-targets.md 0).
  constexpr uint32_t kChanceScoreAt = 0x14;
  if (space->size() < kChanceScoreAt + 4) return *error = "spaceRun.o truncated", std::nullopt;
  k.chanceScoreLimit = (*space)[kChanceScoreAt];
  return k;
}

int ActionRank(int state) {
  switch (state) {
    case 0x23: case 0x24: case 0x25: case 0x2c: case 0x2d: case 0x2e: case 0x3a: case 0x3b:
      return 0;
    case 0x1d: case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x27: case 0x28: case 0x2a:
    case 0x2b: case 0x33: case 0x34: case 0x35: case 0x3c:
      return 2;
    case 0x10: case 0x26: case 0x2f: case 0x36: case 0x37: case 0x69: case 0x6a: case 0x6b:
      return 3;
    case 0x22: case 0x32: case 0x3d: case 0x6d:
      return 4;
    default:
      return 5;
  }
}

bool MayTake(int run, int current) { return ActionRank(run) < ActionRank(current); }

int RunCap(const OffBallConstants& k, CapRun run, int attackLevel, bool tikiTaka) {
  if (tikiTaka) {
    if (run == kCapSecondLine || run == kCapPullAway) return 0;
    if (run == kCapChance) return 1;
  }
  return k.offerNum[run][std::clamp(attackLevel, 0, kAttackLevels - 1)];
}

std::vector<int> SelectRunners(const std::array<float, kSlots>& scores, int running, int cap,
                               int perTickCap, bool chance) {
  std::vector<int> taken;
  if (!(running < cap || chance)) return taken;
  std::array<int, kSlots> order;
  for (int i = 0; i < kSlots; ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(),
                   [&](int a, int b) { return scores[a] > scores[b]; });
  constexpr int kMaxTakes = 10;  // the walk's own bound
  for (int s : order) {
    if (scores[s] <= 0.0f) break;
    taken.push_back(s);
    ++running;
    if (running >= cap || static_cast<int>(taken.size()) >= perTickCap ||
        static_cast<int>(taken.size()) >= kMaxTakes)
      break;
  }
  return taken;
}

float ChanceScore(const OffBallConstants& k, float s, float runnerX) {
  return k.chanceScoreBase + s * runnerX;
}

float ChanceReach_m(const OffBallConstants& k, float dz_m, float awareness) {
  // As written: no term at or under 12 m, then (dz - 30)/-18 unclamped.
  const float dh = dz_m > k.reachDhFrom_m ? (dz_m - k.reachDhTo_m) / k.reachDhDiv_m : 0.0f;
  float bonus = 0.0f;
  if (awareness > k.awareTo)
    bonus = k.awareBonus_m;
  else if (awareness > k.awareFrom)
    bonus = (awareness - k.awareFrom) / k.awareSpan * k.awareBonus_m;
  // ponytail: the 24/26/22 floors need "runners needing service", FUN_1405a5b20
  // and FUN_14060a100, none decoded; base 15 always (21 is the no-plan variant).
  const float reach = std::min(k.reachBase_m + dh * k.reachDhGain_m + bonus, k.reachCap_m);
  return reach + bonus;  // the accept test adds the bonus again
}

int DefenderPredictFrames(const OffBallConstants& k, float runnerBallDist2_m2) {
  // int(fps·x + 0.5) each, as the decompile truncates each term separately.
  auto frames = [](float s) { return static_cast<int>(kPesFps * s + 0.5f); };
  const float a = runnerBallDist2_m2 < k.predNearSq_m2  ? k.predNear_s
                  : runnerBallDist2_m2 < k.predMidSq_m2 ? k.predMid_s
                                                        : k.predFar_s;
  // ponytail: the branches for a controlled, action-4/5/7 or ball-carrying
  // defender and the pass-target tail are not reached by GF, whose defenders
  // carry no PES action state; the plain off-ball lead applies.
  return static_cast<uint8_t>(frames(a) + frames(k.predLead_s)) + frames(k.predOffBall_s);
}

std::optional<Vec2f> ChanceLaneTarget(const OffBallConstants& k, const LaneSearchInput& in) {
  const float s = in.s;
  // X: the deepest of the ball and the two last defenders; never behind the runner.
  float X = in.ball.x;
  for (const Vec2f& d : in.deepest)
    if (s * d.x > s * X) X = d.x;
  if (s * X <= s * in.runnerPred.x) X = in.runnerPred.x + k.laneAhead_m * s;

  // The runner's lane and the window around it.
  int k0 = 0;
  for (int i = 1; i < kLaneCount; ++i)
    if (std::fabs(in.runner.z - LaneZ(i)) < std::fabs(in.runner.z - LaneZ(k0))) k0 = i;
  const int lo = std::max(0, k0 - k.laneWindow), hi = std::min(kLaneCount - 1, k0 + k.laneWindow);

  // S row of the defending team at its own line (ball when the ball is past it).
  float lineX = in.defLineX;
  if (s * in.ball.x > s * lineX) lineX = in.ball.x;  // INF(med): attack-frame reading of the swap
  std::array<int, kLaneCount> S{};
  for (int i = 0; i < kLaneCount; ++i) S[i] = LaneFreedom(k, in.opponents, lineX, LaneZ(i));

  // Pre-pass: lanes an opponent sits across.
  // ponytail: the pre-pass gate flags (5aa480/5aa6b0/5a8df0/danger) are all
  // treated false, so the pass always runs; the marker exemption is dropped.
  std::array<bool, kLaneCount> blocked{};
  for (int i = lo; i <= hi; ++i) {
    const Vec2f C{X, LaneZ(i)};
    const float d = std::sqrt(Dist2(in.runner, C));
    const float a = std::fabs(Fold180(Bearing(in.runner, C) - in.teamDir_deg));
    const float w = a <= kBlockFullAngle_deg ? 1.0f
                    : a < kBlockZeroAngle_deg
                        ? (kBlockZeroAngle_deg - a) / (kBlockZeroAngle_deg - kBlockFullAngle_deg)
                        : 0.0f;
    const float r = (k.blockConeFlat + k.blockConeSlope * w) * k.blockRadius_m;
    const float R = d >= r ? (d - r) * (d - r) : 0.0f;
    for (const Vec2f& o : in.opponents)
      if (Dist2(o, C) < R) blocked[i] = true;
  }

  // The three nearest opponents within 15 m, for the on-lane penalty.
  std::vector<Vec2f> near = in.opponents;
  std::sort(near.begin(), near.end(), [&](Vec2f a, Vec2f b) {
    return Dist2(a, in.runnerPred) < Dist2(b, in.runnerPred);
  });
  if (near.size() > static_cast<size_t>(kLaneOppCount)) near.resize(kLaneOppCount);

  const float speed = std::hypot(in.runnerVel.x, in.runnerVel.z);
  const float heading = std::atan2(in.runnerVel.z, in.runnerVel.x) * (180.0f / 3.14159265f);
  // ponytail: FUN_140664ee0's ~95 leaves; its two head gates and the far-lane
  // leaf FUN_14066d720 are ported (the leaf's holder flags, its allClear term
  // and its possession-time accept are taken as 0).
  const bool boxZone = std::fabs(in.ball.z) < kPenaltyAreaHalfWidth_m &&
                       s * in.ball.x > kPenaltyAreaLineX_m - kFarLaneBoxMargin_m;
  auto commits = [&](Vec2f C) {
    if (std::fabs(Fold180(Bearing(in.ball, C) - in.teamDir_deg)) > kCommitDev_deg ||
        std::fabs(in.ball.z - C.z) > kCommitDz_m)
      return false;
    return boxZone ||
           !((s * in.ball.x + kFarLaneAhead_m < s * in.runner.x ||
              std::fabs(in.runner.z - C.z) > kFarLaneAcross_m) &&
             Dist2(in.ball, C) > kFarLaneBallSq_m2);
  };
  std::optional<Vec2f> best;
  int bestS = 0;
  for (int i = lo; i <= hi; ++i) {
    if (blocked[i]) continue;
    const Vec2f C{X, LaneZ(i)};
    if (speed > kMovingSpeed_mps &&
        std::fabs(Fold180(Bearing(in.runnerPred, C) - heading)) > kMovingHeadingMax_deg)
      continue;
    int score = S[i];
    // ponytail: the S = 100 promotions (5a7840 ... 5a7270) and the region code
    // test (T+0xb824) are not ported; the table value stands.
    if (in.hasReceiver) {
      const float d = std::fabs(C.z - in.receiver.z);
      if (d > kRecvGap_m && std::fabs(C.z - in.ball.z) < std::fabs(in.receiver.z - in.ball.z) &&
          std::fabs(C.z) < kGoalAreaHalfWidth_m)
        score += static_cast<int>(std::min(1.0f, (d - kRecvGap_m) / kRecvRamp_m) * kRecvRampScore) +
                 kRecvBonus;
    }
    // FUN_1405a59d0 (decompiled for this port) needs the ball holder's block
    // int +0xd8 > 0 (holder flag f1, off-ball O5: meaning undecoded, taken as
    // 0), so the alternative accept is false; FUN_140595cb0 returns true for a
    // slot not already in SPACE_RUN_CHANCE, which every candidate is.
    if (score < in.limit) continue;
    // FUN_140592fd0, budget 1 (594470's anyHit not ported).
    if (in.limit < kBudgetMinLimit) continue;
    bool budgetFail = false;
    for (int n : {i - 1, i + 1})
      if (n >= 0 && n < kLaneCount && S[n] < in.limit - kBudgetMargin && S[n] < kBudgetNeighbourMax)
        budgetFail = true;
    if (budgetFail) continue;

    const float b = Bearing(in.runner, C);
    int pen = 0;
    if (InCone(in.runner, b, kConeNarrow_deg, kConeRadius_m, in.opponents))
      pen = kConeNarrowPen;
    else if (InCone(in.runner, b, kConeWide_deg, kConeRadius_m, in.opponents))
      pen = kConeWidePen;
    const float lx = C.x - in.runnerPred.x, lz = C.z - in.runnerPred.z;
    const float len2 = lx * lx + lz * lz;
    for (const Vec2f& o : near) {
      if (Dist2(o, in.runnerPred) > kLaneOppRangeSq_m2) break;
      if (std::fabs(o.z - C.z) > kLaneOppDz_m) continue;
      if (std::fabs(Fold180(Bearing(in.runnerPred, o) - Bearing(in.runnerPred, C))) > kLaneOppDev_deg)
        continue;
      if (len2 <= 0.0f) continue;
      const float t = ((o.x - in.runnerPred.x) * lx + (o.z - in.runnerPred.z) * lz) / len2;
      const float px = in.runnerPred.x + t * lx - o.x, pz = in.runnerPred.z + t * lz - o.z;
      const float perp2 = px * px + pz * pz;
      if (t < 0.0f || perp2 >= kLaneOppPerpSq_m2) continue;
      const float d = std::sqrt(perp2);
      const float w = d <= kLaneOppFull_m ? 1.0f
                      : d < kLaneOppZero_m ? (kLaneOppZero_m - d) / (kLaneOppZero_m - kLaneOppFull_m)
                                           : 0.0f;
      pen = std::min(pen, static_cast<int>(kLaneOppPen * w));
    }
    score += pen;
    if (std::fabs(C.z) > kWideLane_m)
      score -= static_cast<int>(kWidePen * Ramp(s * in.ball.x, kWideBallFrom_m, kPenaltyAreaLineX_m) *
                                Ramp(std::fabs(in.ball.z - C.z), kWideDzFrom_m, kWideDzTo_m));
    // ponytail: the sec/595ab0 test, the teammate-claim gates and the tie-break
    // are not ported (ties keep the first lane found).
    if (best && score <= bestS) continue;
    if (!commits(C)) continue;
    best = C;
    bestS = score;
  }
  // ponytail: the builder's undecoded fallback chain (603d00 ... 604490) is
  // replaced by its one decoded target, FUN_1406067c0: the line, 10 m to the
  // runner's side of the ball, through the same commit gates.
  if (!best) {
    const Vec2f C{in.defLineX,
                  in.ball.z + (in.runner.z >= in.ball.z ? 1.0f : -1.0f) * k.fallbackSide_m};
    if (commits(C)) best = C;
  }
  return best;
}

bool RunStop(const StopInput& in) {
  // Chance (0x140607a60): nothing stops the run inside its minimum time.
  if (in.t_frames < kChanceMinFrames) return false;
  if (in.humanControlled || in.becameCarrier) return true;
  // Common test FUN_140688c00, head.
  if (!in.openPlay || !in.teamInPossession) return true;
  const float s = in.s;
  if (Dist2(in.runner, in.target) < kTargetReached_m * kTargetReached_m &&
      s * in.runner.x > s * in.oppLineX + kLineMarginEarly_m)
    return true;
  if (s * in.runner.x > kGoalAreaLineX_m + kGoalLineMargin_m) return true;
  // Step 6/8: a far runner must be well behind the ball.
  // ponytail: FUN_140596240 ("best wing option") is taken as true (its role
  // count 8c51f0 is undecoded), and no run is pinned.
  const float dx = std::fabs(in.runner.x - in.ball.x);
  const float kk = dx <= kNearDx_m ? kNearK_m
                   : dx < kNearDxFar_m ? kNearK_m * (kNearDxFar_m - dx) / (kNearDxFar_m - kNearDx_m)
                                       : 0.0f;
  const float behind = in.run == kCounter ? kFarBehindCounter_m : kFarBehind_m;
  if (Dist2(in.runner, in.ball) > (kk + kFarBase_m) * (kk + kFarBase_m) &&
      !(s * in.runner.x + behind < s * in.ball.x))
    return true;
  // Step 13 tail.
  if (in.run != kCounter && std::fabs(in.runner.z - in.ball.z) > kWideDz_m) return true;
  // Step 14: past the defending line by 1.5 m (0.7 m after 43 frames).
  const float r = in.t_frames > kLateFrames ? kLineMarginLate_m : kLineMarginEarly_m;
  if (s * in.runner.x > s * in.oppLineX + r) return true;
  // ponytail: steps 2-5, 9-12, 15-19 (about 60 undecoded or world-heavy
  // leaves) are not ported; the fallthrough keeps the run instead of PES's
  // final STOP, which only fires after every keep leaf fails.
  return false;
}

float SupporterCost(float angleDiff_deg, float distance_m) {
  return kSupporterAngleWeight * std::fabs(angleDiff_deg) + distance_m +
         (distance_m < kSupporterClose_m ? kSupporterClosePen : 0.0f);
}

float SupportDistance_m(const SupportDistanceInput& in) {
  if (!in.inPlay) return kSupportDead_m;
  const float s = std::clamp((in.supportRange - 1) / kSupportRangeSpan, 0.0f, 1.0f);
  const float deep = Ramp(in.ballX, in.oppLineX - kSupportLineMargin_m,
                          in.ownLineX + kSupportLineMargin_m);
  const float a = std::fabs(Fold180(in.bearingFromAttack_deg));
  const float fwd = 1.0f - Ramp(a, kSupportFwdFull_deg, kSupportFwdZero_deg);
  const float back = Ramp(180.0f - a, kSupportFwdFull_deg, kSupportFwdZero_deg);
  float d = kSupportBase_m + kSupportRangeGain_m * s + kSupportDeep_m * deep +
            (in.buildUpLong ? kSupportLong_m : 0.0f) * fwd * deep +
            Ramp(in.ballX, kPenaltyAreaLineX_m, -kFieldHalfWidthHalf * kFieldHalfWidth_m) * back +
            kSupportWide_m *
                Ramp(std::fabs(in.ballZ), kPenaltyAreaHalfWidth_m - kSupportWideBand_m,
                     kPenaltyAreaHalfWidth_m + kSupportWideBand_m) *
                Ramp(in.ballX, kPenaltyAreaLineX_m - kSupportBoxBand_m, kPenaltyAreaLineX_m);
  // ponytail: the holder-state min 5 and the f0/f1 (+5 floor 9 / -5) flags are
  // undecoded (O5); the plain floor applies.
  return std::max(d, kSupportFloor_m);
}

}  // namespace pes::offball

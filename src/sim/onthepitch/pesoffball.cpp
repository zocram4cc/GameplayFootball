#include "onthepitch/pesoffball.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pes/uniform.hpp"

namespace pes::offball {
namespace {

// --- literals the reports give without an .rdata VA (provenance per line) ---
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

// --- the stop test (FUN_140688c00 and its leaves; run-targets 3, -s1..-s3) ---
//
// Every angle below is a PES bearing (atan2(dx, dz), degrees, [0, 360)), so
// the reports' literals apply unchanged. Inputs PES keeps that GF has no
// counterpart for take one fixed value, named here once:
//   - AI class a580a0 (`cls`) = 0, matching none of the class-gated branches;
//   - run sub-kind T+0x28c (`sub`) = 0 and "pinned" = false (the target
//     builder's special lanes are not ported);
//   - mark count a58320 = 0; player flag bit 21, T+0x2a3, T+0xb8a2 = 0;
//   - team concept slot T+0xb748 = 0x30 (no concept on a runner);
//   - no advanced instruction (a63950) is set (teaminstructions.hpp).
namespace {

constexpr float kRad = 57.2957795f;
// pred(p, n): n = int(fps·0.3+0.5) = 16 frames (0x14059bbe4).
constexpr float kRunPredictFrames = 16.0f;
constexpr int kNoId = -1;
// Field marks F1, F2, F8 (pes-re-fieldinfo.md).
constexpr float kF1 = kFieldHalfWidth_m;
constexpr float kF2 = 3.66f;
constexpr float kF8 = 41.56f;
constexpr float kF0 = kFieldHalfLength_m, kF4 = kGoalAreaLineX_m, kF5 = kGoalAreaHalfWidth_m,
                kF6 = kPenaltyAreaLineX_m, kF7 = kPenaltyAreaHalfWidth_m;
// fps-derived frame counts int(fps·x + 0.5).
constexpr float Frames(float sec) { return static_cast<float>(static_cast<int>(kPesFps * sec + 0.5f)); }
// Speeds the reports give as 100/(fps·T) m/frame (T seconds per 100 m) or as
// FUN_1404c8dc0(k) (INF km/h); here in m/s.
constexpr float Per100(float sec) { return 100.0f / sec; }
constexpr float Kmh(float k) { return k / 3.6f; }

float Wrap360(float a) {
  a = std::fmod(a, 360.0f);
  return a < 0.0f ? a + 360.0f : a;
}
float PBrg(Vec2f a, Vec2f b) {
  if (a.x == b.x && a.z == b.z) return 0.0f;
  return Wrap360(std::atan2(b.x - a.x, b.z - a.z) * kRad);
}
float PVec(Vec2f v) { return (v.x == 0.0f && v.z == 0.0f) ? 0.0f : Wrap360(std::atan2(v.x, v.z) * kRad); }
float Ad(float a, float b) { return std::fabs(Fold180(a - b)); }
float Len(Vec2f v) { return std::sqrt(v.x * v.x + v.z * v.z); }
float Sgn(float v) { return v < 0.0f ? -1.0f : 1.0f; }
int TeamOf(int id) { return id >= kSlots ? 1 : 0; }
bool Valid(int id) { return id >= 0 && id < 2 * kSlots; }
// a4f210: the attack bearing, s == -1 -> 270, else 90.
float TDir(const RunWorld& w, int t) { return w.s[t] < 0.0f ? 270.0f : 90.0f; }
// a4c720(a, b, s, k): b is more than k ahead of a in the attack direction.
bool A720(float a, float b, float s, float k) { return (s * k + a) * s < s * b; }
// a4c9a0: past the opponents' line by k.
bool Beyond(const RunWorld& w, int id, float k) {
  const int t = TeamOf(id);
  return A720(w.lineX[t ^ 1], w.p[id].pos.x, w.s[t], k);
}
// 599d90: that line, advanced to the ball when there is no carrier.
float Line599(const RunWorld& w, int t) {
  float x = w.lineX[t ^ 1];
  if (w.carrier == kNoId && w.s[t] * w.ball.x > w.s[t] * x) x = w.ball.x;
  return x;
}
bool Beyond2(const RunWorld& w, int id, float k) {
  const int t = TeamOf(id);
  return A720(Line599(w, t), w.p[id].pos.x, w.s[t], k);
}
// 5a4830(id, 0, 1) -> 5a4790: the slot is in a run state (0x27 only with c,
// 0x2f only for AI class 6/7).
bool IsRun(int st) {
  switch (st) {
    case 0x1d: case 0x1e: case 0x1f: case 0x20: case 0x21: case 0x24: case 0x25: case 0x2c:
      return true;
    default:
      return false;
  }
}
// a4e720 -> a4e3f0: opponents of `id` inside the cone (|dev| <= width/2,
// minDist < d < radius), positions `frames` ahead.
int ConeOpp(const RunWorld& w, int id, float center, float width, float radius,
            float frames = 0.0f, float minDist = 0.0f) {
  const PlayerFacts& me = w.p[id];
  const Vec2f m{me.pos.x + frames / kPesFps * me.vel.x, me.pos.z + frames / kPesFps * me.vel.z};
  const int o = (TeamOf(id) ^ 1) * kSlots;
  int n = 0;
  for (int i = o; i < o + kSlots; ++i) {
    const PlayerFacts& q = w.p[i];
    if (!q.active) continue;
    const Vec2f qp{q.pos.x + frames / kPesFps * q.vel.x, q.pos.z + frames / kPesFps * q.vel.z};
    const float d2 = Dist2(qp, m);
    if (minDist * minDist < d2 && d2 < radius * radius && Ad(center, PBrg(m, qp)) <= width * 0.5f) ++n;
  }
  return n;
}
// a4fea0 -> a4fbc0: players of team `sel` (2 = both) in the cone around `pos`
// (strict half-width).
int ConeAt(const RunWorld& w, int sel, Vec2f pos, float center, float width, float radius,
           float minDist) {
  int n = 0;
  for (int i = 0; i < 2 * kSlots; ++i) {
    if (!w.p[i].active || (sel != 2 && TeamOf(i) != sel)) continue;
    const float d2 = Dist2(w.p[i].pos, pos);
    if (minDist * minDist < d2 && d2 < radius * radius &&
        Ad(center, PBrg(pos, w.p[i].pos)) < width * 0.5f)
      ++n;
  }
  return n;
}
// a4e250: an opponent within r.
bool OppNear(const RunWorld& w, int id, float r) {
  const int o = (TeamOf(id) ^ 1) * kSlots;
  for (int i = o; i < o + kSlots; ++i)
    if (Dist2(w.p[i].pos, w.p[id].pos) < r * r) return true;
  return false;
}
// a53da0: player j within (minD, maxD) of pt and within ang/2 of dir from it.
bool A53da0(const RunWorld& w, int j, Vec2f pt, float dir, float ang, float maxD, float minD) {
  const float d2 = Dist2(w.p[j].pos, pt);
  return d2 < maxD * maxD && minD * minD < d2 && Ad(dir, PBrg(pt, w.p[j].pos)) < ang * 0.5f;
}
// Opponents of id sorted by distance (the MA+0x22e4c lists).
std::vector<int> OppByDist(const RunWorld& w, int id) {
  std::vector<int> v;
  const int o = (TeamOf(id) ^ 1) * kSlots;
  for (int i = o; i < o + kSlots; ++i)
    if (w.p[i].active) v.push_back(i);
  std::sort(v.begin(), v.end(), [&](int a, int b) {
    return Dist2(w.p[a].pos, w.p[id].pos) < Dist2(w.p[b].pos, w.p[id].pos);
  });
  return v;
}
// 69cd40: a defender D's perpendicular gap to the lane A->B if he reaches it
// before the ball (5.65·t + 0.6 m), else -1.
float LaneIntercept(Vec2f A, Vec2f B, Vec2f C, Vec2f D, float speed, bool flag) {
  Vec2f u{B.x - A.x, B.z - A.z};
  const float l = Len(u);
  if (l > 0.0f) u = {u.x / l, u.z / l};
  auto along = [&](Vec2f p) { return (p.x - A.x) * u.x + (p.z - A.z) * u.z; };
  auto lat = [&](Vec2f p) { return -u.z * (p.x - A.x) + u.x * (p.z - A.z); };
  const float aD = along(D), lD = lat(D);
  if (aD < 0.0f && std::fabs(aD) > 0.5f) return -1.0f;
  if (flag) {
    if (Dist2(A, B) < Dist2(D, A) && Dist2(B, C) < Dist2(B, D)) return -1.0f;
    const float lC = lat(C);
    const float da = Fold180(PBrg(A, B) - PBrg(A, C));
    if ((std::fabs(da) < 20.0f || lC < 0.7f) && along(C) < aD) {
      if (std::fabs(lD - lC) < 1.5f) return -1.0f;
      if (std::fabs(lC) < std::fabs(lD)) return -1.0f;
    }
  }
  if (std::fabs(lD) <= 0.5f) return std::fabs(lD);
  const float thr = 100.0f / 17.7f * (aD / speed) + 0.6f;
  return std::fabs(lD) > thr ? -1.0f : std::fabs(lD);
}

struct Ctx {
  const RunWorld& w;
  int id, t, opp, c, q;
  float s, dir, tt;
  Vec2f P, B;
  const PlayerFacts& me;
  Ctx(const RunWorld& w_, int id_)
      : w(w_), id(id_), t(TeamOf(id_)), opp(TeamOf(id_) ^ 1), c(w_.carrier),
        q(w_.receiver[TeamOf(id_)]), s(w_.s[TeamOf(id_)]), dir(TDir(w_, TeamOf(id_))),
        tt(w_.p[id_].t), P(w_.p[id_].pos), B(w_.ball), me(w_.p[id_]) {}
  float D2() const { return Dist2(P, B); }
};

bool EvKick(const BallEvent& e) {
  return e.valid && (e.kind == 1 || e.kind == 2 || e.kind == 3 || e.kind == 0x10);
}
// Head H of 692810 / 692a00 / 692580: loose ball, kicked during this run and
// at most a second ago.
bool LooseKick(const Ctx& x) {
  return x.c == kNoId && EvKick(x.w.ev) && x.w.ev.age <= x.tt && x.w.ev.age <= kPesFps;
}

// s1 #1, step 4.
bool L68f600(const Ctx& x) {
  if (x.s * x.P.x > kF4 + 1.5f) return true;
  if (x.s * x.P.x < kF4 - 3.0f) return false;
  if (!A720(x.B.x, x.P.x, x.s, 0.2f)) return false;
  const float a = Ad(x.dir, PBrg(x.B, x.me.aim));
  if (a < 22.0f || ConeOpp(x.w, x.id, x.dir, 50.0f, 20.0f)) return true;
  if (A720(x.B.x, x.P.x, x.s, -0.1f)) return true;
  const float wd = Ad(x.dir, PBrg(x.P, x.B));
  std::vector<int> opps;
  for (int i = x.opp * kSlots; i < x.opp * kSlots + kSlots; ++i)
    if (x.w.p[i].active) opps.push_back(i);
  std::sort(opps.begin(), opps.end(),
            [&](int a, int b) { return x.s * x.w.p[a].pos.x > x.s * x.w.p[b].pos.x; });
  for (int o : opps) {
    const Vec2f op = x.w.p[o].pos;
    if (x.s * op.x < x.s * x.P.x) break;
    if (std::fabs(op.z - x.B.z) <= std::fabs(x.P.z - x.B.z) &&
        A53da0(x.w, o, x.P, x.dir, 2.0f * wd, 30.0f, 0.0f))
      return true;
  }
  return false;
}
// s1 #3 (step 8, run != Counter).
bool L692810(const Ctx& x) {
  if (!LooseKick(x)) return false;
  if (x.s * x.B.x <= x.s * x.P.x) return Dist2(x.w.ev.pos, x.B) >= 400.0f;
  return false;
}
// s1 #4 (step 9).
bool L692a00(const Ctx& x) {
  if (!LooseKick(x)) return false;
  if (!A720(x.B.x, x.P.x, x.s, 8.0f)) return false;
  const Vec2f e = x.w.ev.pos;
  if (x.s * x.B.x > x.s * e.x) return false;
  if (std::fabs(e.x - x.B.x) > 7.0f && Beyond(x.w, x.id, -4.0f)) return true;
  return std::fabs(e.x - x.B.x) >= 12.0f;
}
// s1 #5 (step 9, run != Counter).
bool L692580(const Ctx& x) {
  if (!LooseKick(x)) return false;
  if (Dist2(x.w.ev.pos, x.B) < 100.0f) return false;
  if (Ad(x.dir, PVec(x.me.vel)) > 90.0f) return true;
  return Len(x.me.vel) < Kmh(10.0f);
}
// s1 #6: exemption (the caller stops only when this is false).
bool L68a730(const Ctx& x, int run) {
  if (run < kSecondLine || run > kCounter || !Beyond(x.w, x.id, -7.0f)) return false;
  const float dz = x.P.z - x.B.z, dx = x.P.x - x.B.x;
  if (std::fabs(x.P.z) >= 15.0f) {
    if (dx * dx + dz * dz > 484.0f) return false;
  } else {
    if (std::fabs(dz) > 25.0f) return false;
    if (std::fabs(dx) > 12.0f && !A720(x.P.x, x.B.x, x.s, 0.0f)) return false;
  }
  if (OppNear(x.w, x.id, 3.2f)) return false;
  return x.s * x.P.x <= kF6 + 2.0f;
}
// s1 #7: exemption.
bool L68a8c0(const Ctx& x, bool intercept) {
  if (intercept) return false;
  if (std::fabs(x.P.z) > kF5 + 2.0f || !Beyond(x.w, x.id, -2.0f)) return false;
  const Vec2f A = x.me.aim;
  if (Dist2(A, x.B) > 729.0f) return false;
  if (!Valid(x.q) || Dist2(x.w.p[x.q].pos, x.B) > 12.25f) return false;
  if (ConeOpp(x.w, x.q, PBrg(x.B, A), 80.0f, 6.5f)) return false;
  const std::vector<int> near = OppByDist(x.w, x.id);
  for (size_t i = 0; i < near.size() && i < 4; ++i) {
    const Vec2f o = x.w.p[near[i]].pos;
    if (Dist2(x.P, o) > 10.24f) return true;
    if (Ad(x.dir, PBrg(x.P, o)) <= 82.0f && Dist2(o, A) <= Dist2(x.P, A)) return false;
  }
  return true;
}
// s1 #9: blocks the step-12 stop.
bool L694d10(const Ctx& x) {
  if (!Valid(x.c)) return false;
  const PlayerFacts& c = x.w.p[x.c];
  if (std::fabs(x.P.x - c.pos.x) > 8.0f) return false;
  const float a = Ad(x.dir, PBrg(c.pos, x.P));
  if (a < 60.0f || a > 120.0f) return false;
  const float side = x.P.z > c.pos.z ? 0.0f : 180.0f;
  if (ConeOpp(x.w, x.c, side, 70.0f, 7.0f)) return false;
  return std::fabs(x.P.z - c.pos.z) < 20.0f || !Beyond(x.w, x.id, -6.0f);
}
// s1 #10 (step 13).
bool L68dba0(const Ctx& x) {
  if (!Beyond(x.w, x.id, -3.0f) || !Valid(x.q)) return false;
  if (x.D2() < 100.0f || !A720(x.B.x, x.P.x, x.s, 5.0f)) return false;
  const PlayerFacts& q = x.w.p[x.q];
  const float h = PVec(q.vel), f = q.facing, b = PBrg(x.B, x.P);
  const float m = std::min(Ad(h, b), Ad(f, b));
  if (m < 135.0f && (Ad(h, x.dir) < 90.0f || Ad(f, x.dir) < 90.0f) && Ad(b, x.dir) < 36.0f)
    return false;
  return m >= 120.0f;
}
// s1 #11 (step 13).
bool L68de30(const Ctx& x) {
  if (!Valid(x.c)) return false;
  const PlayerFacts& c = x.w.p[x.c];
  if (Dist2(x.P, c.pos) < 144.0f) return false;
  const Vec2f A = x.me.aim;
  const float b = PBrg(x.P, A);
  const float fwd = x.P.z <= A.z ? 0.0f : 180.0f;
  if (Ad(b, fwd) > 65.0f) return false;
  if (!A720(c.pos.x, x.P.x, x.s, 3.0f) || !Beyond(x.w, x.id, -4.0f)) return false;
  const float back = Wrap360(fwd + 180.0f);
  if (Ad(PBrg(c.pos, x.P), back) < 85.0f) return false;
  if (Ad(PVec(c.vel), back) > 65.0f) return false;
  return Ad(c.facing, PBrg(c.pos, A)) >= 95.0f;
}
// s1 #12 (step 13).
bool L68e300(const Ctx& x) {
  if (!Valid(x.c) || !Beyond(x.w, x.id, -6.0f) || x.tt < Frames(0.1f)) return false;
  const PlayerFacts& c = x.w.p[x.c];
  const float side = x.P.z <= c.pos.z ? 0.0f : 180.0f;
  if (Ad(side, PVec(c.vel)) > 25.0f || Ad(side, c.facing) > 25.0f) return false;
  const Vec2f A2 = x.me.aim;  // ponytail: 599ff0's point taken as the run target
  if (Dist2(c.pos, A2) < 144.0f || Dist2(c.pos, x.P) < 25.0f) return false;
  return Ad(c.facing, PBrg(c.pos, A2)) >= 105.0f;
}
// s3 #19, step 6: false = stop. 8c51f0 is the role-class map.
bool L596240(const Ctx& x) {
  if (x.me.roleClass >= 3) return true;
  const bool me = IsRun(x.me.state);
  for (int j = x.t * kSlots; j < x.t * kSlots + kSlots; ++j) {
    if (j == x.id || !x.w.p[j].active) continue;
    const int nj = x.w.p[j].roleClass;
    if (nj < 3 && nj != 0 && IsRun(x.w.p[j].state) &&
        (!me || Dist2(x.w.p[j].pos, x.B) <= x.D2()))
      return false;
  }
  return true;
}
// s1 #14 partial: 68efc0. ponytail: a4f720 and 695110 (grid-flag reader
// a72180) are not ported; the branch that needs them keeps.
bool L68efc0(const Ctx& x) {
  const float a = Ad(x.dir, PBrg(x.B, x.P));
  if (a > 22.0f || x.tt < 2.0f * kPesFps || !Beyond2(x.w, x.id, -3.0f)) return false;
  if (a > 4.0f) return false;
  if (std::fabs(x.P.x - x.B.x) > 8.0f && ConeAt(x.w, x.opp, x.B, x.dir, 90.0f, 3.0f, 0.0f))
    return true;
  const std::vector<int> near = OppByDist(x.w, x.id);
  if (std::fabs(x.P.x - x.B.x) >= 15.0f && !near.empty()) {
    const float d = Dist2(x.w.p[near[0]].pos, x.P);
    return (std::fabs(x.P.x - x.B.x) > 15.0f && d < 12.25f) || d <= 25.0f;
  }
  return false;
}
// s1 #16.
bool L68e900(const Ctx& x) {
  const float bb = PBrg(x.B, x.P);
  if (x.tt < kPesFps || Ad(bb, x.dir) > 22.0f || !Beyond(x.w, x.id, -0.8f) ||
      std::fabs(x.P.x - x.B.x) < 8.5f)
    return false;
  const std::vector<int> near = OppByDist(x.w, x.id);
  if (near.empty() || Dist2(x.w.p[near[0]].pos, x.P) > 16.0f) return false;
  if (!Valid(x.q) || x.w.p[x.q].humanControlled) return true;
  const PlayerFacts& q = x.w.p[x.q];
  if (Ad(q.facing, bb) < 40.0f) return false;
  return Ad(PBrg(q.pos, q.aim), bb) >= 40.0f;
}
// s1 #17.
bool L68ec50(const Ctx& x) {
  if (!Beyond(x.w, x.id, -3.0f) || !A720(x.B.x, x.P.x, x.s, 14.0f)) return false;
  if (Ad(x.dir, PBrg(x.B, x.P)) > 22.0f || x.D2() < 144.0f) return false;
  if (Ad(x.dir, PVec(x.me.vel)) > 22.0f || Ad(x.dir, PBrg(x.P, x.me.aim)) > 22.0f) return false;
  const int m = x.me.marker;
  if (!Valid(m)) return false;
  const PlayerFacts& o = x.w.p[m];
  if (!(x.s * x.P.x <= x.s * o.pos.x) || std::fabs(x.P.x - o.pos.x) < 0.5f ||
      std::fabs(x.P.z - o.pos.z) > 2.0f)
    return false;
  if (Ad(x.dir, PVec(o.vel)) > 22.0f) return false;
  const float vr = Len(x.me.vel), vo = Len(o.vel);
  return vr <= vo || std::fabs(vr - vo) <= Kmh(10.0f);
}
// s1 #18: true -> skip the stop branch.
bool L68d920(const Ctx& x) {
  if (!Valid(x.q)) return false;
  const PlayerFacts& q = x.w.p[x.q];
  const float f = q.facing;
  if (Ad(f, PBrg(q.pos, x.P)) < 90.0f) return false;
  if (Ad(x.dir, f) < 100.0f || !ConeOpp(x.w, x.q, x.dir, 100.0f, 5.0f)) return false;
  for (int m = x.t * kSlots; m < x.t * kSlots + kSlots; ++m) {
    if (m == x.id || m == x.q || !x.w.p[m].active) continue;
    if (Ad(f, PBrg(q.pos, x.w.p[m].pos)) <= 45.0f && Dist2(q.pos, x.w.p[m].pos) <= 100.0f)
      return true;
  }
  return false;
}
// s1 #20.
bool L68b070(const Ctx& x) {
  if (x.s * x.B.x < kF4 - 3.0f) return false;
  if (std::fabs(x.P.x - x.B.x) > 6.0f || std::fabs(x.P.z - x.B.z) > 7.0f) return false;
  if (std::fabs(x.P.z) < kF5) return false;
  return x.s * x.P.x >= kF8 + 1.5f;
}
// s1 #21. a57800's point taken as the opponents' goal centre (INF goal point).
bool L692c50(const Ctx& x) {
  if (Valid(x.c) || !EvKick(x.w.ev)) return false;
  if (Dist2(x.P, x.me.aim) < 16.0f) return false;
  const float a1 = Fold180(x.dir - PBrg(x.P, x.me.aim));
  if (std::fabs(a1) < 75.0f) return false;
  const Vec2f g{x.s * kF0, 0.0f};
  const float a2 = Fold180(x.dir - PBrg(g, x.B));
  if (std::fabs(a2) < 45.0f || std::fabs(a2) > 135.0f) return false;
  if (Sgn(a1) == Sgn(a2)) return false;
  if (std::fabs(x.B.z - g.z) >= 6.5f && Sgn(g.z - x.P.z) == Sgn(x.B.z - x.P.z)) return false;
  return true;
}
// s1 #22. a72e70 bit 12 taken as "inside the penalty area".
bool InBox(float s, Vec2f p) { return s * p.x >= kF6 && std::fabs(p.z) <= kF7; }
bool L643f50(const RunWorld& w, int team);
bool L68b920(const Ctx& x) {
  if (!InBox(x.s, x.B) || !InBox(x.s, x.P)) return false;
  if (x.D2() > 144.0f || x.s * x.B.x < kF6) return false;
  if (A720(x.B.x, x.P.x, x.s, 3.0f)) return false;
  return L643f50(x.w, x.t) || x.s * x.B.x >= kF8;
}
// s2 #1. marks = 0, cls = 0.
bool L691ef0(const Ctx& x) {
  if (!Valid(x.c)) return false;
  if (Dist2(x.P, x.w.p[x.c].pos) > 121.0f) return false;
  const Vec2f G{x.s * kF0, 0.0f};
  if (Dist2(G, x.B) >= 1024.0f) return false;
  const float gB = PBrg(x.B, G);
  if (Ad(x.dir, gB) > 47.0f || ConeOpp(x.w, x.c, gB, 50.0f, 4.5f)) return false;
  const std::vector<int> near = OppByDist(x.w, x.id);
  if (x.D2() > 169.0f && !near.empty() && Dist2(x.w.p[near[0]].pos, x.P) > 25.0f) return false;
  const Vec2f Q{x.P.x + x.me.vel.x, x.P.z + x.me.vel.z};  // a75740, 54 frames ahead
  const float d = Fold180(x.dir - PBrg(x.P, Q));
  if (std::fabs(d) > 35.0f && Dist2(x.B, Q) > 64.0f) {
    const PlayerFacts& c = x.w.p[x.c];
    const float bc = PBrg(c.pos, c.aim);
    if (Ad(bc, x.dir) < 15.0f) return false;
    if (Sgn(Fold180(x.dir - bc)) != Sgn(d)) return false;
  }
  const float bP = PBrg(x.B, x.P);
  const float a1 = PBrg(x.B, {G.x, kF2 + 0.5f}), a2 = PBrg(x.B, {G.x, -(kF2 + 0.5f)});
  auto within = [&](float a, float b) { return bP >= std::min(a, b) && bP <= std::max(a, b); };
  return within(a1, gB) || within(a2, gB);
}
// s2 #2. Z = the slot point.
bool L692fe0(const Ctx& x) {
  if (std::fabs(x.B.z - x.P.z) < 13.0f || std::fabs(x.P.z) < kF7 - 5.0f) return false;
  if (x.s * x.P.x < kF6 - 10.0f) return false;
  if (!Valid(x.q)) return true;
  const PlayerFacts& q = x.w.p[x.q];
  const float b = PBrg(q.pos, x.P);
  if (Ad(q.facing, b) < 55.0f || Ad(q.facing, x.dir) > 135.0f) return false;
  const Vec2f Z = x.me.slotPoint;
  if (std::fabs(Z.z) > kF5 || std::fabs(Z.z - x.B.z) > 12.0f) return false;
  if (!A720(x.B.x, Z.x, x.s, 3.0f)) return false;
  const std::vector<int> near = OppByDist(x.w, x.id);
  if (!near.empty() && Dist2(x.w.p[near[0]].pos, x.P) > 196.0f) return false;
  return ConeOpp(x.w, x.q, b, 80.0f, std::sqrt(Dist2(q.pos, x.P)) + 5.0f) > 0;
}
// s2 #3.
bool L690b90(const Ctx& x) {
  if (std::fabs(x.B.z) > kF5 || x.s * x.B.x < kF6 || std::fabs(x.P.z) < kF7) return false;
  const float r = Ramp(std::fabs(x.P.x), kF6, kF8);
  return std::fabs(x.B.z - x.P.z) >= 17.0f - 7.0f * r;
}
// s2 #4.
bool L690630(const Ctx& x) {
  if (x.s * x.B.x < kF8 || x.s * x.P.x < kF6) return false;
  if (Sgn(x.P.z) == Sgn(x.B.z) && std::fabs(x.B.z) < std::fabs(x.P.z) && std::fabs(x.P.z) > kF5 &&
      std::fabs(x.P.z - x.B.z) < 14.0f)
    if (A720(x.s * kF4, x.P.x, x.s, -3.0f) || A720(x.s * kF4, x.B.x, x.s, -3.0f)) return true;
  if (x.s * x.P.x < kF8 && !A720(x.B.x, x.P.x, x.s, -8.0f)) {
    const float tz = x.me.aim.z;
    if (std::fabs(tz) < kF5 && std::fabs(tz - x.B.z) < 22.0f) return false;
  }
  const float z = x.P.z;
  return (std::fabs(z) >= kF7 || std::fabs(z - x.B.z) >= 20.0f) && std::fabs(x.B.z - z) >= 10.0f &&
         Sgn(z) == Sgn(x.B.z);
}
// s2 #5. R = the run target.
bool L690400(const Ctx& x) {
  if (x.s * x.B.x < kF8 || std::fabs(x.B.z) < kF5 - 1.0f) return false;
  if (Sgn(x.P.z) != Sgn(x.B.z) || std::fabs(x.P.z) > kF7) return false;
  const Vec2f R = x.me.aim;
  if (Sgn(x.B.z) == Sgn(R.z) && std::fabs(x.B.z) <= std::fabs(R.z) && std::fabs(x.P.z) <= std::fabs(R.z))
    return std::fabs(R.z) >= kF5 + 3.0f;
  return false;
}
// s2 #6.
bool L690cf0(const Ctx& x) {
  if (x.tt < Frames(1.8f) || x.s * x.P.x < kF6 - 2.0f) return false;
  if (Len(x.me.vel) > 0.9f * Per100(22.5f)) return false;
  if (x.D2() > 400.0f) return false;
  for (int o : OppByDist(x.w, x.id))
    if (std::fabs(x.w.p[o].pos.z - x.B.z) <= std::fabs(x.P.z - x.B.z) &&
        A53da0(x.w, o, x.P, x.dir, 180.0f, 3.5f, 0.0f))
      return true;
  return false;
}
// s2 #7.
bool L694650(const Ctx& x) {
  if (x.s * x.P.x < kF6 || std::fabs(x.P.z) > kF2 + 2.0f) return false;
  if (Ad(x.dir, PVec(x.me.vel)) > 32.0f) return false;
  const float vMin = 0.8f * Per100(12.9f);
  if (Len(x.me.vel) < vMin || !Valid(x.q)) return false;
  if (Dist2(x.w.p[x.q].pos, x.B) > 9.0f) return false;
  if (std::fabs(x.P.z - x.B.z) < 5.0f) return false;
  if (x.s * x.B.x < kF8 - 2.5f || x.D2() > 400.0f) return false;
  if (A720(x.B.x, x.P.x, x.s, 1.0f)) return false;
  const int m = x.me.marker;
  if (!Valid(m)) return false;
  const PlayerFacts& o = x.w.p[m];
  const float a = Ad(x.dir, PBrg(x.P, o.pos));
  if (a > 70.0f) return false;
  const float dm = Dist2(x.P, o.pos);
  if ((Sgn(o.pos.z - x.P.z) == Sgn(x.B.z - x.P.z) || a >= 80.0f || dm <= 9.0f) &&
      (a >= 30.0f || dm <= 7.29f))
    if (Ad(x.dir, PVec(o.vel)) > 32.0f || Len(o.vel) < vMin) return false;
  const float bb = PBrg(x.B, x.P), r = std::sqrt(x.D2());
  if (ConeAt(x.w, x.opp, x.B, bb, 40.0f, r, 3.0f) && !A53da0(x.w, m, x.B, bb, 40.0f, r, 3.0f))
    return false;
  const float d = x.P.z < x.B.z ? 0.0f : 180.0f;
  return !(ConeAt(x.w, x.opp, x.P, d, 180.0f, 3.0f, 0.0f) && !A53da0(x.w, m, x.P, d, 180.0f, 3.0f, 0.0f));
}
// s2 #8 / #9: 695110 (grid-flag reader) taken as false.
bool L693a20(const Ctx& x) {
  if (x.tt < Frames(1.2f) || !Beyond2(x.w, x.id, -3.5f)) return false;
  const Vec2f R = x.me.aim;
  if (std::fabs(x.P.z) < kF7 + 3.0f) {
    if (std::fabs(x.P.z - x.B.z) > 4.0f || std::fabs(x.P.z) < kF7 - 1.0f) return false;
    if (std::fabs(x.P.z - R.z) > 4.0f && Sgn(R.z - x.P.z) == Sgn(-x.P.z)) return false;
  }
  if (std::fabs(x.P.x - x.B.x) < 8.0f || !A720(x.B.x, x.P.x, x.s, 0.0f)) return false;
  if (Ad(x.dir, PBrg(x.B, x.P)) > 36.0f || !OppNear(x.w, x.id, 8.0f)) return false;
  const int m = x.me.marker;
  if (!Valid(m) || !A53da0(x.w, m, x.P, x.dir, 200.0f, 8.0f, 0.0f)) return false;
  const float dz = x.P.z - x.B.z, dm = x.w.p[m].pos.z - x.B.z;
  if (Sgn(dz) == Sgn(dm) && std::fabs(dm) > std::fabs(dz)) return false;
  const float n = Frames(0.5f) / kPesFps;
  const Vec2f Pp{x.P.x + n * x.me.vel.x, x.P.z + n * x.me.vel.z};
  return Ad(x.dir, PBrg(Pp, R)) <= 45.0f || std::fabs(R.z - x.w.p[m].pos.z) <= 5.0f;
}
bool L693730(const Ctx& x, bool flag) {
  if (!flag || x.tt < Frames(2.2f) || !Beyond2(x.w, x.id, -2.5f)) return false;
  if (x.D2() < 289.0f || std::fabs(x.P.z) < kF7 - 2.0f) return false;
  if (std::fabs(x.P.x - x.B.x) < 10.0f || !A720(x.B.x, x.P.x, x.s, 0.0f)) return false;
  const int m = x.me.marker;
  if (!Valid(m) || !A53da0(x.w, m, x.P, x.dir, 200.0f, 8.0f, 0.0f)) return false;
  const float dz = x.P.z - x.B.z, dm = x.w.p[m].pos.z - x.B.z;
  return !(Sgn(dz) == Sgn(dm) && std::fabs(dm) > std::fabs(dz));
}
// s2 #12.
bool L68e660(const Ctx& x, bool flag) {
  if (!flag || !Valid(x.c)) return false;
  const PlayerFacts& c = x.w.p[x.c];
  if (std::fabs(x.P.z - c.pos.z) < 17.0f || Dist2(c.pos, c.aim) < 4.0f) return false;
  const float bP = PBrg(c.pos, x.P);
  if (Ad(c.facing, bP) < 145.0f) return false;
  const float bt = PBrg(c.pos, c.aim);
  if (Ad(bt, bP) < 125.0f || Len(c.vel) < Per100(32.7f) || Ad(x.dir, bt) < 100.0f) return false;
  return std::fabs(x.P.x - x.B.x) >= 9.0f;
}
// s2 #13.
bool L68b440(const Ctx& x, bool flag) {
  if (!flag || x.tt < Frames(3.0f)) return false;
  if (Ad(x.dir, PBrg(x.B, x.P)) > 45.0f || Ad(x.dir, PVec(x.me.vel)) < 95.0f) return false;
  if (Ad(x.dir, PBrg(x.P, x.me.aim)) < 95.0f) return false;
  if (!Beyond(x.w, x.id, 0.2f) || x.D2() < 400.0f) return false;
  if (x.D2() <= 1369.0f && Valid(x.q))
    return Ad(x.w.p[x.q].facing, PBrg(x.w.p[x.q].pos, x.P)) >= 65.0f;
  return true;
}
// s2 #14: sqrt(D2) >= 1444 (raw) never holds on a pitch; kept as written.
bool L68c050(const Ctx& x) { return x.tt >= Frames(2.5f) && std::sqrt(x.D2()) >= 1444.0f; }
// s2 #16.
bool L692ea0(const Ctx& x) {
  const float bx = x.s * x.B.x;
  if (bx < kF6 - 5.0f) return false;
  const float r = Ramp(bx, kF6 - 5.0f, kF6 + 2.0f);
  if (std::fabs(x.me.aim.z) < kF7 - (3.0f * r + 3.0f)) return false;
  return std::fabs(x.B.z - x.P.z) >= 18.0f;
}
// s2 #18 / #19: SecondLine only.
bool L691a80(const Ctx& x) {
  if (x.tt < Frames(1.0f) || x.me.state != kSecondLine) return false;
  if (A720(x.B.x, x.P.x, x.s, 8.0f) && Ad(PBrg(x.P, x.me.aim), PBrg(x.B, x.P)) < 45.0f) return true;
  if (!A720(x.B.x, x.P.x, x.s, 13.0f) || Beyond(x.w, x.id, -5.0f)) return false;
  return ConeOpp(x.w, x.id, x.dir, 70.0f, 20.0f) || ConeOpp(x.w, x.id, x.dir, 170.0f, 2.5f);
}
// s2 #21.
bool L68d0b0(const Ctx& x, bool flag) {
  if (!L643f50(x.w, x.t)) return false;
  if (x.s * x.P.x < kF6 - 1.0f || std::fabs(x.P.z) < kF5 + 2.0f || std::fabs(x.me.aim.z) < kF5)
    return false;
  if (flag && (std::fabs(x.P.z) < kF7 || A720(x.B.x, x.P.x, x.s, 7.0f) || x.D2() > 144.0f))
    return false;
  if (std::fabs(x.P.z - x.B.z) < 3.0f || std::fabs(x.B.z) < std::fabs(x.P.z) ||
      std::fabs(x.B.z) < kF7 + 1.5f || x.s * x.B.x > kF8)
    return true;
  // ponytail: 44e020 (the T+0xc060 per-player value) is undecoded; taken as
  // 60, so this test passes on.
  const int r = x.q;
  if (!Valid(r)) return true;
  const Vec2f rp = x.w.p[r].pos;
  for (int o : OppByDist(x.w, x.id)) {
    const Vec2f op = x.w.p[o].pos;
    if (Dist2(op, x.P) > 64.0f) return true;
    if (std::fabs(op.z) <= std::fabs(rp.z) && A720(rp.x, op.x, x.s, -1.5f) &&
        Ad(x.dir, PBrg(rp, op)) <= 95.0f) {
      if (x.s * rp.x < x.s * x.P.x && std::fabs(rp.x - x.P.x) > 6.0f) return true;
      if (ConeOpp(x.w, x.id, x.dir, 50.0f, 50.0f)) return true;
      for (int j = x.t * kSlots; j < x.t * kSlots + kSlots; ++j) {
        if (j == x.id || j == r || !x.w.p[j].active) continue;
        const Vec2f jp = x.w.p[j].pos;
        if (x.s * jp.x < kF6 - 5.0f) return true;
        if (Sgn(x.P.z - x.B.z) == Sgn(jp.z - x.B.z) &&
            (x.w.p[j].state == kCenteringGet || (x.s * jp.x > kF6 && std::fabs(jp.z) < kF5)))
          return false;
      }
      return true;
    }
  }
  return true;
}
// s3 #2 (Chance only). 594a10 (path test) and 695110 not ported: that tail
// keeps.
bool L690960(const Ctx& x) {
  if (x.me.state != kChance || x.tt < Frames(0.6f) || !Beyond(x.w, x.id, -7.0f)) return false;
  const float L = x.w.lineX[x.opp];
  float k = -16.0f;
  if (Valid(x.c)) {
    if (!Beyond(x.w, x.id, -3.0f)) return false;
    k = -20.0f;
  }
  if (x.D2() < 484.0f) return false;
  if (A720(L, x.B.x, x.s, k)) return false;
  return Beyond(x.w, x.id, 0.0f);
}
// s3 #8 (cls != 0x13).
bool L693ef0(const Ctx& x) {
  if (x.s * x.P.x < kF8 || std::fabs(x.P.z) < kF2 + 1.5f) return false;
  if (std::fabs(x.B.z) > std::fabs(x.P.z) || std::fabs(x.P.z - x.B.z) < 6.0f) return false;
  std::vector<int> mates;
  for (int j = x.t * kSlots; j < x.t * kSlots + kSlots; ++j)
    if (x.w.p[j].active) mates.push_back(j);
  std::sort(mates.begin(), mates.end(),
            [&](int a, int b) { return x.s * x.w.p[a].pos.x > x.s * x.w.p[b].pos.x; });
  for (int j : mates) {
    const Vec2f jp = x.w.p[j].pos;
    if (x.s * jp.x < x.s * x.P.x) break;
    if (std::fabs(jp.z - x.B.z) <= std::fabs(x.P.z - x.B.z)) return true;
  }
  return false;
}
// s3 #10.
bool L68d730(const Ctx& x) {
  if (x.s * x.B.x < kF8 || std::fabs(x.B.z) < kF5 || std::fabs(x.B.z) > kF7) return false;
  const float n = Frames(0.3f) / kPesFps;
  const Vec2f Q{x.P.x + n * x.me.vel.x, x.P.z + n * x.me.vel.z};
  if (Sgn(Q.z) != Sgn(x.B.z)) return false;
  auto inChannel = [&](Vec2f v) {
    return x.s * v.x > kF8 && std::fabs(v.z) > kF5 && std::fabs(v.z) < kF7;
  };
  return inChannel(Q) || inChannel(x.me.aim);
}
// s3 #11.
bool L68fc70(const Ctx& x) {
  if (x.s * x.B.x < kF8) return false;
  const Vec2f A = x.me.aim;
  if (Sgn(A.z) == Sgn(x.B.z)) return false;
  return std::fabs(A.z) >= kF5 && std::fabs(x.B.z - A.z) >= 8.0f;
}
// s3 #12: the c064 value taken as 0 (passes).
bool L68cde0(const Ctx& x) {
  const Vec2f A = x.me.aim;
  if (std::fabs(A.z) < kF7 - 2.0f) return false;
  for (int j = x.t * kSlots; j < x.t * kSlots + kSlots; ++j) {
    if (j == x.id || !x.w.p[j].active || x.w.p[j].state != kOverlap) continue;
    const Vec2f jp = x.w.p[j].pos;
    if (Sgn(jp.z) == Sgn(A.z)) {
      if (std::fabs(jp.x - x.P.x) >= 6.0f && !Beyond(x.w, j, -7.0f))
        return kF6 - 10.0f < x.s * jp.x;
      return true;
    }
  }
  return false;
}
// s3 #13 (marks 0).
bool L68cc40(const Ctx& x) {
  if (x.tt < Frames(0.3f) || x.s * x.P.x < kF6) return false;
  if (!A720(x.B.x, x.P.x, x.s, 3.0f) || x.D2() < 144.0f) return false;
  return Beyond(x.w, x.id, -1.0f);
}
// b4 #15 -> 694ee0 (decompiled for this port).
bool L694ee0(const Ctx& x, bool flag) {
  if (!Valid(x.q) || std::fabs(x.B.z) < kF7) return false;
  const PlayerFacts& q = x.w.p[x.q];
  const float k = flag ? 0.8f : 0.0f, a = flag ? 2.0f : 0.0f;
  if (Ad(x.dir, PVec(q.vel)) > 85.0f || Ad(x.dir, q.facing) > 85.0f) return false;
  return !ConeOpp(x.w, x.q, x.dir, a + 90.0f, k + 40.0f) &&
         !ConeOpp(x.w, x.q, x.dir, a + 160.0f, k + 4.5f) &&
         !ConeOpp(x.w, x.q, x.dir, a + 120.0f, k + 6.0f);
}
bool L68ac40(const Ctx& x, Vec2f pos, bool flag) {
  if (!Valid(x.q) || std::fabs(x.B.z) < kF7) return false;
  if (Sgn(x.P.z) != Sgn(x.B.z) || std::fabs(x.P.z) > std::fabs(x.B.z)) return false;
  const float k = flag ? 0.8f : 0.0f;
  if (A720(x.B.x, x.P.x, x.s, 5.0f + k) && !A720(x.w.lineX[x.opp], x.B.x, x.s, -10.0f - k))
    return false;
  const float lim = x.s * x.B.x <= kF6 - 12.0f ? kF7 + 0.8f - k : kF5 + 0.8f - k;
  if (std::fabs(pos.z) < lim) return false;
  return L694ee0(x, flag);
}

// 643f50 (decompiled for this port): the carrier (or last toucher of the
// team) is free to cross: in the box zone and no opponent in four cones.
bool L643a30(const RunWorld& w, int team, int id);
bool L643f50(const RunWorld& w, int team) {
  const int c = w.carrier;
  if (!Valid(c) || TeamOf(c) != team) return false;
  const float s = w.s[team];
  const Vec2f cp = w.p[c].pos;
  if ((!L643a30(w, team, c) && s * cp.x < kF6 - 6.0f) || std::fabs(cp.z) < kF7 + 2.0f) return false;
  const float dir = TDir(w, team);
  if (ConeOpp(w, c, dir, 110.0f, 15.0f) || ConeOpp(w, c, dir, 165.0f, 5.5f) ||
      ConeOpp(w, c, dir, 220.0f, 3.0f) || ConeOpp(w, c, dir, 265.0f, 2.2f))
    return false;
  return true;  // carrier valid: the decompile returns true on this path
}
// b1 #23, carrier branch only. ponytail: a63950/held (instruction 7), a72fb0
// (the grid lookup) are not ported; the line checks without them.
bool L643a30(const RunWorld& w, int team, int) {
  const int c = Valid(w.carrier) ? w.carrier : -1;
  if (c < 0) return false;
  const float s = w.s[team];
  const float A = TDir(w, team);
  const float vb = PVec(w.p[c].vel);
  if (s * w.ball.x < kF6 && Ad(vb, A) > 65.0f) return false;
  if (s * w.ball.x < kF6 && std::fabs(w.ball.z) < kF7) return false;
  return true;
}


// The cascade FUN_140688c00 (run-targets 3), steps numbered as there.
bool CommonStop(const RunWorld& w, int id, int run) {
  const Ctx x(w, id);
  const bool counter = run == kCounter;
  // Head (off-ball 2.2).
  if (!w.openPlay || !w.possession[x.t]) return true;
  if (w.carrier == id) return true;
  if (x.s * x.P.x > kF4 + 2.5f) return true;
  // 1: the last ball event is a pass to him.
  if (EvKick(w.ev) && w.ev.target == id) {
    if (w.ev.kind != 2) return true;
    if (w.ev.age >= kPesFps / 3.0f) return true;
  }
  // 2: ponytail: the carrier/receiver pass-aim keeps (5a7fe0/7be0) and stop
  // (5a7d10) read PES action records (+0x1490/+0x1494) GF has no equivalent of.
  // 3.
  if (Valid(x.c) && !Beyond(w, id, 0.7f) && w.possessionAge[x.t] < Frames(0.3f)) return false;
  // 4. ponytail: 68bd90 needs T+0xb824 (the team's region code), not decoded.
  if (L68f600(x)) return true;
  // 5: event kind 4 has no GF counterpart.
  // 6.
  const bool pinned = false;
  const float dx = std::fabs(x.P.x - x.B.x);
  const float k = dx <= 4.0f ? 10.0f : dx < 10.0f ? 10.0f * (10.0f - dx) / 6.0f : 0.0f;
  if (!L596240(x)) return true;
  // 8.
  const float d2 = x.D2();
  if (d2 > (k + 40.0f) * (k + 40.0f) && !A720(x.P.x, x.B.x, x.s, counter ? 13.0f : 8.0f))
    return true;
  if (!counter && L692810(x)) return true;
  // 9.
  if (L692a00(x)) return true;
  if (!counter && L692580(x)) return true;
  // 10.
  const float n = kRunPredictFrames / kPesFps;
  const Vec2f pred{x.P.x + n * x.me.vel.x, x.P.z + n * x.me.vel.z};
  bool intercept = false;
  for (int o = x.opp * kSlots; o < x.opp * kSlots + kSlots && !intercept; ++o)
    if (w.p[o].active && Dist2(w.p[o].pos, x.B) >= 6.25f &&
        LaneIntercept(x.B, pred, x.P, w.p[o].pos, 20.833334f, true) >= 0.0f)
      intercept = true;
  // 11. 5a4b90 needs the opponents' defensive mode (T+0xa50c): false.
  if (Valid(x.c) && x.tt > 2.0f * kPesFps && !L68a730(x, run) && !L68a8c0(x, intercept) &&
      !counter && !pinned) {
    const PlayerFacts& c = w.p[x.c];
    const float a = Ad(c.facing, PBrg(x.B, x.me.aim));
    if (a > 130.0f && d2 > 169.0f && (ConeOpp(w, x.c, x.dir, 70.0f, 6.5f) || d2 > 1156.0f))
      return true;
  }
  // 12. ponytail: of the run != 0x1d skip tests only |ball.x - P.x| < 15 is
  // ported (the rest are listed, not reduced, in run-targets 3).
  if (Valid(x.c) && x.tt > kPesFps + 0.5f && !counter && !pinned && d2 >= 289.0f &&
      std::fabs(x.P.z - x.B.z) > 0.5f && A720(x.B.x, x.P.x, x.s, -5.0f) &&
      !(run != kSecondLine && dx < 15.0f)) {
    const PlayerFacts& c = w.p[x.c];
    const float a = Ad(c.facing, PBrg(c.pos, x.P));
    const float dist = std::sqrt(Dist2(c.pos, x.P));
    const float wv = dist <= 15.0f ? 1.0f : dist < 27.0f ? (27.0f - dist) / 12.0f : 0.0f;
    if (a > 95.0f + 35.0f * wv && !L694d10(x) && (!Beyond(w, id, -10.0f) || a > 80.0f)) return true;
  }
  // 13.
  if (!pinned && (L68dba0(x) || L68de30(x) || L68e300(x))) return true;
  if (!counter && std::fabs(x.P.z - x.B.z) > 43.0f) return true;
  // 14 (cls != 2).
  if (run != kPullAway) {
    const float r = x.tt > Frames(0.8f) ? 0.7f : 1.5f;
    if (Valid(x.q) && (x.q == x.c || w.p[x.q].t <= 3.0f * kPesFps)) {
      if (Beyond2(w, id, r)) return true;
    } else if (Beyond(w, id, r)) {
      return true;
    }
  }
  // 15: state byte (falls) has no GF counterpart.
  // 16.
  if (x.tt > 4.0f * kPesFps && Valid(x.c) && !L68a730(x, run) && !L68a8c0(x, intercept) &&
      Beyond2(w, id, -2.5f)) {
    const PlayerFacts& c = w.p[x.c];
    const float dev = Ad(PBrg(c.pos, x.P), c.facing);
    const float bd = std::sqrt(d2);
    const float wv = bd <= 15.0f ? 1.0f : bd < 35.0f ? (35.0f - bd) / 20.0f : 0.0f;
    if (dev > 100.0f * wv + 70.0f) return true;
  }
  // 17 (Counter): keep-alive near a running mate skips to 19.
  bool skip18 = false;
  if (counter && x.tt <= 5.0f * kPesFps)
    for (int j = x.t * kSlots; j < x.t * kSlots + kSlots; ++j)
      if (j != id && IsRun(w.p[j].state) && Dist2(w.p[j].pos, x.P) <= 56.25f &&
          Ad(x.dir, PBrg(x.P, w.p[j].pos)) <= 70.0f)
        skip18 = true;
  // 18. ponytail: 5a40a0 -> 5a3080 (partial in -b4 19) is not ported.
  (void)skip18;
  // 19. ponytail: 5a1cc0 and the carrier-action stop need the PES path/action
  // records; 694320 needs 5a8290/6940f0 (not decoded): all taken as false.
  if (counter || (!L68efc0(x) && (pinned || (!L68e900(x) && !L68ec50(x))))) {
    if (x.tt <= 3.0f * kPesFps || !L68d920(x)) {
      if (!pinned && (L68b070(x) || L692c50(x) || L68b920(x) || L691ef0(x) ||
                      (!counter && x.tt > Frames(2.0f) && L692fe0(x))))
        return true;
      // 68b1a0 (FUN_14068b1a0): the crowded-mate stop, past fps/2 frames.
      if (!pinned && x.tt > Frames(0.5f) && CrowdedMateStop(w, id)) return true;
      // 68b6e0 the ball-sorted frame list; 693340 the 643a30 grid read;
      // 694bc0 instruction 6: all false.
      const bool anyKeep = L690b90(x) || L690630(x) || L690400(x) || L690cf0(x) ||
                           L694650(x) || L693a20(x) || L693730(x, intercept) ||
                           L68e660(x, intercept) || L68b440(x, intercept) || L68c050(x) ||
                           L692ea0(x);
      if (!anyKeep) {
        // 691d10 (SecondLine sub 8) and 68fa10 (T+0x87cc) are not reached.
        if (L691a80(x)) return true;
        if (!L68d0b0(x, pinned)) {
          // 691300 (sub 2) not reached; 68aeb0 releases the run when the runner
          // beats the receiver's lead to his own route point.
          if (L690960(x)) return true;
          if (x.tt > Frames(2.0f) && PassToMeStop(w, id)) return true;
          if (L68ac40(x, x.me.aim, true) || L693ef0(x)) return true;
          // 68c1a0 (controlled path), 68baa0 (skill cards), 6910c0 (cls 0x19)
          // are not reached.
          if (!(L68d730(x) || L68fc70(x) || L68cde0(x) || L68cc40(x))) return false;
        }
      }
    }
  }
  // 20.
  return true;
}

}  // namespace

// --- FUN_140654e60, the time-to-reach (raw 0x140654e60-0x14065565c) ---
//
// PES steps the player toward the target two frames at a time. The per-frame
// distance starts at sin((dev+90)/2)·speed·align - the heading factor
// FUN_140a92c80 - and climbs across the speed tiers
// 100/(fps·{12.9, 22.5, 32.7, 72}) m/frame; while the player has not turned
// far enough toward the target (4.5417 deg a frame = 2·int(fps+0.5)/48·2) his
// distance gain is capped by the tier he is in. The loop ends when the walked
// distance reaches the target or 20 s pass, and the answer adds the turn
// penalty int(int(fps·0.25+0.5)·ramp(dev, 90→155)) scaled by the second
// heading ramp and int(fps·0.4+0.5)·u (u = the speed ramp over 11→22 km/h).
constexpr float kTurnDegPerFrame = 2.0f * 109.0f / 48.0f;  // 2·int(fps+0.5)/48·2
constexpr float kReachTier20_s = 20.0f;   // 100/(fps·20) m/frame, the walk tier
constexpr float kReachTier12_9_s = 12.9f;  // 100/(fps·12.9) m/frame, the jog tier
constexpr float kReachTier22_5_s = 22.5f;  // 100/(fps·22.5) m/frame
constexpr float kReachTier32_7_s = 32.7f;  // 100/(fps·32.7) m/frame
constexpr float kReachTier72_s = 72.0f;    // 100/(fps·72) m/frame, the sprint tier
constexpr float kReachWalkFloor = 0.8f;   // 0.8·t20: below it the player cannot turn
constexpr float kReachTurnGate_deg = 55.0f;  // dev <= 55 turns without the accel step
constexpr float kReachAccelFast_s = 1.3f;    // seconds to climb the top two tiers
constexpr float kReachAccelSlow_s = 0.15f;   // seconds to climb the bottom two
constexpr float kReachMaxFrames_s = 20.0f;   // the loop's 20 s ceiling
constexpr float kReachTurnFrom_deg = 90.0f;  // the turn penalty starts at 90 deg
constexpr float kReachTurnSpan_deg = 65.0f;  // ... and saturates 65 deg later
constexpr float kReachTurnScale_s = 0.25f;   // int(fps·0.25+0.5) frames at full
constexpr float kReachAlignFrom_deg = 65.0f; // the second heading ramp's start
constexpr float kReachAlignTo_deg = 120.0f;  // ... and its end
constexpr float kReachAlignScale_s = 0.4f;   // int(fps·0.4+0.5) frames at full
constexpr float kReachSpeedFrom_kmh = 11.0f; // the speed ramp 11 -> 22 km/h
constexpr float kReachSpeedTo_kmh = 22.0f;
constexpr float kReachAlignNear_deg = 35.0f; // 30 + 35·(1-u): the full-alignment band
constexpr float kReachAlignFar_deg = 25.0f;  // ... and the dead band past it

// FUN_140a92c80(x, scale): sign·scale·sin(fold180(x)/2), the 512-step table at
// 0x142655950 (its values are sin(i·90/512 deg); the install test reads the
// table's ends and midpoint).
float HeadingFactor(float deg, float scale) {
  const float folded = Fold180(deg);
  const float v = std::sin(folded * 0.5f * kRad);
  return folded < 0.0f ? -scale * v : scale * v;
}

// FUN_140655660 + FUN_140654e60: the frames for player `id` to reach `X`.
// ponytail: FUN_140a75740 (the path) is the straight segment - GF has no
// obstacle path, so the waypoint list is the destination itself.
int FramesToReach(const RunWorld& w, int id, Vec2f X) {
  const PlayerFacts& p = w.p[id];
  const float speed = Len(p.vel);                      // metres per frame
  const float facing = p.facing;
  const float need = Bearing(p.pos, X);                // FUN_140439e50
  const float dev = std::fabs(Ad(facing, need));
  const float heading = PVec(p.vel);
  // u: 0 at 11 km/h, 1 at 22 km/h (0x140655033: 3.6·speed against 11 and 22).
  const float kmh = speed * kPesFps * 3.6f;
  const float u = Ramp(kmh, kReachSpeedFrom_kmh, kReachSpeedTo_kmh);
  const float near = kReachAlignNear_deg + 35.0f * (1.0f - u);
  const float far = near + kReachAlignFar_deg;
  const float alignRamp = dev <= near ? 1.0f : dev >= far ? 0.0f : (far - dev) / (far - near);
  const float perFrame = HeadingFactor(dev + 90.0f, 1.0f) * speed * alignRamp;
  const float t20 = Per100(kReachTier20_s) / kPesFps;
  const float t129 = Per100(kReachTier12_9_s) / kPesFps;
  const float t225 = Per100(kReachTier22_5_s) / kPesFps;
  const float t327 = Per100(kReachTier32_7_s) / kPesFps;
  const float t72 = Per100(kReachTier72_s) / kPesFps;
  const float dist = std::sqrt(Dist2(p.pos, X));
  float gain = perFrame, turned = 0.0f, walked = 0.0f;
  int frames = 0;
  while (walked < dist && frames <= static_cast<int>(Frames(kReachMaxFrames_s))) {
    if (!(turned < dev) || (t20 * kReachWalkFloor > gain) || dev <= kReachTurnGate_deg) {
      float span = 0.0f, sec = kReachAccelSlow_s;
      if (gain >= t225) {
        span = t20 - t225;
        sec = kReachAccelFast_s;
      } else if (gain >= t327) {
        span = t225 - t327;
      } else {
        span = t327 - t72;
      }
      if (t129 > gain) {
        const int n = std::max(1, static_cast<int>(kPesFps * sec + 0.5f));
        gain += 2.0f * span / static_cast<float>(n);
      }
    }
    if (dev > turned) turned += kTurnDegPerFrame;
    walked += 2.0f * gain;
    frames += 2;
  }
  if (frames > static_cast<int>(Frames(kReachMaxFrames_s)))
    frames = static_cast<int>(dist / t129);
  // The turn penalty and the second heading ramp (0x1406554b1-0x1406555ea).
  const float turnRamp = dev <= kReachTurnFrom_deg ? 0.0f
                        : dev >= kReachTurnFrom_deg + kReachTurnSpan_deg ? 1.0f
                        : (dev - kReachTurnFrom_deg) / kReachTurnSpan_deg;
  const int turnPenalty =
      static_cast<int>(Frames(kReachTurnScale_s) * turnRamp);
  const float dev2 = std::fabs(Ad(heading, need));
  const float second = dev2 >= kReachAlignFrom_deg
                           ? Ramp(dev2, kReachAlignTo_deg, kReachAlignFrom_deg)
                           : 1.0f;
  int scale = static_cast<int>(Frames(kReachAlignScale_s) * u);
  if (p.position == 0) scale = static_cast<int>(static_cast<float>(scale) * 0.5f);  // rec[1] == 3
  return frames + static_cast<int>(turnPenalty * second + static_cast<float>(scale));
}

// FUN_14068b1a0: the crowded-mate stop, reached at step 19 with t > fps/2.
bool CrowdedMateStop(const RunWorld& w, int id) {
  const Ctx x(w, id);
  if (x.me.aiClass != 2) return false;                // FUN_140a580a0 != 2
  // a4c9a0(id, -7): the runner at least 7 m behind the opponents' line.
  if (!A720(w.lineX[x.opp], x.P.x, x.s, -7.0f)) return false;
  // Within 20 m of the ball-line point (MA+0x2482c/0x24834: the ball at the
  // snap). ponytail: GF's snap is the current ball position.
  if (Dist2(x.P, w.ball) >= 400.0f) return false;
  for (int j = x.t * kSlots; j < x.t * kSlots + kSlots; ++j) {
    if (j == id || !w.p[j].active) continue;
    if (!IsRun(w.p[j].state)) continue;               // FUN_1405a4830
    const bool chanceMate = w.p[j].state == kChance && w.p[j].aiClass == 1;
    if (chanceMate) continue;
    if (Dist2(x.P, w.p[j].pos) < 144.0f) return true;
  }
  return false;
}

// FUN_14068aeb0: the pass-to-me stop, reached at step 19 with t > 2·fps.
bool PassToMeStop(const RunWorld& w, int id) {
  if (!EvKick(w.ev)) return false;                     // a3e190 kind not a kick
  if (w.ev.target == id) return false;                 // a62e40 == id
  const int t = TeamOf(id);
  const int q = w.receiver[t];                         // FUN_1405969b0
  if (!Valid(q) || q == id) return false;
  // FUN_140a580a0: an SB or a 0x13-class runner never keeps this way.
  if (w.p[id].aiClass == 2 || w.p[id].aiClass == 0x13) return false;
  const float s = w.s[t];
  // a4c9a0(id, -4): at least 4 m behind the opponents' line, and not more than
  // 1.2 m past it (a4c9a0(id, -1.2)).
  if (!A720(w.lineX[t ^ 1], w.p[id].pos.x, s, -4.0f)) return false;
  if (A720(w.lineX[t ^ 1], w.p[id].pos.x, s, -1.2f)) return false;
  if (!OppNear(w, id, 5.0f)) return false;             // a4e250(id, 5)
  // FUN_140653480: the receiver's lead, from his distance to the snap (the
  // ball): 196/676 m² thresholds, 0.28/0.35/0.38 s (0x1425c4c18/0x1425cb48c).
  const float d2 = Dist2(w.p[q].pos, w.ball);
  const float sec = d2 < 196.0f ? 0.28f : d2 < 676.0f ? 0.35f : 0.38f;
  const int lead = static_cast<int>(kPesFps * sec + 0.5f);
  // FUN_140655660 on the runner's own route point (FUN_140596c30).
  const int mine = FramesToReach(w, id, w.p[id].aim);
  return lead >= mine + static_cast<int>(kPesFps * 0.15f + 0.5f);
}

// Step 2 of the manager tick (off-ball 1.2): the per-slot re-run block. A slot
// on cooldown is not a candidate; being the pass receiver clears it, and being
// on a run does not - a slot can be running and still blocked by the timer,
// which PES reads as "this slot has just finished a run".
bool RunCooldownBlocks(const RunWorld& w, int id) { return w.p[id].runCooldown > 0; }

int RoleClass(int gfRole) {
  constexpr int kMap[] = {0, 1, 2, 2, 3, 4, 5, 5, 6, 9, 8, 8, 7};
  return gfRole >= 0 && gfRole < static_cast<int>(std::size(kMap)) ? kMap[gfRole] : 0;
}

int Position(int gfRole) {
  // GK CB LB RB DM CM LM RM AM CF LW RW SS -> 0 1 2 3 4 5 6 7 8 12 9 10 11
  constexpr int kMap[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 9, 10, 11};
  return gfRole >= 0 && gfRole < static_cast<int>(std::size(kMap)) ? kMap[gfRole] : 0;
}

namespace {
// The team's players front-first by s·x (the a714e0 line list).
std::vector<int> FrontFirst(const RunWorld& w, int team) {
  std::vector<int> v;
  for (int i = team * kSlots; i < team * kSlots + kSlots; ++i)
    if (w.p[i].active) v.push_back(i);
  const float s = w.s[team];
  std::stable_sort(v.begin(), v.end(),
                   [&](int a, int b) { return s * w.p[a].pos.x > s * w.p[b].pos.x; });
  return v;
}
int LineRank(const RunWorld& w, int id) {
  const std::vector<int> v = FrontFirst(w, TeamOf(id));
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i] == id) return static_cast<int>(i);
  return kSlots;
}
// a4f230: attackers of the team in possession ahead of the ball band.
int AttackersAhead(const RunWorld& w) {
  const int t = w.possession[0] ? 0 : 1;
  const float s = w.s[t];
  const int owner = Valid(w.carrier) ? w.carrier : w.receiver[t];
  float line = 0.0f;
  for (int i : FrontFirst(w, t))
    if (s * w.p[i].pos.x <= (w.ball.x - s * 1.5f) * s) {
      line = w.p[i].pos.x;
      break;
    }
  const float back = w.ball.x - s * 7.0f;
  if (s * line <= back * s) line = back;
  int n = 0;
  for (int i = t * kSlots; i < t * kSlots + kSlots; ++i) {
    if (!w.p[i].active || i == owner) continue;
    const float x = w.p[i].pos.x;
    if (((w.ball.x - s * 5.0f) * s < s * x || s * line < s * x) &&
        (w.ball.x - s * 12.0f) * s < s * x && std::fabs(w.p[i].pos.z - w.ball.z) <= 52.0f)
      ++n;
  }
  return n;
}
// h1 #23 FUN_1405a5240: the team's focus player is free and faces forward.
bool ReceiverFreeAndFacing(const RunWorld& w, int t) {
  const int id = w.receiver[t];
  if (!Valid(id)) return false;
  if (OppNear(w, id, 1.0f)) return false;
  const float dir = TDir(w, t);
  if (ConeOpp(w, id, dir, 120.0f, 4.5f)) return false;
  return Ad(PVec(w.p[id].vel), dir) < 60.0f;
}
// FUN_1405938a0(team, 0x7e) with FUN_140593290 (decompiled for this port):
// the team may start Chance runs at all.
bool TeamMayRun(const RunWorld& w, int t) {
  if (!w.possession[t]) return false;
  const float s = w.s[t];
  // 593290: a carrier on the team; else its focus player within 25 m
  // (625 m², 0x1425ab4c4) of the ball and the last kick at least 4 frames old.
  if (Valid(w.carrier)) {
    if (TeamOf(w.carrier) != t) return false;
  } else {
    const int q = w.receiver[t];
    if (!Valid(q) || Dist2(w.p[q].pos, w.ball) > 625.0f) return false;
    if (w.ev.valid && w.ev.age < 4.0f) return false;
  }
  // a4c930(receiver, ball.x): the ball in the team's own half.
  const bool ownHalf = s > 0.0f ? w.ball.x <= 0.0f : w.ball.x >= 0.0f;
  // ponytail: the a4c720 test on MA+0x3c+0x14t (arguments dropped by the
  // decompile) and the holder flags (a50250) are taken false; T+0xa508 == 2
  // (the latched special counter) is not modelled.
  constexpr float kOwnHalfLimit_m = -15.0f;  // 0x14259d668
  const bool open = ReceiverFreeAndFacing(w, t) || !ownHalf ||
                    (s * w.ball.x >= kOwnHalfLimit_m && w.possessionAge[t] >= Frames(4.0f));
  if (!open) return false;
  constexpr float kEndBand_m = 10.0f;  // F0 - 10, 0x142588210
  return s * w.ball.x <= kF0 - kEndBand_m || std::fabs(w.ball.z) <= kF7;
}
}  // namespace

// FUN_140606cd0's rank > 1 angle sub-test, raw 0x140607741-0x140607814. The
// rank byte is the attacking team's line-list position of the runner
// (FUN_140a714e0's +0xb2); the cone is 80 deg about the attack direction from
// the runner, and the 20 m window is measured against the ball-line point
// (MA+0x2482c/0x24834). GF carries no instruction 2 (FUN_1405aaf00 = 0), so the
// closing a4c720's arguments are a = 0 and k = 0: the ball-line point must be
// in the attacking half.
bool RankAngleOk(const RunWorld& w, int id, int attackLevel) {
  const Ctx x(w, id);
  // The gate chain: the runner on the mask's generic path is already past the
  // line and the slot mask. The angle test needs the reach path taken
  // (attackers ahead > 3, or a forward without the LB style bit) - which
  // ChanceMaskBit has already decided - so it is entered whenever the attacker
  // count clears it.
  const int ahead = AttackersAhead(w);
  const bool styleLb = x.me.position == 2;  // a64030(id, 0x11): no style bits in GF
  if (!(ahead > 3 || (x.me.position < 4 && !styleLb))) return true;
  // The rank byte: the attacking team's line-list position of this runner.
  if (LineRank(w, id) <= 1) return true;
  // ponytail: FUN_1405a0d20 (the "go straight for the line" duel test) and
  // FUN_140a64030(id, 9) (style bit 9: none in GF) are taken false, so the
  // style-bit branch of the sub-test is always entered.
  const int pos = x.me.position;
  if (pos > 3) {
    // 0x140607747: the bearing from the ball-line point to the runner, and the
    // runner's facing must agree within 45 deg.
    const float need = Bearing(w.ball, x.P);
    if (std::fabs(Fold180(x.me.facing - need)) >= 45.0f) return true;  // 0x14259be30
    // 0x1406077d: the cone's centre is the attack direction and its radius is
    // 30 m (xmm14, 0x142591140).
    constexpr float kRankCone_deg = 80.0f;  // 0x142596e6c
    constexpr float kRankConeRadius_m = 30.0f;  // 0x142591140
    constexpr float kRankWindow_m2 = 400.0f;  // 0x1425a2670
    if (ConeOpp(w, id, x.dir, kRankCone_deg, kRankConeRadius_m)) return true;
    // 0x1406077be: past 20 m from the ball-line point the runner is rejected,
    // as is a line point that is not ahead in the attack frame.
    if (Dist2(x.P, w.ball) >= kRankWindow_m2) return false;
  }
  // 0x1406077e4-0x140607814: a4c720(a, MA+0x2482c, s, k) with GF's a = 0 and
  // k = 0 (no instruction 2) rejects when the ball-line point is not ahead in
  // the attack frame.
  (void)attackLevel;
  return w.ball.x * x.s > 0.0f;
}

uint32_t AttackSlotMask(const RunWorld& w, int team, int L) {
  constexpr uint32_t kAll = (1u << kSlots) - 1;
  if (L >= 4) return kAll;
  // ponytail: the 0 < L < 4 FUN_1405aaf00 branch needs instruction 2 (none).
  const int need = L == 0 ? 0 : L == 1 ? 2 : L == 2 ? 4 : 6;
  int ahead = AttackersAhead(w);
  const float s = w.s[team];
  int beyond = 0;
  for (int i : FrontFirst(w, team)) {
    if (s * w.p[i].pos.x < kPenaltyAreaLineX_m) break;
    ++beyond;
  }
  ahead = std::max(ahead, beyond);
  if (ahead < need) return kAll;
  // Positions 7+ always qualify (score -100); the rest by distance to the
  // opponents' goal line (INF med: the line-list value), less up to 5 near
  // the ball (10 -> 25 m) and 3 on the receiver's turning side; the `need`
  // lowest take the slots.
  struct E { int slot; float v; };
  std::vector<E> e;
  uint32_t mask = 0;
  const int q = w.receiver[team];
  bool turning = false;
  float side = 1.0f;
  if (Valid(q)) {
    const PlayerFacts& r = w.p[q];
    if (Ad(PVec(r.vel), r.facing) > 15.0f) {
      turning = true;
      side = r.aim.z - r.pos.z < 0.0f ? -1.0f : 1.0f;
    }
  }
  for (int i = team * kSlots; i < team * kSlots + kSlots; ++i) {
    if (!w.p[i].active) continue;
    const int slot = i - team * kSlots;
    if (w.p[i].position >= 7) {
      e.push_back({slot, -100.0f});
      mask |= 1u << slot;
      continue;
    }
    float v = kFieldHalfLength_m - s * w.p[i].pos.x;
    const float d = std::sqrt(Dist2(w.p[i].pos, w.ball));
    const float near = d <= 10.0f ? 1.0f : d < 25.0f ? (d - 25.0f) / -15.0f : 0.0f;
    v -= near * 5.0f;
    if (turning && Valid(q) && (w.p[i].pos.z - w.p[q].pos.z < 0.0f ? -1.0f : 1.0f) == side)
      v += -3.0f;
    e.push_back({slot, v});
  }
  std::stable_sort(e.begin(), e.end(), [](const E& a, const E& b) { return a.v < b.v; });
  for (int i = 0; i < need && i < static_cast<int>(e.size()); ++i) mask |= 1u << e[i].slot;
  return mask;
}

bool ChanceMaskBit(const OffBallConstants& k, const RunWorld& w, int id, uint32_t slotMask,
                   int attackLevel, bool buildUpLong, float awareness) {
  return ChanceMaskReject(k, w, id, slotMask, attackLevel, buildUpLong, awareness) == kMaskPass;
}

// FUN_140606cd0's gates in walk order, naming the one that rejected `id`.
MaskReject ChanceMaskReject(const OffBallConstants& k, const RunWorld& w, int id, uint32_t slotMask,
                            int attackLevel, bool buildUpLong, float awareness) {
  const Ctx x(w, id);
  const int slot = id - x.t * kSlots;
  const PlayerFacts& me = x.me;
  // Pre-gates: rank (or OVERLAP_SB) and FUN_140593430.
  if (!(MayTake(kChance, me.state) || me.state == kOverlap)) return kMaskRankOrState;
  if (id == w.carrier || id == x.q || me.position == 0) return kMaskCarrierOrReceiver;
  if (IsRun(me.state)) return kMaskAlreadyRunning;  // 593430: a running slot only from PULL_AWAY
  // 593430 (raw 0x1405935c4: k = -0.1 @0x14259d648): not already on the line.
  if (Beyond2(w, id, -0.1f)) return kMaskOnTheLine;
  // Inside the box zone only with an opponent within 6.5 m (raw 0x14059373d).
  if (!(x.s * x.P.x <= kF6 - 3.0f || std::fabs(x.P.z) >= kF5 + 1.5f || OppNear(w, id, 6.5f)))
    return kMaskBoxZone;
  if (x.s * x.P.x > kF4) return kMaskGoalArea;
  if (!L596240(x)) return kMaskRoleClass;
  // ponytail: 5a7d10 (carrier pass-aim records) is not applied; the special
  // lanes 60a750/608570/87a0/9350/8ac0 are not ported.
  // Generic lane: the attack-slot mask and the depth limit.
  if (RunCooldownBlocks(w, id)) return kMaskRunCooldown;
  if (me.state == kOverlap || !((slotMask >> slot) & 1u)) return kMaskSlotMask;
  if (x.s * x.P.x > kF8) return kMaskDepth;
  if (!TeamMayRun(w, x.t)) return kMaskTeamMayNotRun;
  const int rank = LineRank(w, id);
  // ponytail: MA+0x68/+0x60 team ints unknown; taken 0 (this gate passes).
  const int pos = me.position;
  // b8a3 = attack level < 1 skips positions 0..4 (counter-trigger 128).
  if (attackLevel < 1 && pos <= 4) return kMaskAttackLevel;
  bool styleLb = pos == 2;  // a64030(id, 0x11): no style bits in GF
  const int ahead = AttackersAhead(w);
  const bool reachPath = ahead > 3 || (pos < 4 && !styleLb);
  const float dz = std::fabs(x.P.z - x.B.z);
  if (reachPath) {
    float dh = 0.0f;
    if (dz > k.reachDhFrom_m) dh = dz < k.reachDhTo_m ? (dz - k.reachDhTo_m) / k.reachDhDiv_m : 0.0f;
    // Build Up Long (a62180(team, 1) == 0) widens the base 15 -> 21.
    constexpr float kLongBase_m = 21.0f;  // 0x1425c84bc
    float r = (buildUpLong ? kLongBase_m : k.reachBase_m) + dh * k.reachDhGain_m;
    // a50250(2) floor 24, 5a5b20 = 26 (needs style bit 0), 60a100 clamp,
    // aaf00 floor 35, 5a9950 clamp: all off for GF's inputs (no holder flags,
    // no style bits, no instructions, AI class 0).
    float bonus = 0.0f;
    if (buildUpLong && Valid(w.carrier)) {
      if (awareness > k.awareTo)
        bonus = k.awareBonus_m;
      else if (awareness > k.awareFrom)
        bonus = (awareness - k.awareFrom) / k.awareSpan * k.awareBonus_m;
    }
    r = std::min(r + bonus, k.reachCap_m);
    if (r + bonus < std::fabs(x.P.x - x.B.x)) return kMaskReach;
  }
  constexpr float kMaxDz_m = 42.0f;  // 0x1425cd830
  if (dz > kMaxDz_m) return kMaskDz;
  // The rank > 1 angle sub-test (raw 0x140607741-0x140607814): a second-line
  // runner inside 20 m of the ball-line point, heading within 45 deg of the
  // bearing to him and with no opponent in the 80 deg cone, is rejected when
  // that point is not ahead in the attack frame - and likewise every runner
  // once four attackers are ahead of the ball band.
  if (!RankAngleOk(w, id, attackLevel)) return kMaskRankAngle;
  // ponytail: 5a8290 / 5a5400 are not reduced; 5a7d10 (the carrier pass-aim
  // stop) is not applied. The generic path goes to 79ac, where with a carrier
  // 6046b0 (style bit 3: none -> true), 609b40 (carrier valid -> false) and
  // 609e60 (receiver action 0x48 -> false) set the bit.
  (void)rank;
  return Valid(w.carrier) || !Valid(x.q) || Dist2(w.p[x.q].pos, x.B) <= 49.0f ? kMaskPass
                                                                           : kMaskGeneric;
}

bool RunStop(const RunWorld& w, int id, int run) {
  const PlayerFacts& me = w.p[id];
  // Selector stop tests' own heads (off-ball 2.2).
  const float minFrames = run == kSecondLine ? kPesFps : kChanceMinFrames;
  if (me.t < minFrames) return false;
  if (me.humanControlled || w.carrier == id) return true;
  if (run == kCounter && CommonStop(w, id, run)) return true;
  if (run == kCounter) return w.s[TeamOf(id)] * me.pos.x > kF8;
  return CommonStop(w, id, run);
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

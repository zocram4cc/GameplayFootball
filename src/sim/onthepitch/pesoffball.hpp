#pragma once

// PES 2021's off-ball attacking runs and the designated supporter
// (docs/re/pes-re-off-ball.md, OffBallRE; pes-re-run-targets.md + its leaf
// companions, RunTargetsRE).
//
// One run manager per team (ActionSelectorManager, FUN_140525530) ticks on the
// team's own parity frame. It first releases runners whose stop test says so,
// then walks the selectors in registration order; a selector takes slots whose
// candidate bit is set and whose current action ranks lower, best score first,
// up to the run's cap (selectorVision.o by attack level) and its per-tick cap.
//
// This file is the decision core: pure functions over plain inputs, so every
// rule is testable without a Match. TeamAIController builds the inputs.
//
// Where the numbers live:
//   - selectorVision.o / spaceRun.o: records of
//     common/match/constant/constant_team.bin, read through PesVfs.
//   - floats with a VA in the reports: the exe's .rdata via PeImage::ReadVa.
//   - fps multiples and the few literals the reports give without a VA are
//     named constants in pesoffball.cpp with their function as provenance.
//
// Coordinates: x along the pitch, z across it (GF's coords[1]); `s` is the
// attacking team's sign (+1 attacks +x). Angles are degrees.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace pes::offball {

inline constexpr const char* kConstantTeamPath = "common/match/constant/constant_team.bin";
inline constexpr int kSlots = 11;
inline constexpr int kAttackLevels = 5;

// Action states (T+0xa2a0 values, string table 0x1434A4D50).
enum ActionState : int {
  kNothing = 0,
  kBasePosition = 1,
  kMoveOnPass = 0x10,
  kSecondLine = 0x1d,
  kChance = 0x1e,
  kCounter = 0x1f,
  kLineBreak = 0x20,
  kDiagonal = 0x21,
  kPassSupport = 0x22,
  kPassAndGoRun = 0x24,
  kOneTwoPassRun = 0x25,
  kCenteringGet = 0x26,
  kOverlap = 0x27,
  kOverlapCB = 0x28,
  kGoalGet = 0x2a,
  kPostPlay = 0x2b,
  kTeammateMoving = 0x2c,
  kPullAway = 0x2f,
  kMark = 0x36,
};

// selectorVision.o rows, in the file's (sorted) key order.
enum CapRun : int {
  kCapDiagonal = 0,
  kCapLineBreak,
  kCapPullAway,
  kCapChance,
  kCapCounter,
  kCapSecondLine,
  kCapRunCount,
};

// Chance's per-tick cap (FUN_140524a30 ctor arg); every other score selector 1.
inline constexpr int kChancePerTick = 11;

// FieldInfo F1, the half-width to the outer line edge (pes-re-fieldinfo.md).
inline constexpr float kFieldHalfWidth_m = 34.06f;

struct OffBallConstants {
  uint8_t offerNum[kCapRunCount][kAttackLevels];  // selectorVision.o offerNum_*
  int chanceScoreLimit;                           // spaceRun.o chanceScore (low byte)
  float chanceScoreBase;    // 100.0 @0x14253ee30 (Chance score 0x140606575)
  float laneAhead_m;        // 0.2 @0x142598578 (X past the runner)
  float blockConeFlat;      // 0.6 @0x14257a190 (blocked-lane radius)
  float blockConeSlope;     // 0.4 @0x14259b89c
  float blockRadius_m;      // 7 @0x14259bdf0
  int laneWindow;           // byte @0x143449d88 (lanes either side of the runner's)
  float reachDhFrom_m;      // 12 @0x14259be04 (Chance reach, 0x140606cd0)
  float reachDhTo_m;        // 30 @0x142591140
  float reachDhDiv_m;       // -18 @0x1425c7e70
  float reachBase_m;        // 15 @0x14259be0c
  float reachDhGain_m;      // 5 @0x14259222c
  float reachCap_m;         // 37 @0x1425cd82c
  float awareFrom;          // 75 @0x1425aa1bc
  float awareTo;            // 95 @0x1425c594c
  float awareSpan;          // 20 @0x14259be1c
  float awareBonus_m;       // 18 @0x14259be18
  float gridCellsX;         // 104 @0x1425cb308 (occupancy grid, FUN_140a783d0)
  float gridCellsZ;         // 68 @0x1425cf878
  float gridRadiusSq_m2;    // 16 @0x14259be10 (FUN_140a77130)
  float gridFalloff_m;      // 4 @0x1425656c0
  float gridScale;          // 0.25 @0x142592228
  float fallbackSide_m;     // 10 @0x142588210 (FUN_1406067c0: line, 10 m to the runner's side)
  // FUN_1405997f0, the defenders' prediction length: by the runner's squared
  // distance to the ball, then a fixed lead and the off-ball lead.
  float predNearSq_m2;      // 196 @0x1425c4c18
  float predMidSq_m2;       // 676 @0x1425cb48c
  float predNear_s;         // 0.28 @0x1425cb46c
  float predMid_s;          // 0.4 @0x14259b89c
  float predFar_s;          // 0.6 @0x14257a190
  float predLead_s;         // 0.1 @0x142588200
  float predOffBall_s;      // 0.3 @0x142590d98
};

std::optional<OffBallConstants> LoadOffBallConstants(const PeImage& exe, const PesVfs& vfs,
                                                     std::string* error);

// FUN_140a40050 rank; a selector may take a slot only if its rank is lower
// (FUN_140a40000).
int ActionRank(int state);
bool MayTake(int run, int current);

// cap(id) for attack level L (FUN_140600e40): Tiki-Taka (instruction 6) cuts
// SecondLine and PullAway to 0 and Chance to 1.
int RunCap(const OffBallConstants& k, CapRun run, int attackLevel, bool tikiTaka);

// The shared select 0x140602060: the slots to take, in take order. `running` is
// how many slots already hold this run.
std::vector<int> SelectRunners(const std::array<float, kSlots>& scores, int running, int cap,
                               int perTickCap, bool chance);

// Chance score 0x140606060: base + s·x of the runner.
float ChanceScore(const OffBallConstants& k, float s, float runnerX);

// The Chance mask's longitudinal reach (0x140606cd0): a candidate only if
// |x_runner - x_ball| <= reach. `dz` = |z_runner - z_ball|, `awareness` 0..99.
float ChanceReach_m(const OffBallConstants& k, float dz_m, float awareness);

struct Vec2f {
  float x = 0.0f, z = 0.0f;
};

struct LaneSearchInput {
  float s = 1.0f;
  Vec2f runner, runnerPred;  // now and 16 frames ahead
  Vec2f runnerVel;           // m/s
  Vec2f ball;
  float defLineX = 0.0f;               // the defending team's line (S-table row)
  std::vector<Vec2f> opponents;        // active defending players
  std::vector<Vec2f> deepest;          // the two deepest outfield defenders, predicted
  float teamDir_deg = 0.0f;            // the attack direction's bearing
  bool hasReceiver = false;
  Vec2f receiver;
  int limit = 0;
};

// FUN_1405997f0 for an outfield defender in no special action (not the
// carrier, not controlled, not in action 4/5/7): PES frames the deepest
// defenders are predicted ahead, from the runner's squared distance to the ball.
int DefenderPredictFrames(const OffBallConstants& k, float runnerBallDist2_m2);

// FUN_14059ba60 as reached from the Chance target builder: the line X at one of
// the 33 lanes 2 m apart, within the window around the runner's lane.
std::optional<Vec2f> ChanceLaneTarget(const OffBallConstants& k, const LaneSearchInput& in);

struct StopInput {
  int run = kChance;
  float t_frames = 0.0f;  // PES frames in the run
  bool humanControlled = false;
  bool becameCarrier = false;  // carrier, PASS_GET or the pass's target
  bool openPlay = true;
  bool teamInPossession = true;
  float s = 1.0f;
  Vec2f runner, ball, target;
  float oppLineX = 0.0f;  // the defending team's line
};

// The selector's stop test (vt+0x18): true releases the runner.
bool RunStop(const StopInput& in);

// FUN_1405eb980: 0.7·|Δangle| + distance + 15 when closer than 10 m.
float SupporterCost(float angleDiff_deg, float distance_m);

struct SupportDistanceInput {
  bool inPlay = true;
  int supportRange = 5;  // 1..10
  bool buildUpLong = false;
  float ballX = 0.0f, ballZ = 0.0f;       // attack frame (s·x)
  float ownLineX = 0.0f, oppLineX = 0.0f;  // attack frame
  float bearingFromAttack_deg = 0.0f;      // supporter bearing vs attack direction
};

// FUN_1405e0c60 for the designated supporter (pes-re-tactics.md 4.1).
float SupportDistance_m(const SupportDistanceInput& in);

}  // namespace pes::offball

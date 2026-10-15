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
// PES's tick rate, FUN_141e532d0 = [0x14351ae54]; fps multiples and frame
// counts in this header's reports are expressed against it.
inline constexpr float kPesFps = 54.0f;

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

// One player as the run code reads him. Angles are PES bearings
// (atan2(dx, dz) in degrees, [0, 360): 90 = +x).
struct PlayerFacts {
  bool active = false;
  bool goalie = false;
  bool humanControlled = false;
  Vec2f pos, vel;        // m, m/s
  float facing = 0.0f;   // +0x554, body facing
  int state = kNothing;  // T+0xa2a0
  float t = 0.0f;        // PES frames in the state (ring T+0xa2cc)
  int roleClass = 0;     // FUN_1408c51f0: GK0 CB1 SB2 DMF3 CMF4 SMF5 AMF6 SS7 WF8 CF9
  Vec2f aim;             // FUN_140596c30 / a76240: the run target, else the position
  Vec2f slotPoint;       // T+0x4b4c + 12·slot: the slot's formation point
  int marker = -1;       // FUN_140a62850: the opponent marking him
  int aiClass = 0;       // FUN_140a580a0 (T+0xc0), the AI class the state tests read
  int position = 0;      // T+0x4c00: registered position, 0 GK 1 CB 2 LB 3 RB 4 DMF 5 CMF
                         // 6 LMF 7 RMF 8 AMF 9 LWF 10 RWF 11 SS 12 CF
};

// The last ball event a3e190(ctx+0xe0, 0). Kinds as PES: 1 pass, 2 through
// pass, 3 lofted, 0x10 shot.
struct BallEvent {
  bool valid = false;
  int kind = 0;
  float age = 1e9f;  // PES frames since the kick
  Vec2f pos;         // where it was kicked
  int target = -1;   // the intended receiver (global id)
};

// Everything the run selectors and stop tests read, global ids team·11 + slot.
struct RunWorld {
  std::array<PlayerFacts, 2 * kSlots> p;
  int carrier = -1;                      // FUN_140a627e0(ctx, 0): ball owner
  std::array<int, 2> receiver{-1, -1};   // FUN_1405969b0 per team
  Vec2f ball;
  std::array<float, 2> s{1.0f, -1.0f};   // T+0x254
  std::array<float, 2> lineX{};          // MA+0x44 + 0x14·t: team t's line
  bool openPlay = true;
  std::array<bool, 2> possession{};      // T+0x255
  std::array<float, 2> possessionAge{};  // MI+0x11a0, PES frames
  BallEvent ev;
};

// T+0x4c00 registered positions from GF's e_PlayerRole order.
int Position(int gfRole);

// FUN_140500200: T+0xb8b8, the slots of `team` the Chance mask may take, by
// attack level (decompiled for this port).
uint32_t AttackSlotMask(const RunWorld& w, int team, int attackLevel);

// FUN_14068b1a0 (raw 0x14068b1a0): the crowded-mate stop. An SB (AI class 2)
// runner within 20 m of the ball and no more than 7 m behind the opponents'
// line is released when another runner (not a class-1 Chance mate) is within
// 12 m of him.
bool CrowdedMateStop(const RunWorld& w, int id);

// FUN_140654e60 with FUN_140655660 and FUN_140a75740 (raw walks): the number of
// frames for player `id` to reach `X`, on PES's speed tiers (100/(fps·{12.9,
// 22.5, 32.7, 72}) m/frame) with the heading-dependent accel step.
int FramesToReach(const RunWorld& w, int id, Vec2f X);

// FUN_14068aeb0 (raw 0x14068aeb0-0x14068b06e, resolved with FUN_140654e60's
// two angle arguments): the pass-to-me stop, true when the last ball event was
// a kick whose recorded target is not this runner, an opponent is within 5 m,
// the runner sits 1.2-4 m behind the opponent line, and he reaches his own
// route point inside the receiver's lead (the receiver cannot beat him to it).
bool PassToMeStop(const RunWorld& w, int id);

// FUN_140688c00 step 19's leaf FUN_1405a1cc0 (raw 0x1405a1cc0, needs
// FUN_140654e60's angle args): a duel-interception keep is rejected when the
// carrier is far from the runner's aim and no teammate reaches the crossing
// point sooner.
bool DuelInterceptionKeep(const RunWorld& w, int id);

// FUN_140691300 (raw 0x140691300): the run sub-kind 2 stop.
bool SubKindTwoStop(const RunWorld& w, int id);

// FUN_140606cd0's rank > 1 angle sub-test (raw 0x140607741-0x140607814): with
// the runner on the 2nd or later line of the attacking side, a forward (or a
// back with the 0x11 style bit) whose route leaves him within 80 deg of the
// attack direction and 20 m of the ball-line point is only a candidate when
// the ball-line point itself is past the halfway line (a4c720 with a = 0 and
// k = 0; a = 15 and k = -30 with instruction 2, which GF does not carry).
bool RankAngleOk(const RunWorld& w, int id, int attackLevel);

// FUN_140606cd0's generic-lane path for slot `id` (decompiled for this port):
// true sets his candidate bit. `awareness` is the reach byte (0..99).
bool ChanceMaskBit(const OffBallConstants& k, const RunWorld& w, int id, uint32_t slotMask,
                   int attackLevel, bool buildUpLong, float awareness);

// FUN_1408c51f0's role classes (pes-re-stamina-subs.md 1): GK0 CB1 LB/RB2
// DMF3 CMF4 LMF/RMF5 AMF6 SS7 LWF/RWF8 CF9, from GF's e_PlayerRole order
// (GK CB LB RB DM CM LM RM AM CF LW RW SS).
int RoleClass(int gfRole);

// The selector's stop test (vt+0x18) for runner `id` in run `run`: true
// releases him. Chance 0x140607a60 / Counter 0x14060b410 / SecondLine
// 0x1406118e0 / Diagonal 0x140615090, each over FUN_140688c00.
bool RunStop(const RunWorld& w, int id, int run);

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

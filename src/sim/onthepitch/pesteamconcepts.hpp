// PES 2021's team sheet and its team concepts (docs/re/pes-re-tactics.md 1-2,
// pes-re-transitions.md 1-3, pes-re-team-ai-leftovers.md 2.3). The sheet holds
// the numeric sliders and the seven binary toggles; the toggles gate the
// `match::ai::SubConcept*` team patterns the concept selector FUN_140579840
// starts and stops every team tick. Ported here: the selector's own rules and
// the two transition concepts GF can express (TransitionChase, the counter-press
// trapper; SideCounter, the counter-attack). Everything is deterministic: the
// report finds no RNG in the selector or in these concepts.
//
// Coordinates: GF x along the pitch, GF y lateral (PES z). `dir` is the
// team's attack sign in x (+1 attacks +x). Headings follow PES: degrees,
// 0 = +y, 90 = +x (direction (sin h, cos h)), as FUN_140a92d40/FUN_140a8f7c0.

#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "base/math/vector3.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace pes {

using blunted::Vector3;

// The seven binary toggles, sheet bytes S+0x13+7*plan+k (tactics 2).
enum SheetToggle {
  kAttackingStyle = 0,  // 0 Counter Attack, 1 Possession Game
  kBuildUp,             // 0 Long Pass, 1 Short Pass
  kAttackingArea,       // 0 Wide, 1 Centre
  kPositioning,         // 0 Maintain Formation, 1 Flexible
  kDefensiveStyle,      // 0 Frontline Pressure, 1 All-out Defence
  kContainment,         // 0 Middle, 1 Wide
  kPressuring,          // 0 Aggressive, 1 Conservative
  kSheetToggleCount,
};

// The sheet values the AI reads (tactics 1, 4): Support Range, Defensive Line
// and Compactness run 1..10; the two sliders with no AI effect are not kept.
struct TeamSheet {
  int supportRange = 0;
  int defensiveLine = 0;
  int compactness = 0;
  std::array<int, kSheetToggleCount> toggles{};
};

inline constexpr int kConceptCount = 48;          // FUN_140579e10's ids 0..47
inline constexpr int kLastAttackingConcept = 0x20;  // ids <= 0x20 run in possession
inline constexpr int kConceptSideCounter = 0x03;
inline constexpr int kConceptForeCheckPress = 0x21;
inline constexpr int kConceptForeCheckLineDown = 0x22;
inline constexpr int kConceptTransitionChase = 0x24;
inline constexpr int kConceptRetreatBlockCreate = 0x25;

struct TeamConceptConstants {
  // Sheet ctor FUN_1408c5390 defaults; the toggles default to 0 (xor esi,esi).
  int defaultSupportRange = 0, defaultDefensiveLine = 0, defaultCompactness = 0;
  // team/subConcept.json (constant_team.bin subConcept.o): an id <= 0 is skipped.
  std::array<bool, kConceptCount> enabled{};
  float endTestAfter_s = 0;  // 0.1 (0x142588200): slot 3 runs once a concept is older

  // TransitionChase (transitions 2.1; start FUN_140575510, execute FUN_140575b60)
  float chaseWindow_s = 0;           // 3 (lea ecx,[rax+rax*2] x fps)
  float chaseThirdMargin_m = 0;      // 10, carrier own-frame x >= F6 - this
  float chaseWideMargin_m = 0;       // 2, |carrier y| >= F7 - this
  float chaseRangeSq_m2 = 0;         // 100, chaser within
  float chaseGoalSideMax_deg = 0;    // 100 (same .rdata 100.0)
  float chaseInsideMin_deg = 0;      // 90
  float chaseSecondRangeSq_m2 = 0;   // 400, second-man list walk stops past it
  float chaseSecondWidthSlack_m = 0; // 15
  float chaseTargetDistance_m = 0;   // 8
  float chaseTargetTurn_deg = 0;     // 160

  // SideCounter (transitions 3.1; start FUN_140540780, end FUN_1405404a0,
  // supporters FUN_140541010, runner target FUN_14053f510)
  float counterWindow_s = 0;         // 3
  float counterKickoffGuard_s = 0;   // 6
  float counterFacingMax_deg = 0;    // 55
  float counterBallMaxDepth_m = 0;   // -12 (own frame)
  float counterLineMargin_m = 0;     // -10, opponents counted goal-side
  float counterAbortLineMargin_m = 0;  // -14
  float counterAbortWideMargin_m = 0;  // 4
  int counterMaxGoalSide = 0;        // 2 (cmp al,2; ja fail)
  float runnerUnmarkedSq_m2 = 0;     // 42.25
  float runnerChannelShare = 0;      // 50 / 100 of the box-edge-to-touchline gap
  float runnerChannelSlack_m = 0;    // 11
  float runnerCentreFactor = 0;      // 1.2, Attacking Area against the sheet's lane
  float runnerAreaFactor = 0;        // 0.7, Attacking Area with the sheet's lane
  float runnerWideLane_m = 0;        // 17
  float supportAhead_m = 0;          // 15
  float supportSide_m = 0;           // 12
  float supportTurn_deg = 0;         // 22
  float supportMinAhead_m = 0;       // 8
  float supportHoldBonus_m = 0;      // -5
  float counterRunGrace_s = 0;       // 2 (add eax,eax x fps)
  float counterMaxAge_s = 0;         // 6 (lea x3, add x2)
  float counterRunnerFarSq = 0;      // 1369, compared against sqrt(d^2) as written
  float counterRunnerMaxDx_m = 0;    // 30

  // ForeCheckPress (start FUN_1405747f0, continue FUN_140574710, count FUN_140574200)
  float foreCheckWindow_s = 0;       // 5
  float foreCheckHold_s = 0;         // 2: always continues inside it
  float foreCheckBallFloor_m = 0;    // -10 own frame
  float foreCheckCorridor_m = 0;     // 12
  float pressGoalSide_deg = 0;       // 70: "close" mate, and the 100..170 ramp span
  float pressRampFrom_deg = 0;       // 100
  float pressRampTo_deg = 0;         // 170
  float pressRadius_m = 0;           // 8
  float pressRadiusRamp_m = 0;       // 4
  float pressBonusAbove = 0;         // 0.2
  int pressCount = 0;                // 3
  float pressLaneRadius_m = 0;       // 6.5, FUN_140a53da0's radius
  float pressLaneBonus_m = 0;        // 5
  // ForeCheckLineDown (start FUN_140574ea0, end FUN_140574b00) and its helpers
  float lineDownBallDivisor = 0;     // -2.5: F0 / this is the deep end
  float lineDownBallMax_m = 0;       // 27
  float lineDownGap_m = 0;           // 3
  float lineDownLongPassFacing_deg = 0;  // 70
  // RetreatBlockCreate (end FUN_140575db0)
  float retreatGap_m = 0;            // 5
  float retreatBackFront_m = 0;      // 8 (- F6)
  float retreatMidFront_m = 0;       // 18 (- F6)
  // The slot-target band push-up FUN_140687fb0
  float pushUpBehind_m = 0;          // -4 behind the measured back line
  float pushUpBallGap_m = 0;         // 6 behind the ball point
  float pushUpCap_m = 0;             // -5 own frame
  // FUN_140687820: a man counts toward his measured line clamped to +-4 m of it
  float measuredLineClamp_m = 0;     // 4
  // FUN_140574bb0, the uncovered carrier (LineDown trigger)
  float uncoveredLineMargin_m = 0;   // 12: ball beyond our line + this, or wide, needs nobody near
  float uncoveredNearSq_m2 = 0;      // 9
  float uncoveredFacing_deg = 0;     // 90: the holder faces our goal
  float uncoveredRampFrom_m = 0;     // 5
  float uncoveredRampTo_m = 0;       // 20
  float uncoveredRampSpan_m = 0;     // 15
  float uncoveredRadiusRamp_m = 0;   // 3
  float uncoveredRadius_m = 0;       // 4
  float uncoveredCone_deg = 0;       // 80
};

// The defending concepts' inputs. Own frame: +x toward the opponents' goal.
struct DefenceConceptInput {
  bool inPlay = false;
  float sincePossessionChange_s = 0;
  float dir = 1;                // world x -> own frame
  Vector3 ball;                 // world
  bool opponentsHoldBall = false;  // FUN_140a55d40(ball, side, 2)
  bool keeperHolds = false;     // FUN_140a53f90
  bool attackLevelZero = false; // T+0xb8a3
  bool holderLongPass = false;  // the opponent on the ball in LONG_PASS (anime 8)
  float holderHeading_deg = 0;
  bool holderOpponent = false;  // an opponent holds the ball (carrier or MA holder)
  float ownLine = 0;            // MA+0x40+0x14*side: our deepest outfielder, world x
  float teamMid = 0;            // MA+0x3c+0x14*side: midpoint of front man and ownLine
  std::vector<Vector3> mates;   // our outfield men, world
  std::vector<Vector3> allMen;  // all our men on the pitch, keeper included
  int presser = -1;             // the MATCH_UP man, index into mates
  // T+0x89a0 / T+0x89a4 (FUN_140687820): measured back and midfield line, world x.
  float measuredBack = 0, measuredMid = 0;
  // T+0x875c / T+0x8770: back and midfield band, own frame.
  float backBand = 0, midBand = 0;
};

bool ForeCheckPressCount(const TeamConceptConstants& k, const DefenceConceptInput& in);
bool ForeCheckPressStart(const TeamConceptConstants& k, const DefenceConceptInput& in);
bool ForeCheckPressEnd(const TeamConceptConstants& k, const DefenceConceptInput& in);
bool ForeCheckLineDownStart(const TeamConceptConstants& k, const DefenceConceptInput& in);
bool ForeCheckLineDownEnd(const TeamConceptConstants& k, const DefenceConceptInput& in);
bool RetreatBlockCreateEnd(const TeamConceptConstants& k, const DefenceConceptInput& in);
// FUN_140687fb0: how far (>= 0) every band is pushed upfield while neither
// RetreatBlockCreate nor ForeCheckLineDown runs. Own frame.
float BandPushUp_m(const TeamConceptConstants& k, float measuredBackOwn, float ballOwn, float backBand);

std::optional<TeamConceptConstants> LoadTeamConceptConstants(const PeImage& exe, const PesVfs& vfs,
                                                             std::string* error);

TeamSheet DefaultTeamSheet(const TeamConceptConstants& k);

// Slot 1 of the ported concepts (tactics 2): the sheet gate.
bool ConceptGate(int id, const TeamSheet& sheet);

// --- TransitionChase -------------------------------------------------------
struct ChaseMan {
  Vector3 pos;
  bool falling = false;
  // In BASE_POSITION / MARK / PASS_COURSE_CUT: not engaging, not on an order.
  bool available = false;
};

struct TransitionChaseInput {
  bool inPlay = false;
  float sincePossessionChange_s = 0;
  bool carrierKicking = false;
  Vector3 carrier;
  float dir = 1;  // the chasing team's attack sign
  int chaser = -1;  // index into men: the engaging defender, -1 none
  // Own men in order of distance from the carrier, nearest first.
  std::vector<ChaseMan> men;
};

struct ChasePick { int chaser = -1, second = -1; };

std::optional<ChasePick> TransitionChaseStart(const TeamConceptConstants& k, const TransitionChaseInput& in);
Vector3 TransitionChaseTarget(const TeamConceptConstants& k, const Vector3& chaser, const Vector3& carrier,
                              float dir);

// --- SideCounter -----------------------------------------------------------
struct CounterMan {
  Vector3 pos;
  bool wideOrForward = false;   // position category > 4
  bool supportEligible = false; // position category > 2, not in a run/press state
  float nearestOpponentSq_m2 = 0;
};

struct SideCounterInput {
  bool attackAllowed = false;  // T+0xa508 != 0
  float sincePossessionChange_s = 0;
  float sinceKickoff_s = 0;    // in-play time since a kick-off restart; huge when the last restart was not one
  bool keeperHolds = false;
  bool ownKeeperPunting = false;
  int holder = -1;             // index into men
  float holderHeading_deg = 0;
  Vector3 ball;
  float dir = 1;
  float oppLineX = 0;          // the opponents' offside line, world x
  std::vector<Vector3> opponents;  // outfield, on the pitch
  std::vector<CounterMan> men;
  int attackingArea = 0;
};

std::optional<int> SideCounterStart(const TeamConceptConstants& k, const SideCounterInput& in);
Vector3 SideCounterRunTarget(const TeamConceptConstants& k, const Vector3& runner, float oppLineX);
// The three PASS_SUPPORT points and men (-1 none); `previous` holds last pick.
struct CounterSupport {
  std::array<Vector3, 3> points;
  std::array<int, 3> men{-1, -1, -1};
};
CounterSupport SideCounterSupporters(const TeamConceptConstants& k, const SideCounterInput& in, int runner,
                                     const std::array<int, 3>& previous);
// Slot 3: true = stop.
bool SideCounterEnd(const TeamConceptConstants& k, bool inPlay, bool runnerStillRunning, float age_s,
                    const Vector3& ball, const Vector3& runner);

}  // namespace pes

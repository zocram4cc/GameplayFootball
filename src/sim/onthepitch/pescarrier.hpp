#pragma once

// PES 2021's ball-carrier decision layer: match::ai::bp::BallPlayer's think
// units, docs/re/pes-re-carrier-ai.md and docs/re/pes-re-attack-decisions.md 2.
//
// This is the decision core only - the parts the reports mark "implementable
// now": the shoot zone and its "just shoot" rule, the receiver openness
// formula with its class ladder, PassForward's three tiers, and the plan
// selection order with its rolled start probabilities. Every function is pure
// (arguments plus the constants), so the whole layer is testable against
// synthetic constants without an install and without a Match; the caller
// (ElizaController) builds the inputs from its own state.
//
// Angles are degrees throughout, positions are metres. The pitch frame is
// PES's (x = length axis, z = width axis, y up) and the caller maps this
// engine's (x, y, z) onto it: this engine's y is PES's z.
//
// Every number comes from the user's own PES2021.exe through
// pes::PeImage::ReadVa: floats from .rdata, integer bases from the immediates
// of the instructions that use them (each instruction's opcode bytes are
// checked before its immediate is trusted). The few values whose instruction
// the port did not re-read carry `ponytail:` at their field with the report
// section that names them.

#include <optional>
#include <string>
#include <vector>

#include "pes/exe.hpp"
#include "pesrandom.hpp"

namespace pes {

// Two metres on the pitch, PES frame (x along it, z across it).
struct CarrierPoint {
  float x = 0.0f;
  float z = 0.0f;
};

struct CarrierConstants {
  // --- the shoot zone (report 4.1, 0x140699610's init + 0x140698d10) ---
  float shootR0_m = 0.0f;             // 11.0, the box half-disc radius
  float shootR1_m = 0.0f;             // 12.0, the header range
  float nearCentral_m = 0.0f;         // 18.0
  float farCentral_m = 0.0f;          // 24.0
  float nearSide_m = 0.0f;            // 16.0
  float farSide_m = 0.0f;             // 22.0
  float centralHalfAngle_deg = 0.0f;  // 15.0
  float sideAngle_deg = 0.0f;         // 40.0
  float finishingBoost_m = 0.0f;      // 3.0, near range gain per clamp01 ramp
  float longShotBoost_m = 0.0f;       // 3.0, far range gain with the skill bits
  float finishingFloor = 0.0f;        // 70.0, the ramp's low stat
  float finishingTop = 0.0f;          // 99.0, the ramp's high stat
  float finishingSpan = 0.0f;         // 29.0, the ramp's width

  // --- the shoot decision (report 5.1, 0x142121920) ---
  float justShootDist_m = 0.0f;      // 20.0, "just shoot" distance
  float justShootAngle_deg = 0.0f;   // 40.0, "just shoot" angle
  float firstTimeDistSq = 0.0f;      // 400.0 = 20 m^2, no first-time shot beyond
  float firstTimeNearDistSq = 0.0f;  // 225.0 = 15 m^2
  float firstTimeIncoming_deg = 0.0f;  // 120.0, incoming ball vs facing
  float firstTimeBody_deg = 0.0f;      // 70.0, body angle at 15 m
  float firstTimePathGate_m = 0.0f;    // 7.0, the path test's distance
  float aboveHeadFactor = 0.0f;        // 0.8, ball above 0.8 * height/100
  float statDivisor = 0.0f;            // 100.0, height/100

  // --- the receiver records (report 4.3, 0x1421174f0 / 0x142118700) ---
  float movingBallFloor_kmh = 0.0f;  // 10.0, difficulty's moving-ball ramp
  float movingBallSpan_kmh = 0.0f;   // 50.0
  float kmhPerMs = 0.0f;             // 3.6
  float distFloor_m = 0.0f;          // 10.0, difficulty's distance ramp
  float distSpan_m = 0.0f;           // 20.0
  float ballAngleHigh_deg = 0.0f;    // 150.0, the incoming-ball angle ramp
  float ballAngleSpan_deg = 0.0f;    // 60.0
  float oneTouchGain = 0.0f;         // 0.5, skill 12 halves the moving-ball term
  float runFloor_kmh = 0.0f;         // 16.0, the running term
  float runSpan_kmh = 0.0f;          // 12.0
  float turnFloor_deg = 0.0f;        // 60.0, the turn term
  float turnSpan_deg = 0.0f;         // 70.0
  float turnTop_deg = 0.0f;          // 130.0
  float highFloor_m = 0.0f;          // 0.10869, the ball radius
  float highSpan_m = 0.0f;           // 0.89131, to 1 m
  float diffMovingBallWeight = 0.0f;  // 2.0
  float diffRunWeight = 0.0f;         // 1.5
  float diffTurnWeight = 0.0f;        // 1.5
  float lowPassFloor = 0.0f;          // 80.0, g(LowPass)'s low stat
  float lowPassTop = 0.0f;            // 99.0
  float lowPassSpan = 0.0f;           // 19.0
  float lowPassBase = 0.0f;           // 0.7
  float lowPassGain = 0.0f;           // 0.3
  float forwardAngle_deg = 0.0f;      // 60.0, the direction class
  float squareAngle_deg = 0.0f;       // 110.0

  // --- the openness / interception formula (report 4.3) ---
  float openNear_m = 0.0f;           // 1.0, clamp01((d-1)/29)
  float openSpan_m = 0.0f;           // 29.0
  float openInFront_deg = 0.0f;      // 90.0, opponents in front only
  float openBlockMargin_m = 0.0f;    // 1.5, body-block corridor half width
  float openLaneMargin_m = 0.0f;     // 1.0, opponents inside the lane
  float openAlongMin_m = 0.0f;       // 0.2
  float openAlongGain = 0.0f;        // 0.6
  float openAlongPenalty = 0.0f;     // 0.3
  float openMarginScale = 0.0f;      // 100.0
  float openKLowAngle_deg = 0.0f;    // 45.0, the behind-the-ball k factor
  float openKHighAngle_deg = 0.0f;   // 90.0
  float openClass1 = 0.0f;           // 10.0
  float openClass2 = 0.0f;           // 40.0
  float openClass3 = 0.0f;           // 60.0
  float openClass4 = 0.0f;           // 100.0
  float openClass5 = 0.0f;           // 130.0

  // --- PassForward's short forward (report 5.2, 0x142125c80) ---
  float fwdPlanAngleGate_deg = 0.0f;  // 60.0
  float fwdTeamFlagGate = 0.0f;       // 36.0
  float fwdCloseRange_deg = 0.0f;     // 120.0
  float fwdShortPassDistSq = 0.0f;  // 900.0 = 30 m^2, a short pass needs an open lane
  float fwdGoalCentreDist_m = 0.0f;   // 30.0
  float fwdLaneWidthGain = 0.0f;      // 0.25
  float fwdLaneWidthMin_m = 0.0f;     // 1.5
  float fwdLaneWidthMax_m = 0.0f;     // 6.0
  float fwdLaneBudget = 0.0f;         // 220.0 frames
  float fwdLineValueSq = 0.0f;        // 1225.0 = 35 m^2
  float fwdLineValueFarSq = 0.0f;     // 1600.0 = 40 m^2
  float fwdNearestOppFar_m = 0.0f;    // 8.0
  float fwdNearestOppNear_m = 0.0f;   // 5.0
  float fwdNearestOppMin_m = 0.0f;    // 1.5
  float fwdTargetAdvance_m = 0.0f;    // 35.0
  float fwdOppNearDist_m = 0.0f;      // 7.0
  float fwdOppNearFreeDist_m = 0.0f;  // 3.0
  float fwdTurnFacing_deg = 0.0f;      // 80.0
  float fwdFacingBack_deg = 0.0f;      // 135.0
  float fwdMateSpeedCap_kmh = 0.0f;    // 20.0

  // --- PassForward's lofted forward (report 5.2) ---
  float loftMinDist_m = 0.0f;       // 15.0
  float loftMaxDist_m = 0.0f;       // 25.0
  float loftDistBase_m = 0.0f;      // 10.0
  float loftDistGain_m = 0.0f;      // 5.0
  float loftKeeperNearSq = 0.0f;    // 100.0 = 10 m^2
  float loftKeeperFarSq = 0.0f;     // 225.0 = 15 m^2

  // --- the through-ball chooser (attack-decisions 2.6, 0x142127ff0) ---
  float throughMinDist_m = 0.0f;     // 5.0
  float throughCap_m = 0.0f;         // 20.0
  float throughOpenCap_m = 0.0f;     // 15.0
  float throughOpennessOpen = 0.0f;  // 100.0
  float throughClassOpen = 0.0f;     // 4
  float throughMateSpeedCap_kmh = 0.0f;  // 60.0
  float throughFacing_deg = 0.0f;         // 135.0

  // --- the plan roll (report 4.4) ---
  float breakthroughSeedK = 0.0f;  // 1100.0
  float middleShotSeedK = 0.0f;    // 1200.0
  float curveShotSeedK = 0.0f;     // 1300.0
  float crossSeedK = 0.0f;         // 1500.0
  float longSeedK = 0.0f;          // 1600.0
  int planRollModulus = 100;       // `mov edx, 0x64`
  int planRollSlot = 2;            // `mov edx, 2`
  // The stat term every rolled plan adds: N * clamp01((x - 70)/29).
  float statFloor = 0.0f;     // 70.0, 0x14211a9bb
  float statSpan = 0.0f;      // 29.0, 0x14211acc4
  float breakthroughBase = 0.0f;   // 70, 0x14211a956 `mov edx, 0x46`
  float breakthroughComBase = 0.0f;  // 20, 0x14211a991 `mov ebx, 0x14`
  float breakthroughStatGain = 0.0f;  // 15, 0x14211a9db
  float breakthroughCap = 0.0f;       // 70, `cmp ebx, 0x46`
  float middleShotBase = 0.0f;       // 70, 0x14211b628
  float middleShotStateGain = 0.0f;  // 40, 0x14211bcf9
  float middleShotStatGain = 0.0f;   // 15, 0x14259be0c
  float middleShotFloor = 0.0f;      // 5, 0x14259be7c's -5 max
  float middleShotCap = 0.0f;        // 80, 0x14211b716
  float crossBase = 0.0f;            // 50, 0x14211ac86
  float crossStatGain = 0.0f;        // 20, 0x14259be2c
  float crossFloor = 0.0f;           // 40, 0x14211ad5e
  float crossCap = 0.0f;             // 80, 0x14211ad57
  float longBasePressed = 0.0f;      // 30, 0x14211b289
  float longBaseMid = 0.0f;          // 60, 0x14211b2c5
  float longBaseFree = 0.0f;         // 80, 0x14211b2a9
  float longStatGain = 0.0f;         // 10, report 4.4
  // --- the dribble direction search (report 1; addresses in the loader) ---
  float dribbleEdgeCost = 0.0f;       // 100, 0x14253ee30: a band cell's cost
  float dribbleEdgeMargin_m = 0.0f;   // 0.5, 0x142565600: the touchline band
  float dribblePrefGain = 0.0f;       // 0.6, 0x14257a190: preference scale
  float dribbleRange_m = 0.0f;        // 10, 0x142588210: the search radius and falloff
  float dribbleStep_m = 0.0f;         // 2/3, 0x14258d10c: radial step, and the push-out
  float dribbleDirections = 0.0f;     // 72, 0x1425cb47c: headings in the circle
  // --- the threat term (report 1.1) ---
  float threatReachFloor_m = 0.0f;    // 5, 0x14259222c: reach floor, and full-weight distance
  float threatReachScale = 0.0f;      // 2, 0x142565604: reach = max(floor, scale * R_o)
  float threatKeeperScale = 0.0f;     // 2, 0x142565604: a keeper's threat factor
  float threatFullDist_m = 0.0f;      // 5, 0x14259222c: the carrier-opponent distance at w = 0
  float threatFalloff = 0.0f;         // -0.25, 0x1425c48ec: w = (d - 5) * falloff
  float threatPushSq_m2 = 0.0f;       // 0.444444, 0x142bf0a88: squared push-out radius

  bool operator==(const CarrierConstants&) const = default;
};

std::optional<CarrierConstants> LoadCarrierConstants(const PeImage& exe, std::string* error);

// --- the shoot zone (report 4.1) ---
enum ShootBand { kBandClose = 0, kBandMid = 1, kBandFar = 2, kBandOut = 3 };
enum ShootSector { kSectorCentral = 0, kSectorSideLow = 1, kSectorSideHigh = 2, kSectorWide = 3 };

struct ShootZone {
  float d_m = -1.0f;         // distance to the goal line, or to the near post
  float angle_deg = 0.0f;    // bearing to that point against the attack direction
  int band = kBandOut;       // 0 close, 1 mid, 2 far, 3 out
  int sector = kSectorWide;  // 0 central, 1/2 side, 3 wide
  bool inBox = false;        // inside the 180 degree half-disc of radius r0
};

// The zone 0x140698d10 builds. `finishing` is stat 0x1A, `longShotSkill` the
// skill 39 | 33 | 8 mask, `halfLen_m`/`goalHalfWidth_m` the pitch marks PES
// reads from its FieldInfo (this engine's own Law-1 marks).
ShootZone ComputeShootZone(float ballX, float ballZ, float attackSign, float finishing,
                           bool longShotSkill, float halfLen_m, float goalHalfWidth_m,
                           const CarrierConstants& k);

// --- the dribble direction search (report 1) ---

// One opponent as the threat sees him: his position, the point he is moving to
// (equal to pos when static), his R_o reach figure and whether he is a keeper.
struct DribbleOpponent {
  CarrierPoint pos;
  CarrierPoint pred;
  float reachRo_m = 0.0f;
  bool keeper = false;
};

struct DribbleInput {
  CarrierPoint ball;
  float desired_deg = 0.0f;   // the heading he wants, degrees, PES frame (x, z)
  float weight = 0.0f;        // the image-state preference weight W, 0..1
  float halfLen_m = 0.0f;     // pitch half-length along x (the goal axis)
  float halfWid_m = 0.0f;     // pitch half-width along z (the touchlines)
  std::vector<DribbleOpponent> opponents;
};

// The threat term at p for a carrier at `ball` (report 1.1).
float DribbleThreat(CarrierPoint ball, CarrierPoint p, const std::vector<DribbleOpponent>& opponents,
                    const CarrierConstants& k);

// The heading (degrees, [0, 360)) the cheapest 15-step ray costs, out of
// dribbleDirections equal headings (report 1).
float DribbleDirection(const DribbleInput& in, const CarrierConstants& k);

bool InShootRange(const ShootZone& z);   // 0x1406997e0
bool IsCentral(const ShootZone& z);      // 0x140699710
bool IsMid(const ShootZone& z);           // 0x1406997a0
bool IsClose(const ShootZone& z);         // 0x140699820
bool InBox(const ShootZone& z);           // 0x1406996f0
bool InHeaderRange(const ShootZone& z, const CarrierConstants& k);  // 0x140699750

// The header-range gate PES adds to it: the ball must be over the shooter's
// head, which is `ballY > 0.8 * height/100` (0x142121a5e).
bool BallAboveHead(float ballHeight_m, float height_m, const CarrierConstants& k);

// --- the shoot decision (report 5.1) ---
struct ShootInput {
  ShootZone zone;
  float ballHeight_m = 0.0f;
  float height_m = 0.0f;  // stat 0x31
  bool owned = false;     // ball at the carrier's feet
  bool volatileShot = false;  // ctx4+0x6E, a volley/header situation
  bool receiving = false;     // ctx4+0x6F
  int planId = 0;
  float incomingVsFacing_deg = 0.0f;  // incoming ball vs the carrier's facing
  float ballToGoalDistSq = 0.0f;      // ball -> the shot's aim point
  float aimVsFacing_deg = 0.0f;       // the aim point vs the facing
  float bodyPathGate_m = 0.0f;        // 0x140a4dcc0's output
  bool keeperPresent = false;
  bool keeperSettled = false;
};

// The shot sub-kind `a` of the answer record: 0 = no shot, 1 = shoot
// (the "just shoot" rule and the box/header rule, report 5.1). The remaining
// sub-kinds (chip 2, finesse 3, knuckle 4, volley 5) are gated on capability
// rows and skill bits this engine does not carry - ponytail: they stay on the
// plain shot, the action layer picks the animation from the ball's height.
int ShootSubKind(const ShootInput& in, const CarrierConstants& k);

// --- the receiver records (report 4.3) ---
struct ReceiverInput {
  CarrierPoint target;    // the pass target (to feet, or into space)
  float openness = 150.0f;  // the record's openness (150 = the start value)
  float difficulty = 0.0f;
  int opennessClass = 0;
  int dirClass = 0;
  int turnClass = 0;
  float ballToTarget_m = 0.0f;
  float passAngle_deg = 0.0f;  // ball -> target
  float fwdAngle_deg = 0.0f;   // the pass against the attack direction
  float turnAngle_deg = 0.0f;  // the pass against the carrier's facing
};

// The direction/turn classes of report 4.3.
int DirectionClass(float fwdAngle_deg, const CarrierConstants& k);
int TurnClass(float turnAngle_deg, const CarrierConstants& k);

// The difficulty term (+0xF0), report 4.3.
float ReceiverDifficulty(float ballSpeed_mps, float dist_m, float incomingAngle_deg,
                         bool oneTouchPass, float carrierSpeed_kmh, bool running,
                         float turnAngle_deg, float ballHeight_m, float lowPass,
                         const CarrierConstants& k);

// The openness ladder: margin -> class, 0x142117b81.
int OpennessClassFromMargin(float margin, const CarrierConstants& k);

// One opponent's contribution to a lane's openness (0x142118700's scored
// tail): the perpendicular distance to the closest point of the lane against
// the time the passing opponent has to reach it.
float LaneOpennessMargin(float laneX, float laneZ, float along, float perp, float kFactor,
                         float difficulty, const CarrierConstants& k);

// --- PassForward (report 5.2) ---
enum PassKind { kPassShort = 0, kPassLong = 1, kPassCentering = 2, kPassThrough = 3 };

// One opponent the openness formula prices a lane with: his current point,
// his predicted point (position + velocity * the frames PES looks ahead) and
// the bearing of his predicted point against the pass direction.
struct LaneOpponent {
  CarrierPoint current;
  CarrierPoint predicted;
  bool sliding = false;  // action 0x33/0x34, the wider margin
};

struct PassForwardInput {
  bool owned = false;
  int carrierPressureClass = 3;  // 0 pressed .. 3 free
  bool close = false;            // the carrier is in the close shooting band
  bool danger = false;           // imageA+0x31C1
  float freeSpace = 0.0f;        // the carrier's team-table "free space" value
  int imageState = 0;            // imageA+0x32B0+8
  float planAngle_deg = 0.0f;    // plan.+0xC0
  float attackAngle_deg = 0.0f;  // team+0x250
  float attackSign = 1.0f;
  float halfLen_m = 52.5f;
  float goalHalfWidth_m = 3.7f;
  float goalAreaHalfWidth_m = 9.16f;
  float planWideSide = 0;  // plan+0xC4 == 3: the same-touchline-side condition
  // 0x140a4c8e0's answer for the target mate: whether he can be reached on the
  // ground at all. ponytail: the helper itself is not decoded; the caller sets
  // it from its own lane test.
  bool mateLaneOk = false;
};

// A candidate receiver with the lanes PES scores it on.
struct PassCandidate {
  int index = -1;
  ReceiverInput record;
  float pressureDistOpp0_m = 0.0f;  // the pressure entry's distance to opponent 0
  int pressureClass = 3;
  float nearestOppToMate_m = 100.0f;  // the mate's nearest opponent
  bool mateFree = false;              // 0x14063e010 && 0x14063e270
  float mateSpeed_kmh = 0.0f;
};

struct PassAnswer {
  int index = -1;   // the mate, -1 = no answer
  int kind = kPassShort;
  bool oneTouch = false;
};

// PassForward, kind 4 (0x14212a700): its three tiers in order - short forward,
// then through when the carrier has free space, then the lofted forward.
PassAnswer PassForward(const std::vector<PassCandidate>& mates,
                       const std::vector<LaneOpponent>& opponents, const PassForwardInput& in,
                       const CarrierConstants& k);

// --- the plan (report 4.4) ---
enum CarrierPlan {
  kPlanBasic = 0,
  kPlanBreakthrough = 1,
  kPlanMiddleShot = 2,
  kPlanCurveShot = 3,
  kPlanPassThrough = 4,
  kPlanCross = 5,
  kPlanLong = 6,
  kPlanSideChange = 7,
  kPlanShotPassFromGoal = 8,
  kPlanBackDFLine = 9
};

// The priority order the plan chooser walks (0x142bf02c0): 9, 8, 2, 3, 1, 4,
// 5, 6, else Basic.
constexpr int kPlanPriorityOrder[] = {9, 8, 2, 3, 1, 4, 5, 6};

// One plan's rolled start probability, the P table of report 4.4. Returns a
// negative value when the plan cannot start from this state. `stat` is the
// carrier's own stat the plan reads (Breakthrough: Dribbling/Speed/Contact;
// MiddleShot: Kicking Power/Finishing; Cross and Long the same pair),
// `skill`/`com` the plan's skill and COM triggers and `state2` MiddleShot's
// image-state-2 bonus.
float PlanStartProbability(int planId, const CarrierConstants& k, float stat, bool skill, bool com,
                           bool state2);

// The roll itself: PES builds a throw-away generator, seeds slot 2 with
// int(carrierClock * K) and draws one RangeRaw(slot 2, 100)
// (0x14211a89..0x14211aac2). Returns the drawn 0..99.
int PlanRoll(int carrierClock, float seedK, const RngConstants& rng, const CarrierConstants& k);

// The think cadence (report 2): PES's re-think gate, `frame - busyStamp > 5`,
// converted to this engine's 10 ms tick the way pesballphysics.hpp converts
// the ball's per-tick factors - one GF tick is 0.54 of a PES tick, so a PES
// frame count becomes GF ticks by dividing by that ratio.
inline constexpr float kGfTicksPerPesTick = 0.01f / (1.0f / 54.0f);

inline int PesFramesToGfTicks(int pesFrames) {
  return static_cast<int>(static_cast<float>(pesFrames) / kGfTicksPerPesTick + 0.5f);
}

// Whether a think may run this tick: the carrier has been free of a committed
// action for longer than the gate.
inline bool RethinkDue(int nowFrame, int busyStampFrame, int gateFrames = 5) {
  return nowFrame - busyStampFrame > gateFrames;
}

}  // namespace pes

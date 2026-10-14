#pragma once

// PES 2021's goalkeeper: the deflect point, the post-catch walk target, the
// open-play base position and the loose-ball rush:
// `ActionKeeperDeflect::exec` (0x1409d75d0 -> 0x1440f35f0),
// `ActionKeeperAfterCatchMove::exec` (0x1409e8880),
// `ActionKeeperBasePosition::exec` (0x1409dc040) with its core `0x1409e0390`,
// `0x1409df7d0` and the rush decider `FUN_1421162c0`, all read from
// docs/re/pes-re-keeper-port.md and docs/re/pes-re-goalkeeper.md section 10.
//
// Every number comes from the user's own PES2021.exe through
// pes::PeImage::ReadVa; nothing is transcribed here (see the port report's
// section 5 table for the VA of each value).

#include <optional>
#include <string>

#include "pes/exe.hpp"

namespace pes {

struct KeeperConstants {
  // The deflect point: a fixed offset from the keeper. FUN_1440f35f0's two
  // branches, both metres.
  float deflectSidewaysRadius_m = 1.0f;   // heading+90 branch, 1 [0x14256ae1c]
  float deflectRequestedRadius_m = 2.0f;  // requested-angle branch, 2 [0x142565604]
  // 90 degrees, shared by the heading+90 offset and the step's
  // `sin(θ + 90)` lateral term (0x14259be5c).
  float quarterTurn_deg = 90.0f;
  // The requested angle is used when it is within this many degrees of his
  // heading; beyond it he deflections the far side (heading ∓ 60).
  float deflectSidewaysGate_deg = 120.0f;      // 120 [0x14256ae30]
  float deflectFarSideAngle_deg = 60.0f;       // heading ∓ 60 [0x14259a764]
  // p3+0xe68 <= 0: the point is lifted this high (2.4 [0x1425c7660]).
  float deflectRaisedHeight_m = 2.4f;

  // AfterCatchMove::exec's carry: the ball is held at least this far up-field
  // (4 [0x1425656c0]) and at least 1 m off his own line, then stepped this far
  // toward the chosen teammate.
  float distributionCarryUpfield_m = 4.0f;      // 4 [0x1425656c0]
  float distributionOwnLineMargin_m = 1.0f;     // 1 [0x14256ae1c]
  float distributionTargetStep_m = 4.0f;        // 4 [0x1425656c0]
  float distributionLateralCorrection_m = 2.0f;  // 2 [0x142565604] (toward the middle)
  float distributionBoxSideMargin_m = 1.0f;     // 1 [0x14256ae1c] (|z| <= F7 - 1)
  float angleWrap_deg = 360.0f;                 // 360 [0x14259858c]
  float degreesPerRadian = 57.2957764f;         // 57.2957764 [0x14259be40]

  // --- Open-play positioning (docs/re/pes-re-goalkeeper.md 10.2) ---
  // The 2 m floor when the ball is straight in front, and the goal-area half
  // width that replaces it when the ball is far off the bearing.
  float positioningFloor_m = 2.0f;             // 2 [0x142565604]
  float positioningFloorGoalBias_m = 0.5f;     // 0.5 [0x142565600]
  float positioningBearingSpan_deg = 50.0f;    // 50 [0x14259be34]
  // The defensive-line term: 25 m behind the midpoint of the team line and the
  // line for his side, never deeper than the goal-area line.
  float positioningLineBack_m = 25.0f;        // 25 [0x14259be20]
  float positioningLineClamp_m = 10.0f;       // 10 [0x142588210]
  // The style caps: how far out of his goal he stands (bit 20 / none / bit 19).
  float positioningDefensiveCap_m = 4.0f;      // 4 [0x1425656c0]
  float positioningNoStyleCap_m = 15.0f;       // 15 [0x14259be0c]
  float positioningOffensiveCap_m = 20.0f;     // 20 [0x14259be1c]
  // How much closer he comes as the ball closes on him end to end: 4 m while
  // the ball is within 40 m of him lengthwise, 13 m at 70 m.
  float positioningRampNear_m = 40.0f;        // 40 [0x14259be2c]
  float positioningRampFar_m = 70.0f;         // 70 [0x14259be4c]
  float positioningRampSpan_m = 30.0f;        // 30 [0x142591140] (40 -> 70)
  float positioningRampSpanDepth_m = 9.0f;    // 9 [0x14259d60c] (4 -> 13)
  // Never farther than this from R.
  float positioningMaxRadius_m = 15.0f;        // 15 [0x14259be0c]
  // Never nearer than this to his own goal line.
  float positioningGoalLineMargin_m = 0.8f;   // 0.8 [0x142572348]
  // The lateral shuffle: a Range0(6) draw, 0.1 m a step, scaled down by GK
  // Awareness, and flipped when the predicted ball is 90 degrees off the axis.
  // The keeper's only draw - his other decisions are deterministic in PES.
  float positioningJitterStep_m = 0.1f;        // 0.1 [0x142588200]
  float positioningAwarenessLow = 40.0f;       // 40 [0x14259be2c]
  float positioningAwarenessSpan = 59.0f;      // 59 [0x1425cb2fc] (40 -> 99)
  float positioningAwarenessHigh = 99.0f;      // 99 [0x1425c48cc]
  // In possession (10.2.3): 6 m deep in his own third, 9 m at the halfway of
  // his half, 13 m at the centre line, 20 m with the ball in the opponents'
  // half, capped 13/15/17 by style and never more than 25 m behind the team
  // line.
  float positioningPossessionDeep_m = 6.0f;        // 6 [0x14259bdec]
  float positioningPossessionRamp1_m = 3.0f;      // 3 [0x14258c264]
  float positioningPossessionMid_m = 9.0f;        // 9 [0x14259d60c]
  float positioningPossessionRamp2_m = 4.0f;      // 4 [0x1425656c0]
  float positioningPossessionFar_m = 13.0f;       // 13 [0x1425c5948]
  float positioningPossessionRamp3_m = 7.0f;      // 7 [0x14259bdf0]
  float positioningPossessionLineMargin_m = 3.0f;  // 3 [0x14258c264]
  float positioningPossessionUpfield_m = 3.0f;    // 3 [0x14258c264] (dir*dx >= 3)
  float positioningPossessionDefensiveCap_m = 13.0f;  // 13 [0x1425c5948]
  float positioningPossessionNoStyleCap_m = 15.0f;    // 15 [0x14259be0c]
  float positioningPossessionOffensiveCap_m = 17.0f;  // 17 [0x14259d614]

  // --- The rush (docs/re/pes-re-goalkeeper.md 10.3) ---
  // The race factor: the opponent must need this many times the keeper's frames
  // before he will not go. Offensive GK comes out most readily.
  float rushOffensiveFactor = 1.1f;    // 1.1 [0x1425a3590]
  float rushNoStyleFactor = 1.2f;      // 1.2 [0x14259d5d4]
  float rushDefensiveFactor = 1.3f;    // 1.3 [0x14259bdd8]
  // The hysteresis, in seconds (PES writes it as gs * 0.71 frames, which is
  // 0.71 s at any rate).
  float rushHysteresis_s = 0.71f;      // 0.71 [0x1425eb848]
  // No rush at all once the ball is farther than this.
  float rushMaxBallTime_s = 3.3f;      // 3.3 [0x1425c4c10]
  // How far past his own line he will chase the meeting point: F0 + 3 when
  // already rushing, and 2 more when the newest ball event is his team's.
  float rushXLimitRushing_m = 3.0f;    // 3 [0x14258c264]
  float rushXLimitOwnEvent_m = 2.0f;   // 2 [0x142565604]
};

std::optional<KeeperConstants> LoadKeeperConstants(const PeImage& exe, std::string* error);

// The deflect decision (report §1): where the keeper contacts the ball.
// `requestAngle_deg` is the angle the deflect was asked for and `heading_deg`
// the direction the keeper faces, both in degrees. PES reads the request angle
// from the anime request; GF's deflect command carries none, so the callers pass
// the ball's incoming direction (ponytail: the closest quantity, and the
// direction the anim query already sorts on).
struct DeflectPoint {
  float radius_m;   // metres out from the keeper
  float angle_deg;  // 0..360, the offset direction
  bool raised;      // PES's `p3+0xe68 <= 0` branch: no height known, lift it
};

DeflectPoint PesDeflectPoint(float requestAngle_deg, float heading_deg,
                             const KeeperConstants& k);

// The distribution decision (report 10.6.2): `ActionKeeperAfterCatchMove` gives
// the point the keeper WALKS TO while holding the ball. It is not the pass
// target - that comes from the GK think units (10.6.3) - so this engine uses it
// as the walk target and keeps its own teammate rule for the pass.
//
// All coordinates are on this engine's pitch (x along it, z across it),
// `attackDir` is +1 when he attacks toward +x and -1 otherwise, and every mark
// is in metres.
//
// `marks` are the FieldInfo values the walk target is clamped between (PES
// F4/F5/F6/F7, magnitudes on either half): the goal-area line, the goal-area
// half width, the penalty-area line and the penalty-area half width. This engine
// passes its own Law-1 marks, which are the same surfaces (105 x 68 m pitch, the
// goal area 5.5 m deep and 18.32 m wide, the penalty area 16.5 m deep and
// 40.3 m wide - gametypes.hpp and SetPieceLaws). `pes::FieldMarks`
// (pesjudge.hpp) is the referee's subset of the same table.
//
// ponytail: PES picks the walk target's teammate as the first team-mate in his
// own neighbour list with 10 m of space (10.6.1: MA+0x22e4c is the engine's
// per-player sorted distance list, not a keeper table). This engine has no such
// list per keeper's neighbour search, so the caller applies its own
// nearest-player rule to the carry point.
struct DistributionPoint {
  float x;
  float z;
};

// The FieldInfo marks the keeper reads (report 10: F[i] = *(G+0x2aa8) + 4*i).
struct KeeperFieldMarks {
  float halfLength_m;         // F0 52.56
  float goalHalfWidth_m;      // F2  3.66
  float goalAreaLineX_m;      // F4 47.06
  float goalAreaHalfWidth_m;  // F5  9.16
  float boxLineX_m;           // F6 36.14
  float boxHalfWidth_m;       // F7 20.12
};

// Where he carries the caught ball first (report 10.6.2's first two lines):
// 4 m up-field, never nearer than 1 m inside the box line, and pulled 2 m toward
// the middle when he is wider than the goal area.
DistributionPoint PesAfterCatchCarry(float keeperX, float keeperZ, int attackDir,
                                     const KeeperFieldMarks& marks, const KeeperConstants& k);

// The walk target: the carry point, then 4 m straight toward the chosen mate
// (the bearing is taken from the keeper, as PES's `target - keeper.pos` is),
// then the box line and side clamps.
DistributionPoint PesAfterCatchWalk(float keeperX, float keeperZ, float carryX, float carryZ,
                                    float mateX, float mateZ, int attackDir,
                                    const KeeperFieldMarks& marks, const KeeperConstants& k);

// Open-play base position (report 10.2.2 out of possession, 10.2.3 in
// possession). He stands on the straight line from 1 m behind his own goal
// centre to the predicted ball, at the smallest of the style cap, 25 m behind
// the defensive line, the 4-to-13 m ball-distance ramp, the ball distance and
// 15 m - never less than 2 m (ball straight in front) to 4.16 m (ball far off
// the bearing). With the ball he steps up as a sweeper instead.
struct KeeperPositionInput {
  float ballX, ballZ;    // the predicted ball (PES: action+0x74)
  float keeperX, keeperZ;
  int defenseSide;       // +1 when he defends +x, -1 otherwise (PES: -dir)
  bool inPossession;     // his team has the ball (PES: T+0x255)
  float teamLineX;       // own frame: the team's defensive-line reference (T+0x875c)
  float offsideLineX;    // own frame: the line for his side (MA+0x40 + s*0x14)
  bool offensiveStyle;   // playing-style word bit 19 (Offensive GK)
  bool defensiveStyle;   // playing-style word bit 20 (Defensive GK)
  float gkAwareness;     // 0..99
  int jitterDraw;        // Range0(6), slot 1 of the per-clip generator:
                         // the lateral shuffle (0x1409e0590); PES's only keeper draw
};

struct KeeperSpot {
  float x, z;          // the standing point
  float radius_m;      // its distance from R along the bearing
  float bearing_deg;   // the R -> ball bearing, from +z
  float jitter_m;      // the lateral shuffle that was applied, already scaled
};

KeeperSpot PesKeeperBasePosition(const KeeperPositionInput& in, const KeeperFieldMarks& marks,
                                 const KeeperConstants& k);

// The rush (report 10.3): the race factor by style, and the lateral limit on the
// point where he meets the ball.
float PesKeeperRushFactor(bool offensiveStyle, bool defensiveStyle, const KeeperConstants& k);
float PesKeeperRushLateralLimit_m(bool offensiveStyle, bool defensiveStyle,
                                  const KeeperFieldMarks& marks);
// `keeperTime_s`/`opponentTime_s` are the seconds each needs to reach the ball
// (PES compares frames; `gs * x` frames is x seconds at any rate). He rushes when
// the opponent's time plus the hysteresis does not stay inside the factor times
// his own.
bool PesKeeperRushRaceWins(float keeperTime_s, float opponentTime_s, bool rushing,
                           float factor, const KeeperConstants& k);

}  // namespace pes

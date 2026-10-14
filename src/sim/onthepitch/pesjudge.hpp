#pragma once

// PES 2021's foul decision: match::ai::Judge, docs/re/pes-re-duels-fouls.md 2.
// A contact is scored as points = P_angle + P_speed + P_action, the points map
// to a level 0..4 through three thresholds, and the level is the card
// (1 foul, 2 foul + warning, 3 yellow, 4 red). Deterministic: no RNG, and no
// referee strictness (no referee data is read anywhere in PES's decision).
//
// Every number comes from the user's PES2021.exe through pes::PeImage::ReadVa:
// floats from .rdata, the integer weights from the immediates of the
// instructions that use them (each instruction's opcode bytes are checked
// before its immediate is trusted), and the protected words through the same
// XOR the game applies.

#include <optional>
#include <string>

#include "../gametypes.hpp"

#include "pes/exe.hpp"

namespace pes {

// Where the chief referee stands for each restart: RefereeChief slot 20
// (0x140a38c40) dispatches on the restart mode, docs/re/pes-re-protection.md
// 3.3. All .rdata floats; the comment is the PES value and its handler.
struct RefereeSpotConstants {
  float kickOffBehind_m;        // 5, kick-off FUN_140a38a00
  float kickOffAcross_m;        // 10
  float throwInUpfieldGate_m;   // 3, throw-in FUN_140a38b00
  float throwInTrail_m;         // 5: upfield of the gate, this far behind the ball
  float throwInLeadDivisor;     // 10: otherwise |x|/10 + 8 ahead of it
  float throwInLeadBase_m;      // 8
  float throwInInset_m;         // 4: off the box's side line and its front line
  float goalKickX_m;            // -4, goal kick 0x144161f30
  float goalKickDrop_m;         // 2.5
  float cornerInset_m;          // 0.5, corner 0x14415d330 (read by emulation)
  float cornerOut_m;            // 2
  float penaltyAhead_m;         // 4, penalty FUN_140a38a70
  float penaltyOut_m;           // 5
  // Free kick 0x144161790.
  float fkHalf;                 // 0.5: the box line's midpoint splits two bands
  float fkDeepX_m;              // -14: own half, near the centre line
  float fkOwnHalfOut_m;         // 2: own half, off the goal area
  float fkOwnBoxX_m;            // 3: own half, beyond the box line
  float fkOwnBoxIn_m;           // 3
  float fkWideDivisor;          // 3: the box width splits in thirds
  float fkBoxCornerInset_m;     // 4, zone 1
  float fkBoxCornerOut_m;       // 2
  float fkQuarter;              // 0.25, zone 3
  float fkEdgeInset_m;          // 3, zones 4 and 6
  float fkEdgeOut_m;            // 3.5
  float fkWideOut_m;            // 1.5, zone 5
  float fkFarAcross_m;          // 10, zone 7
  float fkCentreAhead_m;        // 10, zone 8
  float fkCentreAcross_m;       // 8
  float fkFlankAhead_m;         // 5, zone 9
  float fkFlankAcross_m;        // 15
};

struct JudgeConstants {
  // P_angle (report 2.3, protected 0x143f738a0).
  float angleFloor_deg;        // 40: no points below this approach angle
  float angleSpan_deg;         // 140: full points at floor + span (from behind)
  float slideAngleSpan_deg;    // 120: the slide's span
  float slideAngleKnee_deg;    // 80: above this the slide's angle is pulled back
  float slideGateRamp_deg;     // 5: g1's ramp above the 40-degree floor
  float slideAlignKnee_deg;    // 45: g2 is 0 above this heading difference
  float slideAlignRamp_deg;    // -10: g2 reaches 1 at 35 degrees
  float angleMaxPoints;        // W = 40
  float notFallenFactor;       // 0.7: the victim stayed on his feet
  // P_speed (protected 0x143fa8420).
  float kmhPerMetrePerSecond;  // 3.6
  float speedFloor_kmh;        // 2
  float speedSpan_kmh;         // 28
  float speedMaxPoints;        // 30
  int fallenMinSpeedPoints;    // 5
  int jostleSpeedDivisor;      // 20
  // P_action (FUN_1405b6d40).
  int recklessPoints;          // 35: slides and other reckless actions
  int tacklePoints;            // 25: a tackle of the general sub-kind
  int recklessFallenBonus;     // +15
  int tackleFallenBonus;       // +10
  int dogsoBonus;              // +30
  // Level thresholds (protected 0x143fa8760).
  int foulThreshold;           // T1 = 40
  int boxFoulThreshold;        // T1 in the penalty area = 60
  int cautionThreshold;        // T2 = 80
  int redThreshold;            // T3 = 110
  // The spot bias of FUN_1405b6870: 6 cm towards the victim's own goal and the
  // touchline, so a contact on the box line is judged outside.
  float spotBias_m;            // 0.06
  // Advantage (report 3): the whistle is held this long after a contact
  // (FUN_14050cd70: floor(0.8*gs + 0.5) frames), and an advantage, once played,
  // runs for this long (FUN_14050b490: GameSpeed << 2 frames).
  float whistleHold_s;         // 0.8
  float advantageWindow_s;     // 4
  // Offside (docs/re/pes-re-offside.md 3/4): the body points' surface margins
  // (xmm8/xmm7/xmm10 of FUN_140a7b7a0), the tolerances the leading surface is
  // judged with, the defender rear points' shifts and the category bands.
  // GF has centre positions only, so the torso margin is the one this port's
  // model uses; the rest are read so the model stays honest and the relations
  // between them can be checked.
  float offsideMarginTorso_m;      // 0.15, 0x1425c48a0: the head/torso margin, and the tolerance when nothing arm-like leads
  float offsideMarginArm_m;        // 0.06, 0x1425cc238: an elbow or wrist point
  float offsideMarginToe_m;        // 0.10, 0x142588200: a toe point
  float offsideArmTolerance_m;     // 0.30, 0x142590d98: the tolerance when an arm point leads
  // The line's rear surface, shifted towards the defending team's own goal:
  // PES stores the shifts signed (FUN_14045cf30, -0.15/-0.06/-0.10).
  float offsideRearShiftTorso_m;   // -0.15, 0x14259d64c
  float offsideRearShiftArm_m;     // -0.06, 0x14259d644
  float offsideRearShiftToe_m;     // -0.10, 0x14259d648
  float offsideNearBand_m;         // 1.0, 0x14256ae1c: category 2 / 3
  float offsideClearBand_m;        // 4.0, 0x1425656c0: category 1 / 0
  RefereeSpotConstants refereeSpot;
};

std::optional<JudgeConstants> LoadJudgeConstants(const PeImage& exe, std::string* error);

// What the fouler did (report 2.3, B's action). This engine's contacts are a
// shoulder/jostle, a standing tackle that brought the man down, and a slide.
enum class JudgeAction { Jostle, Tackle, Slide };

struct JudgeContact {
  JudgeAction action = JudgeAction::Tackle;
  // B's approach angle to A in degrees: 180 = from directly behind, 0 = head on.
  float approachAngle_deg = 0.0f;
  // |vel(A) - vel(B)| in metres per second.
  float relativeSpeed_mps = 0.0f;
  // Slides only: |angle between B's heading and the B->A direction| and
  // |angle between A's and B's headings|, both in degrees (report 2.3 g1, g2).
  float slideOffLine_deg = 0.0f;
  float headingsDelta_deg = 0.0f;
  bool victimFell = false;
  // Beyond the fouling team's last defensive line and within the box width.
  bool dogso = false;
  // The biased spot lies in the penalty area A attacks.
  bool inAttackingBox = false;
};

int JudgePoints(const JudgeContact& c, const JudgeConstants& k);
// Level 0..4 from the points (report 2.4), before the overrides.
int JudgeLevelFromPoints(int points, bool inAttackingBox, const JudgeConstants& k);
// The full decision: points, thresholds and the penalty-area overrides of
// report 2.5 step 6. Returns the level 0..4.
int JudgeLevel(const JudgeContact& c, const JudgeConstants& k);

// --- Advantage (report 3) ---

// The referee may only wave play on in the last third of the whistle hold
// (FUN_14050ff50: hold <= holdFrames/3).
bool InAdvantageDecisionWindow(float sinceContact_s, const JudgeConstants& k);

// ShouldPlayAdvantage (FUN_14050ff50): never for a penalty or a red-level foul;
// a yellow-level foul only when the fouler is not already booked; then the
// fouled team's nearest man must be at least as close to the ball as the
// fouling team's.
bool ShouldPlayAdvantage(int level, bool penalty, bool foulerBooked, float fouledNearest_m,
                         float foulingNearest_m);

// One step of the advantage clock: it runs at double speed while the fouled
// team has the ball, and every pass or shot of theirs burns a whole second
// (gs frames, i.e. one second by construction).
float AdvanceAdvantage(float remaining_s, float dt_s, bool fouledTeamHasBall,
                       bool fouledTeamPassed);

// PES pitch space: x along the pitch, z across it (z = -GF y), metres.
struct PitchSpot {
  float x, z;
};

// The four FieldInfo values the referee reads (*(G+0x2aa8) F[0], F[5..7]).
struct FieldMarks {
  float halfLength_m;        // F[0]
  float goalAreaHalfWidth_m;  // F[5]
  float penaltyLineX_m;      // F[6]
  float penaltyHalfWidth_m;  // F[7]
};

// The chief referee's mark for a restart at `restart`, taken by the team
// attacking towards `attackDir` x (+1/-1). Empty for a free kick level with
// the penalty area, where PES writes nothing and he stays where he is.
std::optional<PitchSpot> ChiefRefereeSpot(e_SetPiece kind, PitchSpot restart, int attackDir,
                                          const FieldMarks& f, const RefereeSpotConstants& k);

}  // namespace pes

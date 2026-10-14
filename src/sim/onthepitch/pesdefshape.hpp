// PES 2021's open-play defensive shape (docs/re/pes-re-defensive-shape.md 6,
// 7, 14): the line height, the team length and width, and the marking
// assignment's pure arithmetic. The match-side code (TeamAIController) feeds
// these with the current ball, carrier and players; everything here is a
// function of its arguments so the constants and the relations between them can
// be tested without a match.
//
// Constants marked `exe` come from the install's PES2021.exe image and are read
// by LoadDefensiveShapeConstants. Constants marked `bp` are the live
// `basePosition.o` record the report read at runtime: the VFS offset for that
// record is still a killed hypothesis (report 11), so they are transcribed here
// with that ceiling noted.

#ifndef _HPP_ONTHEPITCH_PESDEFSHAPE
#define _HPP_ONTHEPITCH_PESDEFSHAPE

#include <optional>
#include <string>

#include "base/math/bluntmath.hpp"
#include "base/math/vector3.hpp"
#include "gametypes.hpp"
#include "pes/exe.hpp"

namespace pes {

using blunted::Vector3;

// PES's FieldInfo scalars the shape reads (report 6). PES's pitch is 105.12 x
// 68.12 and GF's is 105 x 68, so these are derived from GF's own pitch by the
// same offsets rather than transcribed; the 6 cm difference is the line's own
// width and is below the port's resolution.
struct ShapeField {
  float halfLength_m;  // F[0], centre to the goal line
  float halfWidth_m;   // F[1], centre to the touchline
  float ownBox_m;      // F[6], the own penalty-area line
  float ownGoalArea_m;  // F[4], the deepest the line may sit
  float eleven_m;      // F[8], 11 m from the own goal line
  float goalAreaFloor_m;  // F[0x11]
};

ShapeField ShapeFieldFromPitch();

struct DefensiveShapeConstants {
  // --- line ramp (f9eb0) ---
  float foreCheck_m;              // bp -9
  float foreCheckSlider_m;        // exe 0x14259be84, subtracted per line step
  float retreatAngle_deg;         // exe 0x14259be5c, style 1's sine angle
  float dfLineCenterAdjustMax_m;  // bp -8
  float dfLineCenterAdjustMin_m;  // bp -20
  float strategyDefensive_m;      // bp 1
  float strategyLevelUp_m;        // bp -14
  float rampHighFactor;           // exe 0x14257a190, 0.6
  float rampLowFactor;            // exe 0x1425c8210, -0.2
  float boxRamp_m;                // exe 0x14259be7c, -5 at attack level 2
  float stepBack_m;               // exe 0x1425cd064, 3.75
  float deepOffset_m;             // exe 0x14259222c, +5 with the press plan
  // --- cushion and step-up (f7a00 / ef910) ---
  float marginTargetLineX_m;      // bp -10
  float cushionAwayAngle_deg;     // exe 0x14259be60, 135
  float cushionSpeedDenominator;  // exe 0x1425cd078, 30000; divided by fps*3600
  float cushionFps;               // PES's FUN_141e532d0, 54 (report 14.1)
  float speedPerHour_s;           // exe 0x142598590, 3600
  float cushionHalfFactor;        // exe 0x142565600, 0.5
  float cushionFacingFloor_deg;   // exe 0x14259be48, 65
  float cushionFacingSpan_deg;    // 115 (report 14.1: (a - 180) / -115)
  float stepUpDistance_m;         // exe 0x1425656c0, 4
  float stepUpHeading_deg;        // exe 0x14253ee30, 100
  float stepUpSpeed_mps;          // exe 0x1425cd068, 5.5556
  float stepUpFacingSpan_deg;     // 35 (report 14.1)
  // --- length ---
  float lengthDf_m;        // bp 46
  float lengthDfRetreat_m;  // bp 42
  float lengthDfSuperRetreat_m;  // bp 32
  float adjustXCompactFW_m;      // bp 0.33
  // --- width (688300 / 63f230) ---
  float dfLineWidth3_m;         // bp 15
  float dfLineWidth4_m;         // bp 19
  float dfLineWidth5_m;         // bp 23
  float dfLineWidthFew_m;       // exe 0x142588210, 10
  float adjustZCompact_m;       // bp 2.85
  float minWidth_m;             // bp 6
  float minWidthDefensive_m;    // bp 5
  float widthCapFactor;         // exe 0x1425ab120, 0.85: never wider than the box's half
  float lateralRampNear_m;      // exe 0x14259bdec, 6
  float widthLevelReduction_m;  // 6, code immediate (report 7)
  float lateralRampFar_m;       // report 7: the ramp runs 6 m .. 15.12 m of ball |z|
  float lastLineCloseMaxRate;   // bp 0.34
  float dfLineCloseRate;        // bp 0.61
  // --- marking (14.2 / 14.3) ---
  float markDistanceBase_m;          // 6
  float markDistanceSpan_m;          // 4
  float markUpfieldRamp_m;            // 20
  float markPressuringBonus_m;        // bp 2
  float stealRadius_m;                // FUN_140529330, 35 m
  float stealRadiusLowLevel_m;        // 20 m when the attack level is below 3
  float midGap_m;                     // bp 7.25 (report 14.1's minDistMF_DF)
  float midLineScoreAdjustMax;        // bp 36
  float midCatchUpDivisor;            // bp 4
  float handOffOrganiserFloor;        // exe 0x142588210, 10 (b <= 10 -> w = 1)
  float handOffOrganiserTop;          // exe 0x14259be34, 50 (b >= 50 -> immediate)
  float handOffFrames_m;              // exe 0x14259a764, 60
  float handOffOrganiserSpan;         // exe 0x1425c6604, -40
};

std::optional<DefensiveShapeConstants> LoadDefensiveShapeConstants(const PeImage& exe,
                                                                  std::string* error);

// PES's ramp(x; at0 -> value0, at1 -> value1), linear and clamped.
float ShapeRamp(float x, float at0, float value0, float at1, float value1);

// What the line chain reads about the ball and the man on it (report 6, 14.1).
struct LineFacts {
  float ballDepth_m = 0.0f;    // own-frame x of the analysis ball
  float carrierDepth_m = 0.0f;  // own-frame x of the carrier, 0 when none
  float carrierFacingFromGoal_deg = 0.0f;  // |facing(carrier) - straight back|
  float ballHeadingFromGoal_deg = 180.0f;  // |heading(ballVel) - straight back|
  float ballSpeed_mps = 0.0f;
};

// FUN_1405f7a00: the factor on marginTargetLineX. 1 keeps the full cushion; it
// shrinks as the carrier turns his back to our goal or the ball travels away.
float TargetLineCushionFactor(const DefensiveShapeConstants& k, const LineFacts& ball,
                              bool hasCarrier, bool carrierIsOpponent, float previousLineDepth_m,
                              float lineDepth_m);

// FUN_1405ef910: whether a deep line steps up to 4 m behind the ball.
bool DefensiveLineStepUp(const DefensiveShapeConstants& k, const ShapeField& field,
                         const LineFacts& ball, float lineDepth_m, bool hasCarrier);

// FUN_1405f9eb0 + f7a00 + ef910: the back line's lower edge, own frame.
float DefensiveLineHeight(const DefensiveShapeConstants& k, const ShapeField& field,
                          const LineFacts& ball, float lineSlider, int style, int attackLevel,
                          bool inPlay, float cushionFactor);

// The opponent order key of report 3.1: most advanced first, the opponent
// projected one sixth of a second ahead.
float OpponentOrderKey(float dir, float depth_m, float depthVelocity_mps);

// FUN_140688300 + 63f230: a line's half-width in metres.
float LineHalfWidth_m(const DefensiveShapeConstants& k, const ShapeField& field,
                      int playersInLine, float compactSlider, int attackLevel, bool inPlay,
                      float ballOffset_m, float ballDepth_m, float lineDepth_m);

// FUN_1405f9eb0's length term: 46/42/32 m by style and attack level, minus 0.33
// m per Compactness step.
float TeamLength_m(const DefensiveShapeConstants& k, int style, int attackLevel,
                   float compactSlider);

// FUN_140a62140's byte -> the hand-off delay in frames (report 3.3).
int MarkHandOffFrames(const DefensiveShapeConstants& k, int organiserByte);

// FUN_140623750's distance term (report 14.3).
float MarkDistance_m(const DefensiveShapeConstants& k, const ShapeField& field, float oppDepth_m,
                     bool pressuring);

// FUN_140623750's goal-side point.
Vector3 MarkSpot(const Vector3& oppPredicted, float distance_m, const Vector3& ownGoal);

}  // namespace pes

#endif

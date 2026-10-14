// PES 2021's open-play defensive shape: the pure arithmetic of the line height,
// the team length and width, and the marking helpers
// (docs/re/pes-re-defensive-shape.md 6, 7, 14.1, 14.2, 14.3). See pesdefshape.hpp
// for where each constant comes from.

#include "onthepitch/pesdefshape.hpp"

#include <cmath>

#include "teamphilosophy.hpp"

namespace pes {
namespace {

// A .rdata float: PES keeps every one of these as a plain float.
bool ReadFloat(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, sizeof(float), reinterpret_cast<uint8_t*>(out));
}

struct ShapeVa {
  uint64_t va;
  float DefensiveShapeConstants::*field;
};

}  // namespace

ShapeField ShapeFieldFromPitch() {
  ShapeField f{};
  f.halfLength_m = pitchHalfW;
  f.halfWidth_m = pitchHalfH;
  // F[6]: the own penalty-area line, 16.5 m up the pitch from the goal line.
  f.ownBox_m = pitchHalfW - TeamPhilosophy::penaltyBoxDepth;
  // F[4]: 5.5 m up - the deepest the line is ever allowed to sit.
  f.ownGoalArea_m = pitchHalfW - 5.5f;
  // F[8] and F[0x11] (report 6).
  f.eleven_m = pitchHalfW - 11.0f;
  f.goalAreaFloor_m = 9.15f;
  return f;
}

std::optional<DefensiveShapeConstants> LoadDefensiveShapeConstants(const PeImage& exe,
                                                                  std::string* error) {
  // Every value the chain takes from the image rather than from basePosition.o,
  // at the virtual addresses the report's raw decodes name.
  const ShapeVa table[] = {
      {0x14259be84, &DefensiveShapeConstants::foreCheckSlider_m},       // 10
      {0x14259be5c, &DefensiveShapeConstants::retreatAngle_deg},        // 90
      {0x14257a190, &DefensiveShapeConstants::rampHighFactor},          // 0.6
      {0x1425c8210, &DefensiveShapeConstants::rampLowFactor},           // -0.2
      {0x14259be7c, &DefensiveShapeConstants::boxRamp_m},               // -5
      {0x1425cd064, &DefensiveShapeConstants::stepBack_m},              // 3.75
      {0x14259be60, &DefensiveShapeConstants::cushionAwayAngle_deg},    // 135
      {0x1425cd078, &DefensiveShapeConstants::cushionSpeedDenominator},  // 30000
      {0x142598590, &DefensiveShapeConstants::speedPerHour_s},          // 3600
      {0x142565600, &DefensiveShapeConstants::cushionHalfFactor},       // 0.5
      {0x14259be48, &DefensiveShapeConstants::cushionFacingFloor_deg},  // 65
      {0x1425656c0, &DefensiveShapeConstants::stepUpDistance_m},        // 4
      {0x14253ee30, &DefensiveShapeConstants::stepUpHeading_deg},       // 100
      {0x1425cd068, &DefensiveShapeConstants::stepUpSpeed_mps},         // 5.5556
      {0x142588210, &DefensiveShapeConstants::dfLineWidthFew_m},        // 10
      {0x1425ab120, &DefensiveShapeConstants::widthCapFactor},          // 0.85
      {0x14259bdec, &DefensiveShapeConstants::lateralRampNear_m},       // 6
      {0x142588210, &DefensiveShapeConstants::handOffOrganiserFloor},   // 10
      {0x14259be34, &DefensiveShapeConstants::handOffOrganiserTop},     // 50
      {0x14259a764, &DefensiveShapeConstants::handOffFrames_m},         // 60
      {0x1425c6604, &DefensiveShapeConstants::handOffOrganiserSpan},    // -40
  };
  DefensiveShapeConstants k{};
  bool ok = true;
  for (const ShapeVa& v : table) ok &= ReadFloat(exe, v.va, &(k.*v.field));
  if (!ok) {
    if (error) *error = "defensive shape constant: .rdata float unreadable";
    return std::nullopt;
  }
  // The live `basePosition.o` record the report read at runtime, at the object
  // vtable 0x142b5a328 (report 8). Its VFS offset is still a killed hypothesis
  // (report 11), so these are transcribed rather than read from the install.
  // ponytail: read constant_team.bin's basePosition.o through PesVfs once the
  // container's layout is reduced; until then a changed .o would need this block
  // edited.
  k.foreCheck_m = -9.0f;
  k.dfLineCenterAdjustMax_m = -8.0f;
  k.dfLineCenterAdjustMin_m = -20.0f;
  k.strategyDefensive_m = 1.0f;
  k.strategyLevelUp_m = -14.0f;
  k.marginTargetLineX_m = -10.0f;
  k.cushionFacingSpan_deg = 115.0f;
  k.stepUpFacingSpan_deg = 35.0f;
  k.lengthDf_m = 46.0f;
  k.lengthDfRetreat_m = 42.0f;
  k.lengthDfSuperRetreat_m = 32.0f;
  k.adjustXCompactFW_m = 0.33f;
  k.dfLineWidth3_m = 15.0f;
  k.dfLineWidth4_m = 19.0f;
  k.dfLineWidth5_m = 23.0f;
  k.adjustZCompact_m = 2.85f;
  k.minWidth_m = 6.0f;
  k.minWidthDefensive_m = 5.0f;
  k.widthLevelReduction_m = 6.0f;
  k.lateralRampFar_m = 15.12f;
  k.lastLineCloseMaxRate = 0.34f;
  k.dfLineCloseRate = 0.61f;
  k.markDistanceBase_m = 6.0f;
  k.markDistanceSpan_m = 4.0f;
  k.markUpfieldRamp_m = 20.0f;
  k.markPressuringBonus_m = 2.0f;
  k.stealRadius_m = 35.0f;
  k.stealRadiusLowLevel_m = 20.0f;
  k.midGap_m = 7.25f;
  k.midLineScoreAdjustMax = 36.0f;
  k.midCatchUpDivisor = 4.0f;
  k.cushionFps = 54.0f;
  return k;
}

float ShapeRamp(float x, float at0, float value0, float at1, float value1) {
  const float t = (at1 == at0) ? 1.0f : (x - at0) / (at1 - at0);
  return value0 + (value1 - value0) * std::clamp(t, 0.0f, 1.0f);
}

float TargetLineCushionFactor(const DefensiveShapeConstants& k, const LineFacts& ball,
                              bool hasCarrier, bool carrierIsOpponent, float previousLineDepth_m,
                              float lineDepth_m) {
  // FUN_1405f7a00. `away` is the ball travelling out of our half at speed; the
  // carrier's facing then takes over and the cushion shrinks as he turns his
  // back to our goal.
  const bool away =
      std::fabs(ball.ballHeadingFromGoal_deg) > k.cushionAwayAngle_deg &&
      ball.ballSpeed_mps * k.cushionFps * k.speedPerHour_s > k.cushionSpeedDenominator;
  if (!hasCarrier) return away ? 0.0f : 1.0f;
  if (!carrierIsOpponent) return 0.0f;
  if (previousLineDepth_m < lineDepth_m) return away ? k.cushionHalfFactor : 1.0f;
  const float facing = ball.carrierFacingFromGoal_deg;
  return facing <= k.cushionFacingFloor_deg
             ? 1.0f
             : (facing < 180.0f ? (facing - 180.0f) / -k.cushionFacingSpan_deg : 0.0f);
}

bool DefensiveLineStepUp(const DefensiveShapeConstants& k, const ShapeField& field,
                         const LineFacts& ball, float lineDepth_m, bool hasCarrier) {
  // FUN_1405ef910. Only a line deeper than the box line steps up, and only when
  // the ball is in front of it.
  if (lineDepth_m >= 1.0f - field.ownBox_m) return false;
  if (ball.ballDepth_m <= lineDepth_m + k.stepUpDistance_m) return false;
  if (!hasCarrier) return false;
  if (std::fabs(ball.ballHeadingFromGoal_deg) > k.stepUpHeading_deg &&
      ball.ballSpeed_mps > k.stepUpSpeed_mps)
    return true;  // the ball is being played away from us
  const float facing = ball.carrierFacingFromGoal_deg;
  return ShapeRamp(facing, k.stepUpHeading_deg, 0.0f, k.stepUpHeading_deg + k.stepUpFacingSpan_deg,
                   1.0f) > 0.0f;
}

float DefensiveLineHeight(const DefensiveShapeConstants& k, const ShapeField& field,
                          const LineFacts& ball, float lineSlider, int style, int attackLevel,
                          bool inPlay, float cushionFactor) {
  // FUN_1405f9eb0, own frame: a ramp of the ball's depth between two line-height
  // offsets, then the step-up when deep, the cushion behind the ball, and the
  // two hard clamps. No smoothing: the target is rebuilt from this tick's ball.
  const float slider = std::clamp(lineSlider, 1.0f, 10.0f);
  float a = 0.0f;
  if (style == 0) {
    a = -k.foreCheck_m - k.foreCheckSlider_m * ShapeRamp(slider, 1.0f, 0.0f, 10.0f, 1.0f);
  } else if (style == 1) {
    const float t = ShapeRamp(slider, 10.0f, 1.0f, 1.0f, 0.0f);
    a = 5.0f * std::sin(k.retreatAngle_deg * t * blunted::pi / 180.0f) - 11.0f;
  }
  float hi = k.rampHighFactor * field.halfLength_m + k.dfLineCenterAdjustMax_m + a;
  float lo = k.rampLowFactor * field.halfLength_m + k.dfLineCenterAdjustMin_m + a;
  if (inPlay) {
    const float level = ShapeRamp(static_cast<float>(attackLevel), 2.0f, 0.0f, 3.0f, 1.0f);
    hi -= level * k.strategyDefensive_m;
    lo -= level * k.strategyDefensive_m;
  }
  if (attackLevel == 3) {
    hi += k.strategyLevelUp_m;
    lo += k.strategyLevelUp_m;
  }

  const float deep = 1.0f - field.ownBox_m;
  float x = deep - std::clamp((ball.ballDepth_m - lo) / (hi - lo), 0.0f, 1.0f) * (deep + 5.0f);

  if (x > -field.ownBox_m) {
    x += (attackLevel == 2 ? k.boxRamp_m : 0.0f) -
         k.stepBack_m * ShapeRamp(static_cast<float>(attackLevel), 1.0f, 1.0f, 2.0f, 0.0f);
    x = std::max(x, -field.ownBox_m);
  }

  // Never less than the cushion behind the ball, then the step-up when deep.
  x = std::min(x, ball.ballDepth_m + k.marginTargetLineX_m * cushionFactor);
  // Never past halfway, never deeper than the goal-area line.
  x = std::min(x, 0.0f);
  return std::max(x, -field.ownGoalArea_m);
}

float TeamLength_m(const DefensiveShapeConstants& k, int style, int attackLevel,
                   float compactSlider) {
  // ponytail: the team-level length bonus {10, 7.5, 5, 2.5, 0} is skipped -
  // GF has no per-team level and PES's live teams were both level 2 (+5 m).
  const float retreat = ShapeRamp(static_cast<float>(attackLevel), 0.0f, 1.0f, 2.0f, 0.0f);
  const float base = style == 0 ? k.lengthDf_m : k.lengthDfRetreat_m;
  const float len = base + (k.lengthDfSuperRetreat_m - base) * retreat;
  return len - (std::clamp(compactSlider, 1.0f, 10.0f) - 1.0f) * k.adjustXCompactFW_m;
}

float LineHalfWidth_m(const DefensiveShapeConstants& k, const ShapeField& field,
                      int playersInLine, float compactSlider, int attackLevel, bool inPlay,
                      float ballOffset_m, float ballDepth_m, float lineDepth_m) {
  // FUN_140688300 + FUN_14063f230: the half-width by the line's head count, the
  // Compactness slider, the attack level, and then the ball-side squeeze.
  float half = playersInLine == 3   ? k.dfLineWidth3_m
             : playersInLine == 4 ? k.dfLineWidth4_m
             : playersInLine >= 5 ? k.dfLineWidth5_m
                                  : k.dfLineWidthFew_m;
  half += -k.adjustZCompact_m *
          std::clamp((std::clamp(compactSlider, 1.0f, 10.0f) - 5.5f) / 4.5f, -1.0f, 1.0f);
  const float level = ShapeRamp(static_cast<float>(attackLevel), 0.0f, 1.0f, 2.0f, 0.0f);
  if (inPlay) half -= k.widthLevelReduction_m * level;
  const float spacing = k.minWidth_m + (k.minWidthDefensive_m - k.minWidth_m) * level;
  half = std::clamp(half, (playersInLine - 1) * 0.5f * spacing,
                    k.widthCapFactor * field.halfWidth_m);

  const float lateral =
      std::clamp((std::fabs(ballOffset_m) - k.lateralRampNear_m) /
                     (k.lateralRampFar_m - k.lateralRampNear_m),
                 0.0f, 1.0f);
  const float deep = ShapeRamp(ballDepth_m, -field.ownBox_m, 1.0f, 10.0f, 0.0f);
  const float gap = std::fabs(lineDepth_m - ballDepth_m);
  float k2 = ShapeRamp(gap, 7.0f, 1.0f, 20.0f, 0.0f);
  if (ballDepth_m < lineDepth_m) k2 = 1.0f;  // the ball is behind the line
  const float squeeze = std::max(k2 * k.lastLineCloseMaxRate, deep);
  const float closeRate = std::clamp(k.dfLineCloseRate + 0.25f, 0.0f, 1.0f);
  return half + (closeRate * half - half) * squeeze * (1.0f - 0.6f * lateral);
}

float OpponentOrderKey(float dir, float depth_m, float depthVelocity_mps) {
  // FUN_140534a20's comparator: most advanced opponent first, projected one
  // sixth of a second ahead (the report's per-frame velocity times fps/6).
  return -dir * (depth_m + depthVelocity_mps / 6.0f);
}

int MarkHandOffFrames(const DefensiveShapeConstants& k, int organiserByte) {
  // A changed target is committed only once the same new one has been pending
  // this long: a good organiser hands markers over quickly, the worst replaces
  // only after the full minute-of-frames.
  const float w = organiserByte <= static_cast<int>(k.handOffOrganiserFloor)
                      ? 1.0f
                  : organiserByte < static_cast<int>(k.handOffOrganiserTop)
                      ? (k.handOffOrganiserTop - static_cast<float>(organiserByte)) /
                            k.handOffOrganiserSpan
                      : 0.0f;
  if (w <= 0.0f) return 0;
  return static_cast<int>(std::floor(w * k.handOffFrames_m));
}

float MarkDistance_m(const DefensiveShapeConstants& k, const ShapeField& field, float oppDepth_m,
                     bool pressuring) {
  // 2 m against a deep man, 6 m against one upfield of 20 m, plus the Pressuring
  // option's 2 m.
  const float deep = ShapeRamp(oppDepth_m, 20.0f - field.ownBox_m, 1.0f, k.markUpfieldRamp_m, 0.0f);
  return k.markDistanceBase_m - k.markDistanceSpan_m * deep +
         (pressuring ? k.markPressuringBonus_m : 0.0f);
}

Vector3 MarkSpot(const Vector3& oppPredicted, float distance_m, const Vector3& ownGoal) {
  // FUN_140623750's goal-side point.
  const Vector3 toGoal = (ownGoal - oppPredicted).GetNormalized(Vector3(1.0f, 0.0f, 0.0f));
  return oppPredicted + toGoal * distance_m;
}

}  // namespace pes

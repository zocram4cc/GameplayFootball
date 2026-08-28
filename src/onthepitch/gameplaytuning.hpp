// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");

#ifndef _HPP_GAMEPLAY_TUNING
#define _HPP_GAMEPLAY_TUNING

#include <algorithm>
#include <cmath>

#include "base/math/bluntmath.hpp"
#include "base/properties.hpp"

namespace GameplayTuning {

inline float Clamp01(float value) {
  return std::max(0.0f, std::min(value, 1.0f));
}

// Adds a small, explainable first-touch penalty under close pressure or when a
// fast incoming ball arrives from outside the player's field of view.
inline float GetFirstTouchContextPenalty(float opponentDistance, float calmness, float balance,
                                         float incomingBallSpeed, float incomingFacingAlignment) {
  const float pressure = Clamp01((1.6f - opponentDistance) / 1.2f);
  const float composure = Clamp01(calmness * 0.65f + balance * 0.35f);
  const float ballPace = Clamp01((incomingBallSpeed - 4.0f) / 10.0f);
  const float blindSide = Clamp01((0.25f - incomingFacingAlignment) / 1.25f);

  const float pressurePenalty = pressure * (1.0f - composure * 0.80f) * 0.08f;
  const float orientationPenalty = ballPace * blindSide * (1.0f - composure * 0.60f) * 0.08f;
  return std::min(pressurePenalty + orientationPenalty, 0.14f);
}

// A shorter support web gives receivers a more predictable first touch.
// This assist is deliberately broad: positioning, not player tiers, controls it.
inline float GetTrapPredictionAssist(float supportDistance) {
  return 1.0f - 0.12f * (1.0f - Clamp01(supportDistance));
}

// How far from the anim's touch point a receiver may still kill the ball.
// The stock 0.4 m window is the single largest source of pass failure: balls
// landing just outside it are simply untouchable. Config-tunable, general for
// every philosophy and skill tier.
//
// Raised from 0.8 m once the failure breakdown was complete enough to see what
// was left: with interceptions, bad traps and out-of-play all counted, the
// largest remaining bucket was passes nobody touched at all - the ball arrived
// and no touch event fired. That is this window, not the receiver's decision.
// A metre is about a player's reach, so it stays inside what a person could
// plausibly stretch to rather than becoming a magnet.
inline float GetTrapTouchableDistance(const blunted::Properties& config) {
  return blunted::clamp(config.GetReal("gameplay_trap_touchable_distance", 0.95f), 0.2f, 1.0f);
}

// The touch check that follows this radius is otherwise binary: miss the
// window by a hair and the ball gets no touch event at all, it runs straight
// through. That gate is the dominant, untouched sink in the pass-failure
// breakdown (trap 8-24 per side). A live headless probe caught it rejecting
// a touch that missed by centimetres on a slow ball (0.84 m at 4.45 m/s):
// animation-blend slop that has nothing to do with speed, hence the modest
// baseline. A fast incoming ball is still harder to line up exactly, so the
// gate widens further with speed on top of that. General: every philosophy
// and skill tier gets it; GetDifficultyFactors still decides how well the
// touch lands.
inline float GetTrapAcceptGateScale(float ballSpeed) {
  return 1.15f + 0.20f * Clamp01((ballSpeed - 4.0f) / 16.0f);
}

// Error loft on an outgoing touch. The stock code added difficultyFactor *
// 5.0 m/s of vertical velocity on any bad roll, lofting ground passes into
// balls the receiver's 1 m height gate and sub-metre touch radius can never
// accept - a self-inflicted trap failure. A misplayed short pass should be
// misdirected or underhit, not chipped; aerial balls keep the full loft.
inline float GetPassErrorLoft(float difficultyFactor, bool groundPass) {
  return difficultyFactor * (groundPass ? 1.25f : 5.0f);
}

// Odds multiplier for a pass aimed at a marked receiver, applied AFTER the
// lane danger is normalized. The first cut added seconds before the clamp,
// which saturated (and vanished) exactly under a real press, where lane
// danger alone already exceeded 0.65. As a multiplier it always bites: a
// marked target's odds drop by up to 35% whatever the lane looks like.
inline float GetReceiverPressureDanger(float nearestOpponentDistance) {
  return 0.35f * (1.0f - Clamp01((nearestOpponentDistance - 1.0f) / 6.0f));
}

// How likely a pass is to come off on execution alone, before anyone contests it.
//
// _GetPassingOdds used to price exactly one way a pass dies: an opponent close
// enough to the lane to intercept it. Down an empty channel a forty-metre ball
// therefore scored the same as a five-metre one, so pass selection - which for
// attacking roles weights tactical advantage up to eleven times the odds - kept
// choosing the long ball to the most advanced man. Measured mean pass length sat
// at 20-25 m with a quarter of all passes beyond 25 m, and the balls that reached
// nobody were the single largest sink in the breakdown.
//
// Distance costs accuracy for reasons that have nothing to do with marking: more
// flight time for the target to be somewhere else by arrival, more spread in
// power and direction, and a harder ball to kill on the other end. The curve is
// flat under about six metres, where none of that bites, and falls off with the
// square of distance after that. It never reaches zero: a long ball is worse, not
// impossible.
// Long range is penalised hard because the aim itself degrades there: AI_GetPass
// leads a moving receiver by at most 0.7 s of his movement, so a forty-metre ball
// with roughly two seconds of flight is aimed several metres behind a man who is
// still running. Those are the passes that reach nobody at all - neither the
// target nor an opponent - and they were the largest remaining hole once body
// collisions were counted.
// Real completion rates fall off faster than the first cut of this curve did:
// roughly 92% under ten metres, 85% at twenty, 70% at thirty, 55% at forty. The
// original 0.75 coefficient over a six-metre dead zone was still handing a
// twenty-five metre ball 0.86, which only looked reasonable while the rating
// *added* this term and drowned it. Now that the rating multiplies by it the
// term decides, so it is worth having it match the real shape.
inline float GetPassExecutionOdds(float distance_m) {
  const float t = Clamp01((distance_m - 4.0f) / 46.0f);
  return 1.0f - 0.85f * t * t;
}

// Rough flight time for a pass of this length, in seconds.
inline float GetPassFlightTime_sec(float distance_m) {
  return 0.3f + distance_m * 0.05f;
}

// How far ahead of a moving receiver to aim, expressed as seconds of his current
// movement.
//
// The aim used to saturate at 0.7 s of movement however long the ball was in the
// air, on the reasoning that the receiver has stopped by then. Past about
// fourteen metres that assumption silently became a systematic error: a
// forty-metre pass is airborne for over two seconds, so a man still running at
// six metres a second was led by four metres and met the ball five or more
// behind him. Nobody touched those at all - not the target, not an opponent -
// which is what the ghost counters were catching.
//
// Leading by the full flight time at full speed is the opposite mistake, and was
// tried and reverted: a receiver who does check his run then watches it sail
// past. So the lead tracks real flight time but assumes he sheds most of his
// pace as the ball travels, which is what a player expecting it actually does.
inline float GetReceiverLeadTime_sec(float distance_m) {
  return GetPassFlightTime_sec(distance_m) * 0.6f;
}

// Lane pick for a panic clearance. The controller probes the default away
// direction and its two neighbours, and the best-scoring lane wins - unless
// every lane is hopeless, in which case the default desperate clearance
// stands (index 1).
inline int GetSafestPanicLane(const float odds[3], float panicLaneFloor = 0.15f) {
  int best = 0;
  for (int i = 1; i < 3; i++)
    if (odds[i] > odds[best]) best = i;
  if (odds[best] < panicLaneFloor) return 1;
  return best;
}

// How much of a hard incoming ball's momentum the receiver scrubs off. The
// bumpyRide blend preserves incoming momentum on a mistimed touch, which is
// fair for a jogged pass but absurd for a rocket: a slightly late touch kept
// nearly the full incoming speed and rolled away - the dominant bad-trap
// failure. Damping ramps in above jogging pace; soft passes are untouched.
inline float GetTrapKillStrength(float incomingBallSpeed) {
  return 0.5f * Clamp01((incomingBallSpeed - 6.0f) / 14.0f);
}

// Applied AFTER the 0..1 difficulty clamps, so a saturated trap (fast ball,
// far offset - exactly the tight-web case) still gets eased instead of being
// swallowed by the clamp. Never amplifies and never negates.
inline void ApplyTrapPredictionAssist(float& distanceFactor, float& heightFactor,
                                      float& ballMovementFactor, float supportDistance) {
  const float assist = GetTrapPredictionAssist(supportDistance);
  distanceFactor *= assist;
  heightFactor *= assist;
  ballMovementFactor *= assist;
}

// How open the game is. The stock engine only let a player shoot inside a tight
// window near the goal, which produced three or four shots a match; these two
// knobs open that up and are tunable from the config.
inline float GetShootingRange(const blunted::Properties& config) {
  return blunted::clamp(config.GetReal("gameplay_shooting_range", 21.0f), 12.0f, 45.0f);
}

inline float GetShotAppetite(const blunted::Properties& config) {
  return blunted::clamp(config.GetReal("gameplay_shot_appetite", 1.9f), 0.5f, 2.5f);
}

// Whether a keeper goes for a shot at all.
//
// The stock engine always played the save animation and almost nothing went in,
// so this roll was introduced to beat him sometimes - but at 0.32 sharpness it
// meant an average keeper tried for fewer than one shot in four and stood
// watching the rest, which is not a keeper being beaten, it is a keeper not
// playing. He goes for nearly everything now; whether he *reaches* it is for the
// save itself to decide, since the animation search only accepts a save that can
// actually get to the ball. Reaction still separates keepers, by a shade rather
// than by whether they bother.
inline float GetKeeperSaveChance(const blunted::Properties& config, float reactionStat) {
  const float sharpness =
      blunted::clamp(config.GetReal("gameplay_keeper_sharpness", 0.88f), 0.2f, 1.0f);
  const float reaction = blunted::clamp(reactionStat, 0.0f, 1.0f);
  return blunted::clamp(sharpness * (0.80f + reaction * 0.20f), 0.05f, 0.99f);
}

// Distance remains the primary fatigue input. This workload factor makes
// jogging slightly cheaper and repeated sprinting slightly more expensive.
inline float GetFatigueWorkloadFactor(float movementSpeed, float maximumSpeed, bool carryingBall) {
  if (maximumSpeed <= 0.0f)
    return 1.0f;

  const float speedRatio = Clamp01(movementSpeed / maximumSpeed);
  const float sprintLoad = Clamp01((speedRatio - 0.55f) / 0.45f);
  float workload = 0.90f + sprintLoad * sprintLoad * 0.28f;
  if (carryingBall)
    workload += sprintLoad * 0.04f;
  return workload;
}

inline bool IsGoalMouthThreat(float lateralPosition, float ballHeight, float goalHalfWidth,
                              float goalHeight, float anticipationFactor) {
  const float anticipation = std::max(anticipationFactor, 1.0f);
  const float anticipatedHalfWidth = goalHalfWidth * anticipation;
  const float anticipatedHeight = goalHeight + 0.11f + (anticipation - 1.0f) * 0.25f;
  return std::fabs(lateralPosition) <= anticipatedHalfWidth && ballHeight >= 0.0f &&
         ballHeight <= anticipatedHeight;
}

}  // namespace GameplayTuning

#endif  // _HPP_GAMEPLAY_TUNING

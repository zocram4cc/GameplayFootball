// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");

#ifndef _HPP_GAMEPLAY_TUNING
#define _HPP_GAMEPLAY_TUNING

#include <algorithm>
#include <array>
#include <cmath>

#include "base/math/bluntmath.hpp"
#include "base/properties.hpp"
#include "gametypes.hpp"

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

// How aggressive the side is set up to be, 0 containing to 1 all-out, from the
// three live sliders a manager actually moves: how high it plays
// (position_offense_depth_factor), how hard it drives with the ball
// (dribble_offensiveness), and whether it breaks early or waits
// (counter_attack). Equal thirds - no single slider IS the tactic, and a pack
// that sets only one of them still reads as leaning that way.
inline float GetTacticalOffensiveness(float offenseDepth, float dribbleOffensiveness,
                                      float counterAttack) {
  return (Clamp01(offenseDepth) + Clamp01(dribbleOffensiveness) + Clamp01(counterAttack)) / 3.0f;
}

// What that aggression is worth to the shot trigger. Team tactics reached shot
// volume nowhere before this: appetite was player skills x player style x one
// global knob, so a side set up to contain took its chances exactly as readily
// as one chasing the game, and a 4cc pack's tactical export said nothing about
// how often it shot. Measured at 7.96 shots a team with the tactic absent; the
// owner wants 8-14 "depending on the aggressiveness of the tactic", so the
// spread is +-30% around a neutral 0.5 that leaves the measured balance alone.
// Bounded by construction: no tactic can silence a side or let it shoot from
// anywhere.
constexpr float kTacticalShotAppetiteSpread = 0.6f;
inline float GetTacticalShotAppetite(float offensiveness) {
  return 1.0f + (Clamp01(offensiveness) - 0.5f) * kTacticalShotAppetiteSpread;
}

// Where a struck ball should cross the line, in metres. The strike is aimed
// ballistically at a band rather than left to the scatter: below the band the
// ball dies on the turf short of goal (measured: 10 of 22 shots crossed the
// line plane BELOW GROUND - flat strikes from the 27 m median distance, where
// gravity alone drops the ball 8.9 m), above it goes over the bar.
constexpr float kShotArrivalMin_m = 0.3f;
constexpr float kShotArrivalMax_m = 2.1f;
constexpr float kGravity_mps2 = 9.81f;

// The vertical launch speed that puts the ball at `arrivalHeight_m` when it
// reaches the goal line: h = z0 + vz*t - g*t^2/2, so vz = (h - z0)/t + g*t/2.
// Pure, because the band is the difference between a shot that arrives and one
// that dies on the turf (measured: 10 of 22 crossings below ground before it
// existed) and it deserves a test rather than a comment.
inline float ShotArrivalVelocityZ(float arrivalHeight_m, float startZ_m, float flightTime_s) {
  const float t = std::max(0.01f, flightTime_s);
  return (arrivalHeight_m - startZ_m) / t + 0.5f * kGravity_mps2 * t;
}

// The band a strike's vertical speed is held inside, so the ball crosses the
// line between kShotArrivalMin_m and kShotArrivalMax_m. Returns `vz` untouched
// when the shot is not to the goal line at all (no flight time to speak of).
inline float ClampShotArrivalVelocityZ(float vz, float startZ_m, float flightTime_s) {
  if (flightTime_s <= 0.05f) return vz;
  const float bandMin = ShotArrivalVelocityZ(kShotArrivalMin_m, startZ_m, flightTime_s);
  const float bandMax = ShotArrivalVelocityZ(kShotArrivalMax_m, startZ_m, flightTime_s);
  return std::max(bandMin, std::min(vz, bandMax));
}

// Where the ball actually crossed the goal-line plane, found from two
// consecutive physics positions rather than from the launch sum. The projection
// above answers "was this aimed on target" - and since the band clamps the
// vertical term against that same formula, the projection cannot disagree with
// it. This one answers "did it get there", and it differs by everything the
// projection leaves out: drag, spin, deflections, the keeper's hand. It is the
// only on-target number that can confirm the band instead of restating it.
struct GoalLineCrossing {
  bool crossed = false;
  float lateral_m = 0.0f;  // y where the ball met the plane
  float height_m = 0.0f;   // z where the ball met the plane
};

// `prev` and `cur` are consecutive positions of the ball, 10 ms apart or less.
// Crossing means the two straddle |x| = pitchHalfW: one inside, one outside.
// A ball that is already outside on both steps (settling in the net, behind the
// line after a goal) has not crossed in this step and is not counted again.
inline GoalLineCrossing FindGoalLineCrossing(const std::array<float, 3>& prev,
                                             const std::array<float, 3>& cur, float pitchHalfW) {
  GoalLineCrossing out;
  const float pa = std::fabs(prev[0]), ca = std::fabs(cur[0]);
  if ((pa < pitchHalfW) == (ca < pitchHalfW)) return out;  // both same side
  const float t = (pitchHalfW - pa) / (ca - pa);
  if (t < 0.0f || t > 1.0f) return out;
  out.crossed = true;
  out.lateral_m = prev[1] + (cur[1] - prev[1]) * t;
  out.height_m = prev[2] + (cur[2] - prev[2]) * t;
  return out;
}

// Inside the posts and under the bar, as measured - not as aimed. The ball's
// physics keeps its centre at or above ground level (`ballRadius`), so there is
// no "crossed below ground" case to test for: a shot that dies on the turf
// never reaches the plane at all, and that shows up as no crossing.
inline bool CrossedInsideGoalFrame(const GoalLineCrossing& cross, float goalHalfWidth_m,
                                   float goalHeight_m) {
  return cross.crossed && std::fabs(cross.lateral_m) < goalHalfWidth_m &&
         cross.height_m < goalHeight_m;
}

// The goal-line plane the goal test uses: the line's own half-width plus the
// inset PES's goal frame sits at. `CheckForGoal`'s triangles are built on it,
// so anything that asks "did the ball cross" has to ask at the same x or it
// answers about a different plane 17 cm away - enough to call a ball angled in
// across the post wide while the goal test calls it a goal.
// (`const`, not `constexpr`: `lineHalfW` in gametypes.hpp is a plain
// `const float`, so a constexpr initialiser would not compile.)
const float kGoalPlaneOffset_m = lineHalfW + 0.11f;

// A shot is struck from inside this range and no further. The engine's own
// shooting range knob maxes out at 45 m, so extrapolating a keeper's touch from
// further away than that is describing a pass, not a shot at goal.
constexpr float kShotRangeCap_m = 45.0f;

// Where the ball *would* meet the plane, from its position and velocity now.
// Used where the ball is stopped before it gets there - the keeper's hand, a
// block - because "was this heading into the frame" is the only way to ask
// about a shot that never arrived. It reads the ball's real state at the moment
// of contact, so unlike the launch projection it carries the flight that
// actually happened: drag, spin and any deflection up to that point.
//
// No drag in the extrapolation, so over the last few metres at the moment of a
// touch the error is small; it is not a long-range predictor and must not be
// used as one.
inline GoalLineCrossing PredictGoalLineCrossing(const std::array<float, 3>& pos,
                                                const std::array<float, 3>& vel,
                                                float pitchHalfW) {
  GoalLineCrossing out;
  // Which goal it is heading for comes from the VELOCITY, not from which half
  // the ball happens to be in: a ball in its own half travelling at the far goal
  // is heading for that goal, and taking the side from `pos[0]` reads its
  // direction as backwards and reports no crossing at all.
  if (std::fabs(vel[0]) <= 0.01f) return out;
  const int side = (vel[0] > 0.0f) ? 1 : -1;
  const float distance = pitchHalfW - pos[0] * side;
  if (distance <= 0.0f) return out;   // already level with, or past, that line
  if (distance > kShotRangeCap_m) return out;  // no shot is struck from here
  const float t = distance / std::fabs(vel[0]);
  if (t > 4.0f) return out;  // and at this pace it is a pass, not a shot
  out.crossed = true;
  out.lateral_m = pos[1] + vel[1] * t;
  out.height_m = pos[2] + vel[2] * t - 0.5f * kGravity_mps2 * t * t;
  return out;
}

// A chance is worth shooting at when the same xG model that scores the stats
// says so: 0.04 is a speculative hit (roughly a 27 m strike through traffic),
// not a prohibition on long shots. Below that the ball is better kept, so the
// trigger declines it and the caller falls through to the pass.
//
// Config-tunable beside the range and the appetite, because it is the knob
// that decides WHERE chances are taken from and that is the whole balance
// question: measured over a match, every single shot came from outside 18 m
// (bands 0/0/4/3 and 0/0/4/6), because the trigger fires the moment a player
// enters the shooting range and he never carries the ball closer. Raising the
// bar declines the 25 m speculation and the caller falls through to the pass.
constexpr float kMinShotXg = 0.04f;
inline float GetMinShotXg(const blunted::Properties& config) {
  return blunted::clamp(config.GetReal("gameplay_min_shot_xg", kMinShotXg), 0.01f, 0.30f);
}
// There is no second "is it worth shooting" predicate: the trigger asks once,
// with this bar over the player's appetite (ElizaController), and the aim
// function no longer asks at all. Two gates on the same question, at two
// different values, is what aimed a shot at the keeper's chest.

// How long a controller lags behind the world: 40 ms for a perfect stat, 80 ms
// for none. Outfielders feed physical_reaction, a keeper his GK Reflexes.
inline int GetReactionTime_ms(float reactionStat) {
  return int(std::round(80.0f - Clamp01(reactionStat) * 40.0f));
}

// The five PES goalkeeper attributes, each anchored so that the stock value
// (the engine's 0.6 default) reproduces the behaviour the engine shipped with.

// GK Awareness: how far ahead he reads the ball when picking his base position.
inline unsigned int GetKeeperAnticipation_ms(float awareness) {
  return static_cast<unsigned int>(300.0f + Clamp01(awareness) * 500.0f);
}

// GK Awareness: how much wider than the goal he treats an incoming ball as a
// threat (1.0 = the real goal mouth). Poor awareness overreacts to balls going
// wide; the stock formula blended defensive positioning and vision to 1.22.
inline float GetKeeperGoalMouthPanic(float awareness) {
  return 1.02f + (1.0f - Clamp01(awareness)) * 0.5f;
}

// GK Coverage (PES "GK Reach"): how far off his line he stands with the ball
// in front of him, and how much of a head start on his own defenders an
// attacker needs before he comes rushing out.
inline float GetKeeperComeOutBias(float coverage) {
  return 0.15f + Clamp01(coverage) * 0.25f;
}
inline float GetKeeperComeOutMargin_m(float coverage) {
  return 1.6f - Clamp01(coverage);
}

// GK Coverage (PES "GK Reach"), as a distance: how far from his standing
// position he still gets a hand to the ball. A dive, not a step - PES's own
// keepers reach roughly their own height at full stretch, and the weakest
// barely leave their feet.
// How much of the WORST-case strike ends up in a shot: 0 is exactly where he
// aimed, 1 is the mishit. The stock curve was random(0,1) ^ (technical_shot *
// 0.7), which even at a perfect 0.99 has a mean of 0.59 - the best finisher in
// the game blended six parts mishit to four parts intention, and 80% of shots
// missed the target (measured: 20 shots, 4 on target, 1.28 xG per match). The
// medals have to mean something: a gold 4cc player (every attribute 0.99)
// should place his shot, a bronze (0.88) nearly, and a poor finisher spray it.
//
// mean weight = 1 / (1 + exponent), so:
//   stat 0.99 -> exponent 16.7 -> 6% mishit
//   stat 0.91 -> exponent  6.9 -> 13%
//   stat 0.88 -> exponent  5.7 -> 15%
//   stat 0.60 -> exponent  2.3 -> 30%
//   stat 0.00 -> exponent  1.0 -> 50%
inline float GetShotWorstCaseWeight(float uniformRandom01, float shotStat) {
  const float exponent = 1.0f / std::max(0.06f, 1.0f - Clamp01(shotStat) * 0.95f);
  return std::pow(blunted::clamp(uniformRandom01, 0.0f, 1.0f), exponent);
}
// How far off the paint the chosen corner lands, in metres of open half-mouth
// pulled back toward the middle: 0 is the paint, `openHalfMouth_m` is the
// middle. A gold medal dares the paint whatever the draw; a poor finisher
// sprays toward the middle, draw by draw. Pure, so the sweep and the corner
// pick share it: the veto picks the SIDE, this sets the DEPTH, and the offset
// never exceeds the open half-mouth, so the aim stays inside the frame.
inline float GetShotPlacementOffset_m(float uniformDraw01, float shotStat, float openHalfMouth_m) {
  const float draw = blunted::clamp(uniformDraw01, 0.0f, 1.0f);
  const float skill = Clamp01(shotStat);
  return draw * (1.0f - skill) * openHalfMouth_m;
}

// How much random swerve a struck ball carries, as a share of the spin the
// animation carries. The stock factor was 0.3 + worstCaseFactor * 0.7, whose
// 0.3 floor applied even to a perfect strike - and the spin plan was never
// wired (the code says so itself: "use curve as actual planned thing, not
// random"), so that swerve was never intention.
//
// One finishing stat prices both halves of the strike: GetShotWorstCaseWeight
// places it, this steadies it in flight. A gold medal flies near-true, a poor
// finisher wobbles.
inline float GetShotCurveNoise(float shotStat) {
  return 0.05f + (1.0f - Clamp01(shotStat)) * 0.95f;
}

// Striking across the body turns the ball off the outside of the boot, and the
// spin that comes with it bends the flight further the same way. That is real,
// and it is the largest DETERMINISTIC source of shots that miss the frame: the
// swing reaches a quarter radian at a right angle, three metres wide of where
// it was aimed from twenty. It is also the only error in the strike the stock
// code never asked the finisher about - a 0.99 striker sliced a half-turned
// volley exactly as far as a centre back did, while every other term here
// already scales with technique. Keeping the boot face square IS the skill; it
// is never squared completely, so the floor stays.
constexpr float kShotBodySliceFloor = 0.15f;
inline float GetShotBodySliceShare(float shotStat) {
  return kShotBodySliceFloor + (1.0f - kShotBodySliceFloor) * (1.0f - Clamp01(shotStat));
}

// Does he get a hand to it? The geometry of the save, kept pure so it can be
// tested: the gap he has to close, the time the ball gives him, the latency his
// reflexes cost, his reach at full stretch and the speed he closes the rest at.
//
// A keeper at zero flight time still has a body. The reach behind the ball is
// what he can stop WITHOUT MOVING plus what the dive adds, and only the dive
// needs time. The old form scaled the whole reach by min(1, timeToLine/latency)
// and carried a `kKeeperReachExtensionShare` multiplier that was already 1.0 -
// so at point blank it left him with almost nothing, and every corner-placed
// shot from inside the box was a goal. Measured at 1x, bar 0.18: 12 of 16
// on-target became goals on one seed and 8 of 13 on the next, the keeper
// saving ~30% of what reached him where a real one saves ~70%.
//
// Zero flight means no dive, not an empty goal. The standing reach is his
// frame with his arms in it, consistent with the 0.9 m "hands rest at hip
// height" the gap is measured against in Player::KeeperAttemptsSave.
constexpr float kKeeperStandingReach_m = 1.0f;
inline float KeeperEffectiveReach_m(float reach_m, float timeToPlane_s, float latency_s) {
  if (latency_s <= 0.0f)
    return reach_m;
  const float extend = std::min(1.0f, std::max(0.0f, timeToPlane_s / latency_s));
  // Only the part of a dive that exceeds his standing frame needs flight time;
  // the worst keeper's dive reach is below it, so he never extends at all
  // rather than reaching backwards. Full flight still leaves the reach
  // untouched.
  return kKeeperStandingReach_m + std::max(0.0f, reach_m - kKeeperStandingReach_m) * extend;
}

inline bool KeeperReachesShot(float gap_m, float timeToPlane_s, float latency_s, float reach_m,
                              float closingSpeed_ms) {
  const float travel_m =
      std::max(0.0f, timeToPlane_s - latency_s) * std::max(0.0f, closingSpeed_ms);
  return gap_m <= reach_m + travel_m;
}

// His hands, in metres, at rest. The gap is measured against this rather than
// against his body's centre.
constexpr float kKeeperHandsHeight_m = 0.9f;

// What a shot asks of the keeper: how far he must get and how long he has.
// PURE, and deliberately so - the keeper is geometry over (gap, time, latency,
// reach) and nothing else, so his save rate can be swept directly instead of
// being read out of 40-minute matches where the attack's own variance is an
// order of magnitude larger than the effect being measured.
//
// The plane is the one he can actually get a hand to: HIS OWN when he is
// between the ball and the goal line, the line otherwise. Sampling where the
// ball meets the LINE and comparing that to a keeper standing metres off it
// charges him for the ball's lateral travel over his own offset - which is
// what `out.planeX` is here to make visible.
struct KeeperSaveChallenge {
  bool shotAtGoal = false;  // false: the ball is not coming at him at all
  float gap_m = 0.0f;
  float timeToPlane_s = 0.0f;
  // The two halves of gap_m. They are different defects: lateral_m is where
  // he was standing, overhead_m is how high the strike was. `gap_m` alone
  // cannot say which one to fix, so the census records both.
  float lateral_m = 0.0f;
  float overhead_m = 0.0f;
  float planeX = 0.0f;
};

inline KeeperSaveChallenge GetKeeperSaveChallenge(const std::array<float, 3>& ballPos,
                                                  const std::array<float, 3>& ballVel,
                                                  const std::array<float, 3>& keeperPos,
                                                  float lineX) {
  KeeperSaveChallenge out;
  const float closingSpeed = ballVel[0] * (lineX > 0.0f ? 1.0f : -1.0f);
  if (closingSpeed <= 0.1f) return out;  // not coming at him: his to collect
  const bool keeperIsInFront =
      ((keeperPos[0] - ballPos[0]) * (lineX - ballPos[0]) > 0.0f) &&
      (std::fabs(keeperPos[0] - ballPos[0]) < std::fabs(lineX - ballPos[0]));
  out.planeX = keeperIsInFront ? keeperPos[0] : lineX;
  out.timeToPlane_s = std::fabs(out.planeX - ballPos[0]) / closingSpeed;
  const float crossingY = ballPos[1] + ballVel[1] * out.timeToPlane_s;
  const float crossingZ = std::max(0.0f, ballPos[2] + ballVel[2] * out.timeToPlane_s);
  out.shotAtGoal = true;
  out.lateral_m = std::fabs(crossingY - keeperPos[1]);
  out.overhead_m = std::max(0.0f, crossingZ - kKeeperHandsHeight_m);
  out.gap_m = std::sqrt(std::pow(out.lateral_m, 2.0f) + std::pow(out.overhead_m, 2.0f));
  return out;
}

inline float GetKeeperDiveReach_m(float coverage) {
  return 0.9f + Clamp01(coverage) * 1.5f;
}

// A keeper does not close the gap at a sprint: he reads, sets and dives, and
// what matters is how fast the dive travels sideways. Calibrated against the
// shots a viewer expects to go in - a 20 m drive at 25 m/s gives 0.8 s, in
// which the best keeper covers 5.7 m (reach included, so the far corner is
// his) and the worst 2.5 m (so it is not).
inline float GetKeeperDiveSpeed_ms(float coverage) {
  return 3.5f + Clamp01(coverage) * 2.0f;
}

// His own reaction, not the controller's tick: 350 ms for a keeper with no
// reflexes, 200 ms for the best, which is where human goalkeepers actually
// sit. GetReactionTime_ms is the input lag of the controller and is far too
// short to stand in for this.
inline float GetKeeperReactionTime_s(float reflexes) {
  return 0.35f - Clamp01(reflexes) * 0.15f;
}

// GK Catching, as the fastest ball he can HOLD rather than parry, in m/s
// relative to himself. Anything quicker is pushed away (GetKeeperParryPush,
// which is GK Clearing). The stock threshold was a product of two normalised
// difficulties against 0.45-0.25*catching, which no fast shot could clear at
// any stat - every real shot was parried, and that is the fumbling.
inline float GetKeeperCatchSpeed_ms(float catching) {
  return 12.0f + Clamp01(catching) * 16.0f;
}

  // (Removed: GetKeeperCatchThreshold, the 0.45-0.25*catching product the old
  // hold/parry test compared two normalised difficulties against. No shot could
  // clear it at any attribute value, so play parried everything. GetKeeperCatchSpeed_ms
  // above is the replacement: catching expressed as a speed in m/s.)

// GK Clearing: how hard a parry is pushed away from goal, in the same units as
// the stock 4.0 forward component of the deflect touch.
inline float GetKeeperParryPush(float clearing) {
  return 2.2f + Clamp01(clearing) * 3.0f;
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

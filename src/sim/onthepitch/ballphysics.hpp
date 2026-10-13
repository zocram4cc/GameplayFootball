#ifndef _HPP_FOOTBALL_ONTHEPITCH_BALLPHYSICS
#define _HPP_FOOTBALL_ONTHEPITCH_BALLPHYSICS

#include "base/math/vector3.hpp"

struct BallPhysicsState {
  blunted::Vector3 position;
  blunted::Vector3 momentum;
};

struct GoalNettingConfig {
  float pitchHalfW = 52.5f;  // gametypes.hpp pitchHalfW: 105x68 pitch since 8163845
  float goalDepth = 2.55f;
  float goalHeight = 2.5f;
  float goalHalfWidth = 3.7f;
  float ballRadius = 0.11f;
};

struct GoalNettingResult {
  bool touchedNet = false;
};

// Keeps a ball that has crossed the goal line inside the net. Whichever panel it
// reaches first - side, rear or top - gets a soft, spring-like deceleration first,
// so a normal shot looks caught rather than stopped dead, and then a hard clamp at
// that panel: without the clamp, a shot fast enough loses only a sliver of its
// speed each 10ms step and can cross the net's own ~2.5m depth before the spring
// has done anything, coming out the far side of the mesh instead of settling in
// it. That is what a full-power penalty did every time; a normal shot is slow
// enough that the spring alone usually looked like it caught it. `ballIsInGoal`
// gates the whole thing off until the ball has actually crossed the goal-mouth
// plane (`state.position` is read as this tick's starting position, exactly like
// the post and crossbar checks it sits beside in Ball::CalculatePrediction).
GoalNettingResult ApplyGoalNettingCollision(BallPhysicsState& state, bool ballIsInGoal,
                                            const GoalNettingConfig& config, float timeStep_s);

// How far Cloth::Push reaches when the ball presses into goal netting. Has to
// clear the mesh's own point spacing or the ball can pass within its own radius
// (0.11m) of every point without ever being close enough to move one - which is
// what made contact look like it did nothing to the net. The imported net settles
// to roughly one point every 0.35m (PrepareGoalNetting logs "553 net point(s)"
// per goal over its ~63 sq. m of side/rear/top netting), so the radius needs to
// clear half that spacing at minimum; this clears it with room for a couple of
// neighbours to move together, which is what makes the give visible.
constexpr float kNettingPushRadius_m = 0.4f;

#endif

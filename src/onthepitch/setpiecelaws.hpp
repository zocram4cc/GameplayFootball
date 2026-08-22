#ifndef _HPP_ONTHEPITCH_SETPIECELAWS
#define _HPP_ONTHEPITCH_SETPIECELAWS

#include "gametypes.hpp"

// The parts of the laws that say where players may stand at a restart.
//
// Law 16: at a goal kick the opponents stay outside the penalty area until the ball is
// in play - and until they have, the kick is not taken. Nothing enforced that here: a
// striker standing in the six-yard box when the kick was awarded stayed there, and a
// goal kick could be taken straight to his feet.
//
// The clearing is expressed as a place to walk to rather than a place to be teleported:
// the players are given it as a destination and run there, and the taker waits.
namespace SetPieceLaws {

// The penalty area, as the pitch is actually painted (proceduralpitch.cpp): 16.5 m deep
// and 20.15 m either side of the centre line.
constexpr float kAreaDepth = 16.5f;
constexpr float kAreaHalfWidth = 20.15f;

// How far outside the line a cleared player is sent. Standing exactly on it is legal
// and looks like cheating, and a man drifting a step back would be inside again.
constexpr float kClearanceMargin = 1.5f;

// Whether a restart requires the opponents to clear the penalty area.
//
// A goal kick always does (Law 16). So does a free kick taken from inside the
// defending team's own area (Law 13) - which is what an offside given deep in defence
// produces, and the case that let a restart be stolen and scored: the flagged attacker
// simply stood where the flag went up.
bool ClearsThePenaltyArea(e_SetPiece setPiece, float restartX = 0.0f, int takerSide = 0,
                          float pitchHalfW = 0.0f);

// Whether `position` is inside the penalty area on `side` of the pitch (-1 or +1, the
// side that team defends).
bool InsidePenaltyArea(float x, float y, int side, float pitchHalfW);

// Where a player standing inside that area should go: straight out of the nearest edge,
// which is what a defender ambling out of the box actually does. Returns (x, y).
void ClearingTarget(float x, float y, int side, float pitchHalfW, float* outX, float* outY);

// Law 13: at a free kick or a corner the opponents stand at least 9.15 m from the ball
// until it is in play. Nobody enforced this either, so a defender could stand on the
// ball and block the kick from a metre away.
constexpr float kRetreatRadius = 9.15f;

// Whether a restart requires the opponents to retreat from the ball.
bool ClearsTheBallRadius(e_SetPiece setPiece);

// Whether a player is closer to the ball than the law allows.
bool InsideBallRadius(float x, float y, float ballX, float ballY);

// Where a player standing too close should go: straight out along the line from the
// ball, which is a defender backing off rather than running round. Returns (x, y).
void RetreatTarget(float x, float y, float ballX, float ballY, float* outX, float* outY);

// Whether the taker may kick. `intruders` is how many opponents are still inside the
// area or the radius, `waited_ms` how long the referee has been holding the restart: the wait is
// bounded so a stuck player cannot freeze the match.
constexpr unsigned long kMaxWait_ms = 6000;

bool MayRestart(int intruders, unsigned long waited_ms);

}  // namespace SetPieceLaws

#endif

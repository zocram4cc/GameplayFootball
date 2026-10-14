#ifndef _HPP_ONTHEPITCH_SETPIECELAWS
#define _HPP_ONTHEPITCH_SETPIECELAWS

#include <cmath>
#include <cstdlib>

#include "gametypes.hpp"
#include "pessetpiecetaker.hpp"

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

// The penalty area and the goal area as PES's FieldInfo has them (F6/F7, F4/F5),
// measured from GF's goal line.
const float kAreaDepth = pitchHalfW - pesPenaltyAreaLineX;
const float kAreaHalfWidth = pesPenaltyAreaHalfWidth;
const float kGoalAreaDepth = pitchHalfW - pesGoalAreaLineX;
const float kGoalAreaHalfWidth = goalAreaHalfWidth;  // F5

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

// How far beyond the line an opponent must be before the taker will kick.
//
// Being out of the area is not the same as being out of the way. Recorded in a match:
// the opponents walked out and the kick went off the moment the last of them crossed
// the line, with him standing on it, half a step from where he started - the clearing
// happened and bought nothing.
//
// So the taker's gate is the area grown by this much. It is smaller than
// kClearanceMargin on purpose: the players walk to the line plus that, and a gate as
// wide as their destination would be satisfied only if they overshot it.
constexpr float kGateMargin = 1.0f;

// Whether an opponent is close enough to the area to hold the restart up.
bool IntrudesOnPenaltyArea(float x, float y, int side, float pitchHalfW);

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

// Whether he is close enough to it to hold the restart up (see kGateMargin).
bool IntrudesOnBallRadius(float x, float y, float ballX, float ballY);

// Where a player standing too close should go: straight out along the line from the
// ball, which is a defender backing off rather than running round. Returns (x, y).
void RetreatTarget(float x, float y, float ballX, float ballY, float* outX, float* outY);

// Whether the taker may kick. `intruders` is how many opponents are still inside the
// area or the radius, `waited_ms` how long the referee has been holding the restart: the wait is
// bounded so a stuck player cannot freeze the match.
constexpr unsigned long kMaxWait_ms = 6000;

bool MayRestart(int intruders, unsigned long waited_ms);

// How long PES 2021's set-piece AI holds a restart before the kicker takes it
// (docs/re/pes-re-setpiece-choreography.md 9 item 2).
//
// Its ThinkUnitSetPlay counts elapsed logic frames from the restart's arm
// (`MI+0x1198` minus the AI context's stored base) and crosses once the count
// reaches a frame-rate multiple: a corner at fps x 3.5, a free kick at fps x 3,
// a penalty at fps. Measured live at 54 Hz, counted from the set-play counter's
// first frame: a corner at 189, a free kick at 162, a penalty at 54, and the
// 379-frame corner is the 189-frame window's second pass.
//
// The kick-off is deliberately not in this list: PES's AI passes it at counter
// 29 of the receiver's loop, released by the receiver's own ball-ownership
// test, and gating it on a wait would make it slower rather than faithful.
// Throw-ins and goal kicks likewise have their own release - the taker's
// arrival and readiness - so they are not fenced either.

// Whether `elapsedFrames` logic frames of waiting have passed the gate. `fps`
// is the logic frame rate (54 in PES); a non-positive rate is treated as 54.
// `k` are the frame-rate multiples read from the install's exe.
bool MayTakeRestart(e_SetPiece setPiece, int elapsedFrames, float fps,
                    const pes::SetPieceTakerConstants& k);

// Whether `elapsedFrames` is still inside the short-corner window: fps x 2
// seconds (fps x 7 when a team-mate is already running the corner), inside
// which PES may play the short corner instead once the partner check passes.
// The two live short corners went at 57 and 78 frames, both inside it.
bool InShortCornerWindow(int elapsedFrames, int windowFrames);

// An offside is staged like a foul: the flag, the complaint, and only then the
// restart. It kept upstream's two seconds to prepare and four to take while
// starting a seven-second choreography over the top, so ResetSituation
// teleported everyone into their restart positions two seconds in and the
// whistle went at four - with three seconds of choreography still posing the
// cast in live play. A cast opponent standing on the ball is then the nearest
// man to it, so he is the taker the keeper is waiting for, and he cannot move
// (Match::UpdateCutsceneChoreo feeds him until the clip ends).
//
// A player down adds PES's +2.5 s (9.3), which is what makes an offside during
// a scramble cost 5 s rather than 2.5.

// The half-time whistle is staged the same way: the walk-off plays, then the
// pitch is reset behind the wipe for the second-half kick-off. The referee gave
// it upstream's three seconds to prepare and five to take while
// Match::SetMatchPhase started a six-second "timeup" cutscene over the top, so
// the reset teleported everyone to their kick-off marks - in plain view, under a
// camera still filming the walk-off - halfway through the shot.
constexpr unsigned long kHalfTimeCutscene_ms = 10000;
constexpr unsigned long kHalfTimePrepareAfterCutscene_ms = 500;
constexpr unsigned long kHalfTimeTakeAfterPrepare_ms = 2000;

inline unsigned long HalfTimePrepareAt_ms(unsigned long whistleTime_ms) {
  return whistleTime_ms + kHalfTimeCutscene_ms + kHalfTimePrepareAfterCutscene_ms;
}

inline unsigned long HalfTimeTakeAt_ms(unsigned long whistleTime_ms) {
  return HalfTimePrepareAt_ms(whistleTime_ms) + kHalfTimeTakeAfterPrepare_ms;
}

// Which restart a dead ball is owed, and from where.
//
// Pure, because it is a decision about the pitch and not about the match around
// it, and because the case that made this worth having cannot be reached any
// other way: a match that stops play without arming anything used to stand
// there until the period ended, four times in one showcase, and nothing in a
// test could have shown it because the decision was buried in a branch that
// only a live match reaches.
enum class e_DeadBallRestart { None, ThrowIn, Corner, GoalKick, KickOff };

struct DeadBallDecision {
  e_DeadBallRestart kind = e_DeadBallRestart::None;
  // Plain floats rather than a Vector3: this header is used by test targets that
  // do not link the vector maths, and a decision about the pitch should not drag
  // a vector library in behind it.
  float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
  // The team that takes it. Kick-offs are given by the team that conceded, so
  // the "other side" rule that decides a throw-in is not the rule there.
  int teamID = 0;
};

// `lastTouchSide` is the defending sign of the side that last touched the ball
// (+1 defends +x). `lastGoalTeamID` is -1 when no goal is outstanding. Teams are
// 0 and 1, so "the other side" is `abs(id - 1)`.
inline DeadBallDecision GetDeadBallRestart(float ballX, float ballY, float pitchHalfW,
                                           float pitchHalfH, int lastTouchTeamID,
                                           int lastTouchSide, int lastGoalTeamID) {
  DeadBallDecision out;
  const float x = ballX, y = ballY;
  if (std::fabs(y) > pitchHalfH) {
    out.kind = e_DeadBallRestart::ThrowIn;
    out.posX = x < -pitchHalfW + 0.6f ? -pitchHalfW + 0.6f
                                      : (x > pitchHalfW - 0.6f ? pitchHalfW - 0.6f : x);
    out.posY = y > 0 ? pitchHalfH : -pitchHalfH;
    out.teamID = abs(lastTouchTeamID - 1);
    return out;
  }
  if (std::fabs(x) > pitchHalfW) {
    // Last touch by the side defending that end gives them the corner.
    if ((x > 0) == (lastTouchSide > 0)) {
      out.kind = e_DeadBallRestart::Corner;
      out.posX = pitchHalfW * (lastTouchSide > 0 ? 1 : -1);
      out.posY = y > 0 ? pitchHalfH : -pitchHalfH;
      out.teamID = abs(lastTouchTeamID - 1);
    } else {
      out.kind = e_DeadBallRestart::GoalKick;
      out.posX = pitchHalfW * 0.92f * (lastTouchSide > 0 ? -1 : 1);
      out.teamID = abs(lastTouchTeamID - 1);
    }
    return out;
  }
  // Inside the pitch and nothing armed: the ball was reset to the centre spot
  // and the kick-off that should have followed it never did.
  out.kind = e_DeadBallRestart::KickOff;
  // the centre spot, already the zero the struct starts with
  out.teamID = lastGoalTeamID >= 0 ? abs(lastGoalTeamID - 1) : abs(lastTouchTeamID - 1);
  return out;
}

}  // namespace SetPieceLaws

#endif

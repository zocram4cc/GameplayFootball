#include "setpiecelaws.hpp"

#include <cmath>

namespace SetPieceLaws {

bool ClearsThePenaltyArea(e_SetPiece setPiece, float restartX, int takerSide,
                          float pitchHalfW) {
  if (setPiece == e_SetPiece_GoalKick)
    return true;
  // A free kick from inside your own area clears it too. A corner and a throw-in are
  // taken with everyone where they stand, and a penalty has its own arrangement that
  // the shootout controller handles.
  if (setPiece == e_SetPiece_FreeKick && takerSide != 0)
    return InsidePenaltyArea(restartX, 0.0f, takerSide, pitchHalfW);
  return false;
}

bool InsidePenaltyArea(float x, float y, int side, float pitchHalfW) {
  if (side == 0)
    return false;
  const float depthIn = (pitchHalfW - x * static_cast<float>(side));
  return depthIn >= 0.0f && depthIn <= kAreaDepth && std::fabs(y) <= kAreaHalfWidth;
}

void ClearingTarget(float x, float y, int side, float pitchHalfW, float* outX, float* outY) {
  *outX = x;
  *outY = y;
  if (!InsidePenaltyArea(x, y, side, pitchHalfW))
    return;

  // Two ways out: up the pitch over the 16.5 m line, or sideways over the 20.15 m one.
  // A player takes the shorter, which is what a defender ambling out of the box does.
  const float lineX = (pitchHalfW - kAreaDepth) * static_cast<float>(side);
  const float outwards = std::fabs(x - lineX);
  const float sideways = kAreaHalfWidth - std::fabs(y);
  if (outwards <= sideways) {
    *outX = lineX - static_cast<float>(side) * kClearanceMargin;
  } else {
    const float sign = y >= 0.0f ? 1.0f : -1.0f;
    *outY = sign * (kAreaHalfWidth + kClearanceMargin);
  }
}

bool ClearsTheBallRadius(e_SetPiece setPiece) {
  // A free kick and a corner both hold the opponents off the ball. A goal kick clears
  // the whole area instead, a throw-in has its own (much shorter) distance that this
  // engine has never modelled, and a kick-off keeps them out of the centre circle -
  // which PrepareSetPiece already arranges.
  return setPiece == e_SetPiece_FreeKick || setPiece == e_SetPiece_Corner;
}

bool InsideBallRadius(float x, float y, float ballX, float ballY) {
  const float dx = x - ballX, dy = y - ballY;
  return dx * dx + dy * dy < kRetreatRadius * kRetreatRadius;
}

void RetreatTarget(float x, float y, float ballX, float ballY, float* outX, float* outY) {
  *outX = x;
  *outY = y;
  if (!InsideBallRadius(x, y, ballX, ballY))
    return;
  float dx = x - ballX, dy = y - ballY;
  float length = std::sqrt(dx * dx + dy * dy);
  if (length < 1e-4f) {
    // Standing on the ball: any direction will do, so pick one rather than dividing by
    // nothing. Up the pitch is as good as any.
    dx = 1.0f;
    dy = 0.0f;
    length = 1.0f;
  }
  const float want = kRetreatRadius + kClearanceMargin;
  *outX = ballX + dx / length * want;
  *outY = ballY + dy / length * want;
}

bool MayRestart(int intruders, unsigned long waited_ms) {
  if (intruders <= 0)
    return true;
  // Bounded: a player wedged against the netting must not be able to stop the match.
  return waited_ms >= kMaxWait_ms;
}

}  // namespace SetPieceLaws

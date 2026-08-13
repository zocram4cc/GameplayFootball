#include "matchmentality.hpp"

#include <algorithm>

#include "gamedefines.hpp"

namespace MatchMentality {

namespace {

// Stay a couple of metres inside the markings; the corner flag itself is out of
// bounds for a player shielding the ball.
const float cornerInsetX = 4.0f;
const float cornerInsetY = 3.0f;

// Late-game momentum, added on top of the manager's own tactics rather than
// replacing them.
const float chaseMomentum = 0.25f;
const float holdMomentum = 0.25f;

}  // namespace

e_Mentality Decide(int goalDifference, unsigned long matchTime_ms) {
  if (goalDifference < 0 && matchTime_ms >= desperationStart_ms)
    return e_Mentality_Desperation;
  if (goalDifference > 0 && matchTime_ms >= timeWastingStart_ms)
    return e_Mentality_TimeWasting;
  return e_Mentality_Normal;
}

bool OverridesFormation(e_Mentality mentality) {
  return mentality == e_Mentality_Desperation;
}

FormationShape GetDesperationShape(int goalDifference) {
  FormationShape shape;
  if (goalDifference <= -2) {
    // Two or more down: all-out attack.
    shape.defenders = 4;
    shape.midfielders = 2;
    shape.forwards = 4;
  } else {
    // One down: an extra forward at the cost of a defender.
    shape.defenders = 3;
    shape.midfielders = 4;
    shape.forwards = 3;
  }
  return shape;
}

float GetOffensiveMomentum(e_Mentality mentality) {
  switch (mentality) {
    case e_Mentality_Desperation:
      return chaseMomentum;
    case e_Mentality_TimeWasting:
      return -holdMomentum;
    default:
      return 0.0f;
  }
}

float ApplyMomentum(float tacticValue, e_Mentality mentality, float weight) {
  const float scaled = GetOffensiveMomentum(mentality) * std::max(0.0f, std::min(weight, 1.0f));
  return std::max(0.0f, std::min(tacticValue + scaled, 1.0f));
}

bool ShouldStayInCorner(e_Mentality mentality, bool hasPossession) {
  return mentality == e_Mentality_TimeWasting && hasPossession;
}

CornerTarget GetCornerTarget(int teamSide, float playerY) {
  const float side = static_cast<float>(teamSide >= 0 ? 1 : -1);
  const float flank = playerY >= 0.0f ? 1.0f : -1.0f;

  CornerTarget target;
  target.x = -side * (pitchHalfW - cornerInsetX);
  target.y = flank * (pitchHalfH - cornerInsetY);
  return target;
}

}  // namespace MatchMentality

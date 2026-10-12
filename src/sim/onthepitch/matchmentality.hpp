// Reactive in-match mentality: chasing a deficit late on, or killing the game
// off when in front. See SIMULATION_IMPROVEMENT_PROPOSAL.md section 2B.

#ifndef _HPP_MATCH_MENTALITY
#define _HPP_MATCH_MENTALITY

namespace MatchMentality {

enum e_Mentality {
  e_Mentality_Normal = 0,
  e_Mentality_Desperation,
  e_Mentality_TimeWasting,
};

struct FormationShape {
  int defenders = 4;
  int midfielders = 4;
  int forwards = 2;
};

struct CornerTarget {
  float x = 0.0f;
  float y = 0.0f;
};

const unsigned long desperationStart_ms = 80UL * 60000UL;
const unsigned long timeWastingStart_ms = 85UL * 60000UL;

// `goalDifference` is own goals minus opponent goals; `matchTime_ms` is the
// running match clock (2700000 at half time, 5400000 at full time).
e_Mentality Decide(int goalDifference, unsigned long matchTime_ms);

bool OverridesFormation(e_Mentality mentality);

// Outfield shape to chase the game with; always ten players.
FormationShape GetDesperationShape(int goalDifference);

// Offensive momentum the mentality contributes. This is *added* to the values
// the manager's own tactics produced - it never overrides them.
float GetOffensiveMomentum(e_Mentality mentality);

// Sums the momentum onto a tactic value, scaled by `weight`, keeping the result
// inside the [0, 1] slider range.
float ApplyMomentum(float tacticValue, e_Mentality mentality, float weight);

bool ShouldStayInCorner(e_Mentality mentality, bool hasPossession);

// Corner flag to shelter the ball at. `teamSide` is -1 or 1 and points towards
// the team's own goal, so the attacking corners lie at -teamSide * x.
CornerTarget GetCornerTarget(int teamSide, float playerY);

}  // namespace MatchMentality

#endif

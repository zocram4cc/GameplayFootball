// The CPU's manager: picks a philosophy for the situation and decides when to
// use the bench. Applied to every team that is not run by a human coach, so the
// AI uses the same tactical features a human manager does.

#ifndef _HPP_AI_MANAGER
#define _HPP_AI_MANAGER

#include <vector>

#include "../data/formations.hpp"
#include "teamphilosophy.hpp"

namespace AIManager {

struct MatchSituation {
  int goalDifference = 0;
  unsigned long matchTime_ms = 0;
  // Share of recent possession belonging to this team, in [0, 1].
  float possessionShare = 0.5f;
};

struct SubstitutionCandidate {
  // 1 when fully rested, approaching 0 when spent (Player::GetFatigueFactorInv).
  float fatigueFactorInv = 1.0f;
  float injuryLevel = 0.0f;
  bool isOnPitch = false;
  float averageStat = 0.5f;
};

struct SubstitutionPlan {
  bool wanted = false;
  int playerOutIndex = -1;
  int playerInIndex = -1;
};

// Below this remaining stamina a player is a substitution candidate.
const float tiredFatigueFactorInv = 0.45f;
// Any injury worse than this is worth a substitution straight away.
const float substitutionInjuryLevel = 0.25f;
// Fatigue substitutions wait until the second half is under way.
const unsigned long fatigueSubstitutionTime_ms = 55UL * 60000UL;

// `preferred` is the team's own philosophy from its data; the AI departs from it
// only when the scoreline and the clock call for it.
TeamPhilosophy::e_Philosophy ChoosePhilosophy(TeamPhilosophy::e_Philosophy preferred,
                                              const MatchSituation& situation);

// The shape the CPU wants on the pitch. `preferred` is the team's own formation;
// the AI only departs from it to chase a game or to see one out.
Formations::e_Formation ChooseFormation(Formations::e_Formation preferred,
                                       const MatchSituation& situation);

// Indices refer to positions in `squad`.
SubstitutionPlan PlanSubstitution(const std::vector<SubstitutionCandidate>& squad,
                                  const MatchSituation& situation, int substitutionsLeft);

}  // namespace AIManager

#endif

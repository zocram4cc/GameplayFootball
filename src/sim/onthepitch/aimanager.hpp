// The CPU's manager: decides when to use the bench. Its attack level is PES's
// strategy decider (pesstrategy.hpp), not this file.

#ifndef _HPP_AI_MANAGER
#define _HPP_AI_MANAGER

#include <vector>


namespace AIManager {

struct MatchSituation {
  unsigned long matchTime_ms = 0;
};

struct SubstitutionCandidate {
  // 1 when fully rested, approaching 0 when spent (the player's remaining
  // stamina as a fraction of his tank, fed from his PES tank directly).
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

// Indices refer to positions in `squad`.
SubstitutionPlan PlanSubstitution(const std::vector<SubstitutionCandidate>& squad,
                                  const MatchSituation& situation, int substitutionsLeft);

}  // namespace AIManager

#endif

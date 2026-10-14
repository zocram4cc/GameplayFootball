#include "aimanager.hpp"

namespace AIManager {

SubstitutionPlan PlanSubstitution(const std::vector<SubstitutionCandidate>& squad,
                                  const MatchSituation& situation, int substitutionsLeft) {
  SubstitutionPlan plan;
  if (substitutionsLeft <= 0)
    return plan;

  const bool fatigueSubsAllowed = situation.matchTime_ms >= fatigueSubstitutionTime_ms;

  // Injuries first, then legs. Both pick the worst case on the pitch.
  int injuredIndex = -1;
  int tiredIndex = -1;
  for (size_t i = 0; i < squad.size(); i++) {
    const SubstitutionCandidate& candidate = squad[i];
    if (!candidate.isOnPitch)
      continue;

    if (candidate.injuryLevel >= substitutionInjuryLevel &&
        (injuredIndex == -1 || candidate.injuryLevel > squad[injuredIndex].injuryLevel))
      injuredIndex = static_cast<int>(i);

    if (fatigueSubsAllowed && candidate.fatigueFactorInv <= tiredFatigueFactorInv &&
        (tiredIndex == -1 || candidate.fatigueFactorInv < squad[tiredIndex].fatigueFactorInv))
      tiredIndex = static_cast<int>(i);
  }

  const int outIndex = injuredIndex != -1 ? injuredIndex : tiredIndex;
  if (outIndex == -1)
    return plan;

  // Best player left on the bench comes on.
  int inIndex = -1;
  for (size_t i = 0; i < squad.size(); i++) {
    const SubstitutionCandidate& candidate = squad[i];
    if (candidate.isOnPitch)
      continue;
    if (inIndex == -1 || candidate.averageStat > squad[inIndex].averageStat)
      inIndex = static_cast<int>(i);
  }
  if (inIndex == -1)
    return plan;

  plan.wanted = true;
  plan.playerOutIndex = outIndex;
  plan.playerInIndex = inIndex;
  return plan;
}

}  // namespace AIManager

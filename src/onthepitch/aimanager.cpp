#include "aimanager.hpp"

#include "matchmentality.hpp"

namespace AIManager {

namespace {

// A two-goal cushion is only worth playing keep-ball with once the match is
// well advanced.
const unsigned long comfortableLeadTime_ms = 60UL * 60000UL;

}  // namespace

TeamPhilosophy::e_Philosophy ChoosePhilosophy(TeamPhilosophy::e_Philosophy preferred,
                                              const MatchSituation& situation) {
  // Chasing a deficit late: press high, whatever the team's usual style.
  if (situation.goalDifference < 0 && situation.matchTime_ms >= MatchMentality::desperationStart_ms)
    return TeamPhilosophy::e_Philosophy_Gegenpressing;

  // Protecting a lead in the closing minutes: get everyone behind the ball.
  if (situation.goalDifference > 0 && situation.matchTime_ms >= MatchMentality::timeWastingStart_ms)
    return TeamPhilosophy::e_Philosophy_ParkTheBus;

  // Comfortably ahead with time to play: slow it down and keep the ball, unless
  // the team has a style of its own.
  if (situation.goalDifference >= 2 && situation.matchTime_ms >= comfortableLeadTime_ms &&
      preferred == TeamPhilosophy::e_Philosophy_Balanced)
    return TeamPhilosophy::e_Philosophy_TikiTaka;

  return preferred;
}

Formations::e_Formation ChooseFormation(Formations::e_Formation preferred,
                                        const MatchSituation& situation) {
  // Chasing the game late: take a defender off the pitch and add a forward.
  if (situation.goalDifference < 0 &&
      situation.matchTime_ms >= MatchMentality::desperationStart_ms) {
    const MatchMentality::FormationShape chase =
        MatchMentality::GetDesperationShape(situation.goalDifference);
    Formations::Shape wanted;
    wanted.defenders = chase.defenders;
    wanted.midfielders = chase.midfielders;
    wanted.forwards = chase.forwards;
    return Formations::FromShape(wanted);
  }

  // Seeing out a lead: an extra body across the back.
  if (situation.goalDifference > 0 && situation.matchTime_ms >= MatchMentality::timeWastingStart_ms)
    return Formations::e_Formation_532;

  return preferred;
}

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

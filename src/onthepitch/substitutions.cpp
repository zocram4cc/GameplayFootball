#include "substitutions.hpp"

#include <algorithm>

namespace Substitutions {

namespace {

int ValidTeam(int teamID) {
  return std::max(0, std::min(teamID, 1));
}

}  // namespace

bool IsSubstitutionWindow(bool isInPlay, bool hasKickedOff, bool isInEntrance) {
  return !isInPlay && hasKickedOff && !isInEntrance;
}

int GetRemaining(const State& state, int teamID) {
  return std::max(0, maxSubstitutions - state.used[ValidTeam(teamID)]);
}

e_Result Validate(const State& state, int teamID, const SquadView& squad, bool isStoppage) {
  // Play has to be stopped first; that condition resolves itself, so report it
  // before complaining about the squad.
  if (!isStoppage)
    return e_Result_NotAStoppage;
  if (GetRemaining(state, teamID) <= 0)
    return e_Result_NoSubstitutionsLeft;
  if (!squad.playerOutIsOnPitch)
    return e_Result_PlayerNotOnPitch;
  if (squad.playerOutIsSentOff)
    return e_Result_PlayerSentOff;
  if (!squad.playerInIsOnBench || squad.playerInHasPlayed)
    return e_Result_PlayerNotAvailable;

  return e_Result_Accepted;
}

void Commit(State& state, int teamID) {
  const int team = ValidTeam(teamID);
  state.used[team] = std::min(state.used[team] + 1, maxSubstitutions);
}

}  // namespace Substitutions

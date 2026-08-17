// Substitution rules for coach mode: three per team, at stoppages only, and
// only with players who are actually available.
// See TECHNICAL_ROADMAP.md section 3B.2.

#ifndef _HPP_SUBSTITUTIONS
#define _HPP_SUBSTITUTIONS

namespace Substitutions {

enum e_Result {
  e_Result_Accepted = 0,
  e_Result_NotAStoppage,
  e_Result_NoSubstitutionsLeft,
  e_Result_PlayerNotOnPitch,
  e_Result_PlayerSentOff,
  e_Result_PlayerNotAvailable,
};

const int maxSubstitutions = 3;

struct State {
  int used[2] = {0, 0};
};

// What the caller knows about the two players involved.
struct SquadView {
  bool playerOutIsOnPitch = false;
  bool playerOutIsSentOff = false;
  bool playerInIsOnBench = false;
  bool playerInHasPlayed = false;
};

// Is this a moment a substitution may be made in?
//
// "Not in play" is not enough on its own: before kick-off the match is not in
// play either, so a plain !IsInPlay() test made the pre-match presentation a
// substitution window - the AI manager swapped players during the walkout,
// HUD banner and tunnel cutscene included, before a ball had been kicked. A
// substitution needs play stopped *within a match that has started*.
bool IsSubstitutionWindow(bool isInPlay, bool hasKickedOff, bool isInEntrance);

int GetRemaining(const State& state, int teamID);

e_Result Validate(const State& state, int teamID, const SquadView& squad, bool isStoppage);

// Spends one of the team's substitutions.
void Commit(State& state, int teamID);

}  // namespace Substitutions

#endif

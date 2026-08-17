// AI vs AI manager mode ("coach mode"): the AI plays both teams while one or two
// humans run the tactics, lineup and substitutions.
// See TECHNICAL_ROADMAP.md section 3B.

#ifndef _HPP_COACH_MODE
#define _HPP_COACH_MODE

#include <string>

namespace CoachMode {

enum e_TeamControl {
  // Played and managed by the AI.
  e_TeamControl_AI = 0,
  // A human is on the sticks; that human also gets the tactics menu.
  e_TeamControl_HumanPlayers,
  // A human runs the touchline only; the AI plays every player.
  e_TeamControl_HumanCoach,
  e_TeamControl_Count,
};

struct Setup {
  e_TeamControl control[2] = {e_TeamControl_AI, e_TeamControl_AI};
};

e_TeamControl Parse(const std::string& name);
std::string GetName(e_TeamControl control);

Setup Create(e_TeamControl team0, e_TeamControl team1);

// Builds the setup from the number of human gamers on each team, so ordinary
// matches are unaffected. `coachModeEnabled` promotes teams with no human
// players to human-coached teams.
Setup FromHumanGamerCounts(int team0HumanGamers, int team1HumanGamers, bool coachModeEnabled);

// At least one team is run from the touchline.
bool IsCoachMode(const Setup& setup);
// Both teams are run from the touchline: manager against manager.
bool IsManagerDuel(const Setup& setup);

bool ControlsPlayersOnPitch(const Setup& setup, int teamID);
// Whether a human may open the game plan / tactics menu for this team.
bool CanEditTactics(const Setup& setup, int teamID);

// May the AI manager adapt this team's philosophy, shape and bench?
//
// In coach mode it may not, for either team. Sparing only the human-coached
// side left the CPU reshaping the other bench, so a manager duel was really one
// manager against a CPU that kept second-guessing him. Coach mode means the
// touchline belongs to the humans and the AI manager is off entirely.
bool AIManagerRuns(const Setup& setup, int teamID);

}  // namespace CoachMode

#endif

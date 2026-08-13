#include "coachmode.hpp"

#include <algorithm>
#include <cctype>

namespace CoachMode {

namespace {

std::string Normalize(const std::string& name) {
  std::string result;
  result.reserve(name.size());
  for (char character : name) {
    const unsigned char raw = static_cast<unsigned char>(character);
    if (std::isalnum(raw))
      result += static_cast<char>(std::tolower(raw));
  }
  return result;
}

int ValidTeam(int teamID) {
  return std::max(0, std::min(teamID, 1));
}

}  // namespace

e_TeamControl Parse(const std::string& name) {
  const std::string key = Normalize(name);
  for (int i = 0; i < e_TeamControl_Count; i++) {
    const e_TeamControl control = static_cast<e_TeamControl>(i);
    if (key == Normalize(GetName(control)))
      return control;
  }
  return e_TeamControl_AI;
}

std::string GetName(e_TeamControl control) {
  switch (control) {
    case e_TeamControl_HumanPlayers:
      return "players";
    case e_TeamControl_HumanCoach:
      return "coach";
    default:
      return "ai";
  }
}

Setup Create(e_TeamControl team0, e_TeamControl team1) {
  Setup setup;
  setup.control[0] = team0;
  setup.control[1] = team1;
  return setup;
}

Setup FromHumanGamerCounts(int team0HumanGamers, int team1HumanGamers, bool coachModeEnabled) {
  const int counts[2] = {team0HumanGamers, team1HumanGamers};

  Setup setup;
  for (int team = 0; team < 2; team++) {
    if (counts[team] > 0)
      setup.control[team] = e_TeamControl_HumanPlayers;
    else if (coachModeEnabled)
      setup.control[team] = e_TeamControl_HumanCoach;
    else
      setup.control[team] = e_TeamControl_AI;
  }
  return setup;
}

bool IsCoachMode(const Setup& setup) {
  return setup.control[0] == e_TeamControl_HumanCoach ||
         setup.control[1] == e_TeamControl_HumanCoach;
}

bool IsManagerDuel(const Setup& setup) {
  return setup.control[0] == e_TeamControl_HumanCoach &&
         setup.control[1] == e_TeamControl_HumanCoach;
}

bool ControlsPlayersOnPitch(const Setup& setup, int teamID) {
  return setup.control[ValidTeam(teamID)] == e_TeamControl_HumanPlayers;
}

bool CanEditTactics(const Setup& setup, int teamID) {
  const e_TeamControl control = setup.control[ValidTeam(teamID)];
  return control == e_TeamControl_HumanCoach || control == e_TeamControl_HumanPlayers;
}

}  // namespace CoachMode

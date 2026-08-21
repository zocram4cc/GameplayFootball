#include "coachmode.hpp"

#include <string>

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

bool AIManagerRuns(const Setup& setup, int teamID) {
  // Per side: the AI manager runs a bench nobody human is running. A human's own
  // team is never managed for him, and a CPU opponent keeps its manager even when
  // the other bench is coached - "coach against CPU" means against a managed CPU.
  //
  // This was once a blanket rule, off for both sides whenever anyone coached, so
  // that a manager duel was not one human against a CPU that kept reshaping the
  // other bench. With the benches assigned per side that case is expressed directly
  // - both marked coached - and the blanket rule only removed an opponent.
  return !CanEditTactics(setup, teamID);
}

Setup FromSelections(const int playing[2], const int coaching[2], bool coachBothSides) {
  Setup setup;
  for (int team = 0; team < 2; team++) {
    if (playing[team] > 0)
      setup.control[team] = e_TeamControl_HumanPlayers;
    else if (coaching[team] > 0 || coachBothSides)
      setup.control[team] = e_TeamControl_HumanCoach;
    else
      setup.control[team] = e_TeamControl_AI;
  }
  return setup;
}

std::vector<std::string> TipLines(const Setup& setup, const std::string& homeName,
                                  const std::string& awayName) {
  std::vector<std::string> lines;
  const bool coached[2] = {setup.control[0] == e_TeamControl_HumanCoach,
                           setup.control[1] == e_TeamControl_HumanCoach};
  if (!coached[0] && !coached[1])
    return lines;

  // A duel does not name the sides: both benches are the viewer's, and two club
  // names of any length overrun the line.
  if (coached[0] && coached[1]) {
    lines.push_back("Both benches are yours. The AI plays.");
  } else {
    const std::string& name = coached[0] ? homeName : awayName;
    if (name.empty()) {
      lines.push_back("You run the bench. The AI plays.");
    } else {
      // Long enough for any club, short enough to keep the line on the menu.
      const size_t kNameBudget = 40;
      const std::string shown =
          name.size() > kNameBudget ? name.substr(0, kNameBudget - 3) + "..." : name;
      lines.push_back("You run the bench for " + shown + ". The AI plays.");
    }
  }
  lines.push_back("Pad:  RT + d-pad = mentality    RT + buttons = instructions");
  lines.push_back("Keys: PgUp/PgDn = line    F5-F11 = instructions" +
                  std::string(IsManagerDuel(setup) ? "    Shift = other bench" : ""));
  return lines;
}

std::string Describe(const Setup& setup) {
  const char* role[e_TeamControl_Count] = {"CPU", "Player", "Coach"};
  const int home = setup.control[0], away = setup.control[1];
  const int homeRole = (home >= 0 && home < e_TeamControl_Count) ? home : e_TeamControl_AI;
  const int awayRole = (away >= 0 && away < e_TeamControl_Count) ? away : e_TeamControl_AI;
  return std::string(role[homeRole]) + " vs " + role[awayRole];
}

}  // namespace CoachMode

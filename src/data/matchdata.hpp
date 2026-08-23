// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MATCHDATA
#define _HPP_MATCHDATA

#include <array>
#include <memory>

#include "../gamedefines.hpp"
#include "defines.hpp"
#include "teamdata.hpp"

class MatchData {
public:
  MatchData(int team1DatabaseID, int team2DatabaseID);
  virtual ~MatchData() = default;

  TeamData* GetTeamData(int id) { return teamData[id].get(); }
  int GetGoalCount(int id) { return goalCount[id]; }
  void SetGoalCount(int id, int amount) { goalCount[id] = amount; }
  void AddPossessionTime_10ms(int teamID);
  unsigned long GetPossessionTime_ms(int teamID) { return possessionTime_ms[teamID]; }
  float GetPossessionFactor_60seconds() {
    return possession60seconds / 60.0f * 0.5f + 0.5f;
  }  // REMEMBER THESE ARE IRL INGAME SECONDS (because, I guess the tactics should be based on irl
     // possession time instead of gametime? not sure yet, think about this)
  void AddShot(int teamID) {
    shots[teamID] += 1;
    // A shot ends the passing sequence: the previous pass cannot still be pending
    // when a new play starts from the goal kick or elsewhere.
    pendingPassTeamID = -1;
    pendingPassIsGoalkeeper = false;
    pendingPassOwnThird = false;
  }
  int GetShots(int teamID) { return shots[teamID]; }
  void AddShotOnTarget(int teamID) { shotsOnTarget[teamID] += 1; }
  int GetShotsOnTarget(int teamID) { return shotsOnTarget[teamID]; }

  // pass tracking
  void AddPassAttempt(int teamID) {
    passAttempts[teamID]++;
    pendingPassTeamID = teamID;
    pendingPassIsGoalkeeper = false;  // set by the GK-lost detector in humanoid.cpp
  }
  void ResetPendingPass() {
    pendingPassTeamID = -1;
    pendingPassIsGoalkeeper = false;
    pendingPassOwnThird = false;
  }
  void SetPendingPassGoalkeeper() { pendingPassIsGoalkeeper = true; }
  bool PendingPassIsGoalkeeper() const { return pendingPassIsGoalkeeper; }
  void SetPendingPassOwnThird() { pendingPassOwnThird = true; }
  bool PendingPassOwnThird() const { return pendingPassOwnThird; }
  void RecordBallTouch(int receivingTeamID) {
    const bool lastWasGoalkeeper = pendingPassIsGoalkeeper;
    const bool lastWasOwnThird = pendingPassOwnThird;
    if (pendingPassTeamID == receivingTeamID) {
      passesCompleted[receivingTeamID]++;
    } else if (pendingPassTeamID >= 0) {
      // A pending pass met by the OTHER team: the passer just played it straight to
      // an opponent. Debug-only - this is a quality signal, not a rule.
#ifndef NDEBUG
      AddBadPassToOpponent(pendingPassTeamID);
      if (lastWasGoalkeeper) AddGoalkeeperLost(pendingPassTeamID);
      if (lastWasOwnThird) {
        // Gave it away in the team's own third: only counts when the opposition
        // turn it into a shot in the next few seconds, which is tracked separately.
        AddOwnThirdGiveaway(pendingPassTeamID);
      }
#endif
    }
    pendingPassTeamID = -1;
    pendingPassIsGoalkeeper = false;
    pendingPassOwnThird = false;
  }
  int GetPassAttempts(int teamID) const { return passAttempts[teamID]; }
  int GetPassesCompleted(int teamID) const { return passesCompleted[teamID]; }

  // foul tracking
  void AddFoul(int teamID) { foulsCommitted[teamID]++; }
  int GetFouls(int teamID) const { return foulsCommitted[teamID]; }

  // Questionable-play logging, debug-only (see the deny list in the match goal).
  // Passed to the log at the end of the match by GameOverPage so pass/play quality
  // is measured rather than guessed at.
  void AddBadPassToOpponent(int teamID) {
#ifndef NDEBUG
    badPassToOpponent[teamID]++;
    badPassTeam = teamID;
    badPass++;
#endif
  }
  void AddGoalkeeperLost(int teamID) {
#ifndef NDEBUG
    goalkeeperLost[teamID]++;
    badPass++;
#endif
  }
  void AddOwnThirdGiveaway(int teamID) {
#ifndef NDEBUG
    ownThirdGiveaway[teamID]++;
    badPass++;
#endif
  }
  int GetBadPassToOpponent(int teamID) const { return badPassToOpponent[teamID]; }
  int GetGoalkeeperLost(int teamID) const { return goalkeeperLost[teamID]; }
  int GetOwnThirdGiveaway(int teamID) const { return ownThirdGiveaway[teamID]; }
  int GetBadPlayTotal() const { return badPass; }

  // whether this match's result has already been written to match history
  bool IsHistorySaved() const { return historySaved; }
  void SetHistorySaved(bool value) { historySaved = value; }

protected:
  std::array<std::unique_ptr<TeamData>, 2> teamData;

  int goalCount[2];
  int badPass = 0;
  int badPassToOpponent[2] = {0, 0};
  int goalkeeperLost[2] = {0, 0};
  int ownThirdGiveaway[2] = {0, 0};
  int badPassTeam = -1;
  bool pendingPassIsGoalkeeper = false;
  bool pendingPassOwnThird = false;

  unsigned long possessionTime_ms[2];
  float possession60seconds;  // -600 to 600 for possession of team 1 / 2 respectively
  int shots[2];
  int shotsOnTarget[2];

  int passAttempts[2];
  int passesCompleted[2];
  int pendingPassTeamID;

  int foulsCommitted[2];

  bool historySaved;
};

#endif

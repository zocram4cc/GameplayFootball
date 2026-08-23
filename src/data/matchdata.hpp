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
  void AddPossessionTime_10ms(int teamID);  // also advances the debug match clock
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
    // An own-third giveaway only becomes a counted "bad play" if it actually
    // hurts: whoever picked the passer's pocket must turn it into a shot within
    // twelve seconds of match time. Otherwise the giveaway came to nothing and
    // stays uncounted.
#ifndef NDEBUG
    if (pendingOwnThirdGiveawayTeam >= 0 && teamID == (pendingOwnThirdGiveawayTeam ^ 1) &&
        matchTime_ms - pendingOwnThirdTime_ms <= ownThirdGiveawayWindow_ms) {
      ownThirdGiveaway[pendingOwnThirdGiveawayTeam]++;
      badPass++;
    }
    pendingOwnThirdGiveawayTeam = -1;
#endif
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
#ifndef NDEBUG
    pendingOwnThirdGiveawayTeam = -1;
#endif
  }
  void SetPendingPassGoalkeeper() { pendingPassIsGoalkeeper = true; }
  bool PendingPassIsGoalkeeper() const { return pendingPassIsGoalkeeper; }
  void SetPendingPassOwnThird() { pendingPassOwnThird = true; }
  bool PendingPassOwnThird() const { return pendingPassOwnThird; }

  // Debug-only measurement, see RecordBallTouch: an own-third giveaway is
  // stamped here with its team and match time; AddShot decides whether it was
  // punished within ownThirdGiveawayWindow_ms.
  void SetPendingOwnThirdGiveaway(int teamID) {
#ifndef NDEBUG
    pendingOwnThirdGiveawayTeam = teamID;
    pendingOwnThirdTime_ms = matchTime_ms;
#endif
  }
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
      passFailIntercept[pendingPassTeamID]++;  // breakdown: intercepted in flight
      if (lastWasGoalkeeper) AddGoalkeeperLost(pendingPassTeamID);
      if (lastWasOwnThird) {
        // Gave it away in the team's own third. Not counted yet: only a shot
        // by the intercepting side within twelve seconds settles it as a bad
        // play (see AddShot).
        SetPendingOwnThirdGiveaway(pendingPassTeamID);
      }
#endif
    }
#ifndef NDEBUG
    else if (receivingTeamID == pendingOwnThirdGiveawayTeam) {
      // The giveaway team won the ball straight back: no shot ever followed,
      // so the pending giveaway is dropped instead of punished later.
      pendingOwnThirdGiveawayTeam = -1;
    }
#endif
    pendingPassTeamID = -1;
    pendingPassIsGoalkeeper = false;
    pendingPassOwnThird = false;
  }
  // The ball left the pitch while a pass was still in flight: count it as an
  // out-of-bounds failure for the passer, then close the passing sequence.
  // Debug-only: the breakdown exists to steer tuning, not to change it.
  void FailPendingPassOutOfBounds() {
#ifndef NDEBUG
    if (pendingPassTeamID >= 0)
      passFailOob[pendingPassTeamID]++;
    // The sequence is over either way: a pending giveaway can no longer be
    // punished once the ball has gone out off the interception.
    pendingOwnThirdGiveawayTeam = -1;
#endif
    ResetPendingPass();
  }
  int GetPassAttempts(int teamID) const { return passAttempts[teamID]; }
  int GetPassesCompleted(int teamID) const { return passesCompleted[teamID]; }
  int GetPassFailIntercept(int teamID) const { return passFailIntercept[teamID]; }
  int GetPassFailOutOfBounds(int teamID) const { return passFailOob[teamID]; }
  int GetPassFailBadTrap(int teamID) const { return passFailTrap[teamID]; }

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
  int GetOwnThirdGiveawayCountdown_ms() {
#ifndef NDEBUG
    if (pendingOwnThirdGiveawayTeam < 0)
      return 0;
    const unsigned long elapsed = matchTime_ms - pendingOwnThirdTime_ms;
    return static_cast<int>(ownThirdGiveawayWindow_ms > elapsed ? ownThirdGiveawayWindow_ms - elapsed : 0);
#endif
    return 0;
  }
  int GetBadPassToOpponent(int teamID) const { return badPassToOpponent[teamID]; }
  int GetGoalkeeperLost(int teamID) const { return goalkeeperLost[teamID]; }
  int GetOwnThirdGiveaway(int teamID) const { return ownThirdGiveaway[teamID]; }
  void AddPassFailBadTrap(int teamID) {
#ifndef NDEBUG
    passFailTrap[teamID]++;
#endif
  }
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
  int passFailIntercept[2] = {0, 0};
  int passFailTrap[2] = {0, 0};
  int passFailOob[2] = {0, 0};
  int badPassTeam = -1;
  bool pendingPassIsGoalkeeper = false;
  bool pendingPassOwnThird = false;

  // Debug-only pending own-third giveaway: which team gave it away, and at
  // what match time. Set on the intercepting touch (RecordBallTouch), settled
  // by AddShot or cleared on a regain/restart.
  int pendingOwnThirdGiveawayTeam = -1;
  unsigned long pendingOwnThirdTime_ms = 0;
  // How long the intercepting side has to make the giveaway count as danger.
  static constexpr unsigned long ownThirdGiveawayWindow_ms = 12000;
  // Monotonic stand-in for the match clock, advanced by AddPossessionTime_10ms:
  // keeps this data layer engine-independent so tests need no Match object.
  unsigned long matchTime_ms = 0;
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

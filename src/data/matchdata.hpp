// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MATCHDATA
#define _HPP_MATCHDATA

#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "../gamedefines.hpp"
#include "defines.hpp"
#include "teamdata.hpp"
class MatchData {
public:
  // Band edges in metres: <5, 5-10, 10-15, 15-25, 25-40, >=40.
  static constexpr int passDistanceBandCount = 6;

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
  // A clearance is a ball hoofed away with no intended recipient. It is not a
  // pass and is deliberately kept out of passAttempts: counting one as a failed
  // pass is what held measured accuracy near 50%, since roughly half of all
  // touches logged as passes were panic clearances that can never complete.
  // No pending pass is opened, so whoever picks the ball up next is not credited
  // with completing anything.
  void AddClearance(int teamID) {
    clearances[teamID]++;
    ResetPendingPass();
  }
  int GetClearances(int teamID) const { return clearances[teamID]; }
  void ResetPendingPass() {
    pendingPassTeamID = -1;
    pendingPassIsGoalkeeper = false;
    pendingPassOwnThird = false;
#ifndef NDEBUG
    pendingOwnThirdGiveawayTeam = -1;
#endif
  }
  void RecordPassGoalkeeperCatch() {
#ifndef NDEBUG
    if (pendingPassTeamID >= 0) passGoalkeeperCatch[pendingPassTeamID]++;
#endif
    ResetPendingPass();
  }
  void RecordPassRestart() {
#ifndef NDEBUG
    if (pendingPassTeamID >= 0) passRestart[pendingPassTeamID]++;
#endif
    ResetPendingPass();
  }
  int GetPassGoalkeeperCatch(int teamID) const {
#ifndef NDEBUG
    return passGoalkeeperCatch[teamID];
#else
    (void)teamID;
    return 0;
#endif
  }
  int GetPassRestart(int teamID) const {
#ifndef NDEBUG
    return passRestart[teamID];
#else
    (void)teamID;
    return 0;
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
    // Taken off an opponent in open play, with nothing in flight to intercept:
    // that is a tackle. Decided before lastTouchTeamID is moved on.
    bool wonFromOpponent = lastTouchTeamID >= 0 && lastTouchTeamID != receivingTeamID &&
                           pendingPassTeamID < 0;
    lastTouchTeamID = receivingTeamID;
    ResolveExpiredCleanChecks();
    const bool lastWasGoalkeeper = pendingPassIsGoalkeeper;
    const bool lastWasOwnThird = pendingPassOwnThird;
    if (pendingPassTeamID == receivingTeamID) {
      passesCompleted[receivingTeamID]++;
      // A completion only counts as CLEAN once the receiving team is still
      // the last team to touch the ball cleanCompletionWindow_ms later; see
      // ResolveExpiredCleanChecks. This runs in release too, since it feeds
      // the [balance-passing] accuracy line rather than debug instrumentation.
      pendingCleanChecks.push_back({receivingTeamID, matchTime_ms + cleanCompletionWindow_ms});
      wonFromOpponent = false;
    } else if (pendingPassTeamID >= 0) {
      // The ball was cut out of a pass in flight: an interception for whoever
      // met it, whichever build this is.
      interceptions[receivingTeamID]++;
      wonFromOpponent = false;
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
    if (wonFromOpponent) tackles[receivingTeamID]++;
    pendingPassTeamID = -1;
    pendingPassIsGoalkeeper = false;
    pendingPassOwnThird = false;
  }
  // Lazily settles any clean-completion window whose deadline has passed:
  // CLEAN if the completing team is still the ball's last-touch team once the
  // deadline arrives, otherwise SCRAPPY (the ball changed hands within
  // cleanCompletionWindow_ms of the reception). Called from RecordBallTouch
  // (a touch event) and FailPendingPassOutOfBounds (an out event) so it is
  // never more than one event stale. Not guarded by NDEBUG: these counters
  // feed the [balance-passing] accuracy line in release builds too.
  void ResolveExpiredCleanChecks() {
    while (!pendingCleanChecks.empty() &&
           pendingCleanChecks.front().deadline_ms <= matchTime_ms) {
      const int completingTeam = pendingCleanChecks.front().teamID;
      if (lastTouchTeamID == completingTeam)
        cleanCompletions[completingTeam]++;
      else
        scrappyCompletions[completingTeam]++;
      pendingCleanChecks.erase(pendingCleanChecks.begin());
    }
  }
  int GetCleanCompletions(int teamID) const { return cleanCompletions[teamID]; }
  int GetScrappyCompletions(int teamID) const { return scrappyCompletions[teamID]; }
  int GetPendingCleanCheckCount() const { return static_cast<int>(pendingCleanChecks.size()); }
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
    // The ball leaving play is an out event too: a clean-completion window
    // may have quietly expired while nobody else touched the ball.
    ResolveExpiredCleanChecks();
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

  // The rest of PES's half-time table (docs/PRESENTATION_SPEC.md 3.4): the
  // referee's own decisions, the ball won back off an opponent, the ball put
  // into the box from wide, and the keeper stopping a shot that was going in.
  // All release counters - this table is shown to the player, not measured for
  // tuning.
  void AddOffside(int teamID) { offsides[teamID]++; }
  int GetOffsides(int teamID) const { return offsides[teamID]; }
  void AddCorner(int teamID) { corners[teamID]++; }
  int GetCorners(int teamID) const { return corners[teamID]; }
  void AddFreeKick(int teamID) { freeKicks[teamID]++; }
  int GetFreeKicks(int teamID) const { return freeKicks[teamID]; }
  void AddCross(int teamID) { crosses[teamID]++; }
  int GetCrosses(int teamID) const { return crosses[teamID]; }
  int GetInterceptions(int teamID) const { return interceptions[teamID]; }
  int GetTackles(int teamID) const { return tackles[teamID]; }
  int GetSaves(int teamID) const { return saves[teamID]; }

  // A shot on target is left pending: it becomes a save if the defending
  // keeper has the ball before the window runs out, and simply lapses if it
  // went in or nobody stopped it.
  static constexpr unsigned long saveWindow_ms = 4000;
  void OpenSaveChance(int shootingTeamID) {
    pendingShotTeamID = shootingTeamID;
    pendingShotTime_ms = matchTime_ms;
  }
  void CloseSaveChance() { pendingShotTeamID = -1; }
  void RecordGoalkeeperTouch(int keeperTeamID) {
    if (pendingShotTeamID >= 0 && pendingShotTeamID != keeperTeamID &&
        matchTime_ms - pendingShotTime_ms <= saveWindow_ms) {
      saves[keeperTeamID]++;
    }
    pendingShotTeamID = -1;
  }

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

  // Debug-only instrumentation behind the [pass-dist] card line: how far the
  // AI actually plays its passes, binned, plus the running second moment so a
  // root-mean-square length can be printed without keeping every sample.
  void AddPassDistance(int teamID, float distance_m) {
#ifndef NDEBUG
    const int band = PassDistanceBand(distance_m);
    if (band >= 0 && band < passDistanceBandCount) passDistanceBands[teamID][band]++;
    passDistanceSum2[teamID] += static_cast<unsigned long long>(distance_m * distance_m * 100.0f);
    passDistanceCount[teamID]++;
#endif
  }
  // Debug-only: mean spread of each player's nearest teammates while the
  // support web is computed - the width the passing network really offers.
  void AddSupportWebSample(int teamID, float width_m) {
#ifndef NDEBUG
    supportWebSum[teamID] += width_m;
    supportWebSamples[teamID]++;
#endif
  }

  // Debug-only: touches that never reach RecordBallTouch (interfere, failed
  // deflect, slide, body collision, keeper retention) while a pass is still
  // pending. These are the leaks the [pass-fail] breakdown cannot see;
  // attributed to the team whose pass was in flight, split by whether the
  // toucher was an opponent. Kinds: 0 interfere, 1 deflect-fail, 2 slide,
  // 3 body collision, 4 keeper-retain.
  void AddGhostTouch(int touchingTeamID, int kind) {
#ifndef NDEBUG
    if (pendingPassTeamID < 0 || kind < 0 || kind >= ghostKindCount) return;
    const bool hostile = touchingTeamID != pendingPassTeamID;
    ghostTouches[hostile ? 1 : 0][pendingPassTeamID][kind]++;
#endif
  }
  static constexpr int ghostKindCount = 5;
  int GetGhostTouch(int hostile, int teamID, int kind) const {
#ifndef NDEBUG
    return ghostTouches[hostile][teamID][kind];
#else
    (void)hostile; (void)teamID; (void)kind; return 0;
#endif
  }

  static int PassDistanceBand(float distance_m) {
    if (distance_m < 5.0f) return 0;
    if (distance_m < 10.0f) return 1;
    if (distance_m < 15.0f) return 2;
    if (distance_m < 25.0f) return 3;
    if (distance_m < 40.0f) return 4;
    return 5;
  }
  int GetPassDistanceBand(int teamID, int band) const {
#ifndef NDEBUG
    return passDistanceBands[teamID][band];
#else
    (void)teamID; (void)band; return 0;
#endif
  }
  // Root-mean-square chosen pass length in metres.
  float GetPassDistanceMeanRms_m(int teamID) const {
#ifndef NDEBUG
    if (passDistanceCount[teamID] == 0) return 0.0f;
    return std::sqrt(static_cast<float>(passDistanceSum2[teamID]) /
                     static_cast<float>(passDistanceCount[teamID]) / 100.0f);
#else
    (void)teamID; return 0.0f;
#endif
  }
  float GetSupportWebWidthMean_m(int teamID) const {
#ifndef NDEBUG
    return supportWebSamples[teamID] > 0 ? supportWebSum[teamID] / supportWebSamples[teamID]
                                         : 0.0f;
#else
    (void)teamID; return 0.0f;
#endif
  }
  int ghostTouches[2][2][ghostKindCount] = {};
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
  int offsides[2] = {0, 0};
  int corners[2] = {0, 0};
  int freeKicks[2] = {0, 0};
  int crosses[2] = {0, 0};
  int interceptions[2] = {0, 0};
  int tackles[2] = {0, 0};
  int saves[2] = {0, 0};
  int pendingShotTeamID = -1;
  unsigned long pendingShotTime_ms = 0;
  int passFailTrap[2] = {0, 0};
  int passFailOob[2] = {0, 0};
  // Clean-completion tracking (release-safe, see RecordBallTouch and
  // ResolveExpiredCleanChecks): a pending check names the team that just
  // completed a pass and the match time its window closes.
  struct PendingCleanCheck {
    int teamID;
    unsigned long deadline_ms;
  };
  int cleanCompletions[2] = {0, 0};
  int scrappyCompletions[2] = {0, 0};
  // Which team touched the ball last, updated on every RecordBallTouch call.
  int lastTouchTeamID = -1;
  static constexpr unsigned long cleanCompletionWindow_ms = 1500;
  std::vector<PendingCleanCheck> pendingCleanChecks;
#ifndef NDEBUG
  int passGoalkeeperCatch[2] = {0, 0};
  int passRestart[2] = {0, 0};
#endif
  int passDistanceBands[2][passDistanceBandCount] = {};
  unsigned long long passDistanceSum2[2] = {0, 0};
  int passDistanceCount[2] = {0, 0};
  float supportWebSum[2] = {0.0f, 0.0f};
  int supportWebSamples[2] = {0, 0};
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
  int clearances[2];
  int passesCompleted[2];
  int pendingPassTeamID;

  int foulsCommitted[2];

  bool historySaved;
};

#endif

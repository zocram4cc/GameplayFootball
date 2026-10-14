// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MATCHDATA
#define _HPP_MATCHDATA

#include <array>
#include <cmath>
#include <memory>
#include <string>
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
    // A shot is a ball on its way to the line: stamped here so the crossing
    // sampler can pair an arrival with the shot it came from, without going
    // through the projected on-target counter (which the strike band clamps,
    // and which therefore cannot be used to check the band).
    lastShotTeamID = teamID;
    lastShotTime_ms = matchTime_ms;
    // Every shot opens the save window, not only the ones the launch
    // projection calls on target: the window used to be opened on that
    // projection, so a "save" could only ever be recorded for a shot the
    // projection had already judged on target, and the saving column could not
    // corroborate the projection it was drawn from. Whether a keeper's touch
    // was a save is decided at the touch, from where the ball was going
    // (GameplayTuning::PredictGoalLineCrossing).
    OpenSaveChance(teamID);
    // A shot ends the passing sequence: the previous pass cannot still be pending
    // when a new play starts from the goal kick or elsewhere. One still pending
    // here was struck before any recorded touch ended it; counted, so the
    // pass accounting closes (see GetPassShotPending).
    if (pendingPassTeamID >= 0) passShotPending[pendingPassTeamID]++;
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

  // Where shots are struck from and, when they miss, which way. Shot quality
  // is the whole balance question - 14 shots from the edge of the box and 14
  // hopeful ones from 30 m score differently - and the projected on-target
  // column alone cannot say whether a miss went wide, over, or died on the
  // turf short of the line. Always on (the sim runs a Release build, where
  // the #ifndef NDEBUG cards are compiled out) and costs one increment per
  // shot.
  static constexpr int shotDistanceBandCount = 4;
  static int ShotDistanceBand(float distance_m) {
    if (distance_m < 11.0f) return 0;  // six-yard box out to the penalty spot
    if (distance_m < 18.0f) return 1;  // the rest of the penalty area
    if (distance_m < 25.0f) return 2;  // just outside it
    return 3;                          // long range
  }
  // `miss`: -1 on target, 0 wide of a post, 1 over the bar, 2 short (the ball
  // meets the plane below ground, so it never arrives at all).
  static constexpr int shotMissKindCount = 3;
  void AddShotGeometry(int teamID, float distance_m, int miss) {
    const int band = ShotDistanceBand(distance_m);
    if (band >= 0 && band < shotDistanceBandCount) shotDistanceBands[teamID][band]++;
    shotDistanceSum_m[teamID] += distance_m;
    shotGeometryCount[teamID]++;
    if (miss >= 0 && miss < shotMissKindCount) shotMisses[teamID][miss]++;
  }
  int GetShotDistanceBand(int teamID, int band) const { return shotDistanceBands[teamID][band]; }
  int GetShotMiss(int teamID, int kind) const { return shotMisses[teamID][kind]; }
  float GetShotDistanceMean_m(int teamID) const {
    if (shotGeometryCount[teamID] == 0) return 0.0f;
    return shotDistanceSum_m[teamID] / static_cast<float>(shotGeometryCount[teamID]);
  }

  // Why the keeper was beaten, not merely how often. Two keeper packages were
  // fitted against a point-blank sweep that turned out to describe neither the
  // match nor (as it later emerged) a real keeper's stats; the sweep says a
  // real 4cc keeper saves ~92% of the measured shot mix and matches say ~56%.
  // The sweep varies REACH with the keeper central and on his line, so it
  // cannot see a keeper who is simply in the wrong place. These two counters
  // separate the causes at the decision itself:
  //   - out of time: the ball met his plane inside his own reaction, so
  //     KeeperEffectiveReach_m gave him no dive at all - reach cannot fix it,
  //   - out of reach: he had the flight time and the gap was still too big -
  //     reach, dive speed or POSITIONING, and the mean gap says which.
  // Always on, like the shot bands above: one increment per beaten keeper.
  static constexpr float keeperOutOfTime_s = 0.25f;  // ~a keeper's own reaction
  // `lateral_m` and `overhead_m` are the two halves of `gap_m`: how far the
  // ball crossed his plane sideways of his hands, and how far above his
  // hands. Kept apart because they are different defects - lateral is where
  // he was standing (positioning), overhead is how high the shot was - and
  // the blended mean cannot say which one to fix.
  void AddKeeperBeaten(int teamID, float gap_m, float timeToPlane_s, float lateral_m,
                       float overhead_m) {
    if (timeToPlane_s < keeperOutOfTime_s)
      keeperBeatenOutOfTime[teamID]++;
    else
      keeperBeatenOutOfReach[teamID]++;
    keeperBeatenGapSum_m[teamID] += gap_m;
    keeperBeatenLateralSum_m[teamID] += lateral_m;
    keeperBeatenOverheadSum_m[teamID] += overhead_m;
    keeperBeatenCount[teamID]++;
  }
  // Would he have got there with a slightly different keeper? Both answers
  // are counters, not means, because the mean gap cannot say which lever has
  // headroom. `standing_m` is the pure short-flight case (no dive to extend);
  // `fasterReaction_m` is the opposite, the shots with flight on the clock
  // where a quicker read buys coverage a longer frame cannot. One batch
  // prints both, so the keeper is settled without guessing a constant.
  void AddKeeperBeatenWhatIf(int teamID, bool savedWithLongerStand, bool savedWithFasterRead) {
    if (savedWithLongerStand) keeperBeatenSavedByStanding[teamID]++;
    if (savedWithFasterRead) keeperBeatenSavedByReaction[teamID]++;
    keeperBeatenWhatIfCount[teamID]++;
  }
  int GetKeeperBeatenSavedByStanding(int teamID) const { return keeperBeatenSavedByStanding[teamID]; }
  int GetKeeperBeatenSavedByReaction(int teamID) const { return keeperBeatenSavedByReaction[teamID]; }
  int GetKeeperBeatenWhatIfCount(int teamID) const { return keeperBeatenWhatIfCount[teamID]; }
  // The pace of the ball into each failed trap. GetTrapKillStrength scrubs at
  // most half of it, and possession then needs the ball within the receiver's
  // 8 m/s sprint plus a 6 m/s tolerance - so above ~28 m/s incoming a trap
  // CANNOT yield possession however well timed it is (worked out, not
  // measured). Whether that cliff matters is the question these two numbers
  // answer: if the failed traps cluster above it, the kill cap is the lever;
  // if they sit below it, trapping fails for some other reason entirely.
  void AddPassFailTrapSpeed(float incomingSpeed_mps) {
    passFailTrapSpeedSum_mps += incomingSpeed_mps;
    passFailTrapSpeedCount++;
  }
  float GetPassFailTrapMeanSpeed_mps() const {
    if (passFailTrapSpeedCount == 0) return 0.0f;
    return passFailTrapSpeedSum_mps / static_cast<float>(passFailTrapSpeedCount);
  }
  float passFailTrapSpeedSum_mps = 0.0f;
  int passFailTrapSpeedCount = 0;
  int GetPassFailTrapSpeedCount() const { return passFailTrapSpeedCount; }
  // What the failed receptions looked like. Measured pace of the pass is not
  // enough: possession needs the ball within 6 m/s of the receiver, and at
  // the ~10 m/s these actually arrive at, a receiver standing still cannot
  // satisfy that however well the pass was weighted. So record the receiver's
  // own speed at the touch and the `bumpyRideBias` that decides how much of
  // the ball's pace survives it. Low speed and high bias together mean the
  // approach, not the pass.
  void AddPassFailReceptionContext(float receiverSpeed_mps, float bumpyRideBias) {
    passFailReceiverSpeedSum_mps += receiverSpeed_mps;
    passFailBumpyRideBiasSum += bumpyRideBias;
    passFailReceptionCount++;
  }
  int GetPassFailReceptionCount() const { return passFailReceptionCount; }
  float GetPassFailMeanReceiverSpeed_mps() const {
    if (passFailReceptionCount == 0) return 0.0f;
    return passFailReceiverSpeedSum_mps / static_cast<float>(passFailReceptionCount);
  }
  float GetPassFailMeanBumpyRideBias() const {
    if (passFailReceptionCount == 0) return 0.0f;
    return passFailBumpyRideBiasSum / static_cast<float>(passFailReceptionCount);
  }
  float passFailReceiverSpeedSum_mps = 0.0f;
  float passFailBumpyRideBiasSum = 0.0f;
  int passFailReceptionCount = 0;
  // Which of AI_HasPossession's four checks rejected the reception. Sampled at
  // the touch the ball is still in the air on a lofted pass, so a 0.8 m contact
  // reads as a failed trap before it has had a tick to settle - which would
  // make this bucket count receptions that were about to succeed. Recording
  // the height and the distance at the touch says which gate actually closed.
  void AddPassFailGate(float ballZ_m, float ballDistance_m) {
    passFailGateBallZSum_m += ballZ_m;
    passFailGateDistanceSum_m += ballDistance_m;
    if (ballZ_m > 0.5f) passFailGateHeightCount++;
    if (ballDistance_m > 1.0f) passFailGateDistanceCount++;
    passFailGateCount++;
  }
  int GetPassFailGateCount() const { return passFailGateCount; }
  int GetPassFailGateHeightCount() const { return passFailGateHeightCount; }
  int GetPassFailGateDistanceCount() const { return passFailGateDistanceCount; }
  float GetPassFailGateMeanBallZ_m() const {
    if (passFailGateCount == 0) return 0.0f;
    return passFailGateBallZSum_m / static_cast<float>(passFailGateCount);
  }
  float GetPassFailGateMeanDistance_m() const {
    if (passFailGateCount == 0) return 0.0f;
    return passFailGateDistanceSum_m / static_cast<float>(passFailGateCount);
  }
  float passFailGateBallZSum_m = 0.0f;
  float passFailGateDistanceSum_m = 0.0f;
  int passFailGateHeightCount = 0;
  int passFailGateDistanceCount = 0;
  int passFailGateCount = 0;
  float GetKeeperBeatenMeanLateral_m(int teamID) const {
    if (keeperBeatenCount[teamID] == 0) return 0.0f;
    return keeperBeatenLateralSum_m[teamID] / static_cast<float>(keeperBeatenCount[teamID]);
  }
  float GetKeeperBeatenMeanOverhead_m(int teamID) const {
    if (keeperBeatenCount[teamID] == 0) return 0.0f;
    return keeperBeatenOverheadSum_m[teamID] / static_cast<float>(keeperBeatenCount[teamID]);
  }
  int GetKeeperBeatenOutOfTime(int teamID) const { return keeperBeatenOutOfTime[teamID]; }
  int GetKeeperBeatenOutOfReach(int teamID) const { return keeperBeatenOutOfReach[teamID]; }
  float GetKeeperBeatenMeanGap_m(int teamID) const {
    if (keeperBeatenCount[teamID] == 0) return 0.0f;
    return keeperBeatenGapSum_m[teamID] / static_cast<float>(keeperBeatenCount[teamID]);
  }

  // Where the ball actually reached the goal-line plane, measured tick by tick
  // (GameplayTuning::FindGoalLineCrossing). `onTarget` counts the crossings
  // inside the posts and under the bar. Kept beside the projected on-target
  // counter because the projection is clamped by the same band it would be
  // checking: the two disagreeing is the interesting signal.
  void AddGoalLineCrossing(int teamID, bool onTarget) {
    goalLineCrossings[teamID] += 1;
    if (onTarget) goalLineCrossingsOnTarget[teamID] += 1;
  }
  int GetGoalLineCrossings(int teamID) const { return goalLineCrossings[teamID]; }
  int GetGoalLineCrossingsOnTarget(int teamID) const { return goalLineCrossingsOnTarget[teamID]; }

  // Crossings that happened with no shot inside the flight window: a ball
  // reaching the line off a pass, a deflection or a rebound. Counted separately
  // so the shot-gated column stays a statement about shots, and so
  // `goals <= crossings in frame` can be checked rather than assumed.
  void AddGoalLineCrossingWithoutShot(int teamID, bool onTarget) {
    goalLineCrossingsUnattributed[teamID] += 1;
    if (onTarget) goalLineCrossingsUnattributedOnTarget[teamID] += 1;
  }
  int GetGoalLineCrossingsUnattributed(int teamID) const {
    return goalLineCrossingsUnattributed[teamID];
  }
  int GetGoalLineCrossingsUnattributedOnTarget(int teamID) const {
    return goalLineCrossingsUnattributedOnTarget[teamID];
  }

  // Is a struck ball still travelling? A shot's flight from 30 m at 30 m/s is
  // about a second and a lofted one three, so five seconds covers the arrivals
  // and excludes a throw-in or a goal kick trundling over the line.
  static constexpr unsigned long shotFlightWindow_ms = 5000;
  bool ShotInFlight() const {
    return lastShotTeamID >= 0 && matchTime_ms - lastShotTime_ms <= shotFlightWindow_ms;
  }
  int GetShotTeamID() const { return lastShotTeamID; }

  // pass tracking
  void AddPassAttempt(int teamID) {
    RecordBallAction();
    RecordPassOverwritten();
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
    RecordBallAction();
    RecordPassOverwritten();
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
  // Anything that is a deliberate act on the ball: a touch, a pass, a clearance.
  // What the stalemate clock measures - the owner watched a first half stand
  // from 32:19 to 45:00 and it must never happen again.
  void RecordBallAction() { lastBallActionTime_ms = matchTime_ms; }
  // Seconds the ball has gone untouched, on the in-play clock.
  float GetSecondsSinceBallAction() const {
    return static_cast<float>(matchTime_ms - lastBallActionTime_ms) / 1000.0f;
  }

  void RecordPassGoalkeeperCatch() {
    if (pendingPassTeamID >= 0) passGoalkeeperCatch[pendingPassTeamID]++;
    ResetPendingPass();
  }
  void RecordPassRestart() {
    if (pendingPassTeamID >= 0) passRestart[pendingPassTeamID]++;
    ResetPendingPass();
  }
  int GetPassGoalkeeperCatch(int teamID) const { return passGoalkeeperCatch[teamID]; }
  int GetPassRestart(int teamID) const { return passRestart[teamID]; }
  // A new pass or clearance opened while the last pass was still pending:
  // that pass ended without any recorded outcome. The last of the sinks, so
  // completed + intercept + out + gk + restart + overwritten must equal the
  // attempts, and any shortfall is a path nothing here has found.
  void RecordPassOverwritten() {
    if (pendingPassTeamID >= 0) passOverwritten[pendingPassTeamID]++;
  }
  int GetPassOverwritten(int teamID) const { return passOverwritten[teamID]; }
  int GetPassShotPending(int teamID) const { return passShotPending[teamID]; }

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
    RecordBallAction();
    // Taken off an opponent in open play, with nothing in flight to intercept:
    // that is a tackle. Decided before lastTouchTeamID is moved on.
    bool wonFromOpponent = lastTouchTeamID >= 0 && lastTouchTeamID != receivingTeamID &&
                           pendingPassTeamID < 0;
    // A completion inside its window losing the ball to another team: note
    // when. Lost within a moment is a first touch; lost seconds later is
    // pressure, and they are different problems.
    if (pendingPassTeamID < 0)
      for (PendingCleanCheck& check : pendingCleanChecks)
        if (check.teamID != receivingTeamID && !check.lostTimeRecorded) {
          check.lostTime_ms = matchTime_ms - check.arrivalTime_ms;
          check.lostTimeRecorded = true;
        }
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
      pendingCleanChecks.push_back(
          {receivingTeamID, matchTime_ms + cleanCompletionWindow_ms, matchTime_ms});
      wonFromOpponent = false;
    } else if (pendingPassTeamID >= 0) {
      // The ball was cut out of a pass in flight: an interception for whoever
      // met it, whichever build this is.
      interceptions[receivingTeamID]++;
      wonFromOpponent = false;
      // The breakdown counter is release-safe: the tuning batches run a release
      // build, and a quality signal that vanishes with the assertions cannot
      // steer pass accuracy from 65% toward the 80% target. The signals below
      // stay debug-only - they are quality data, not rules.
      passFailIntercept[pendingPassTeamID]++;  // breakdown: intercepted in flight
      // A pending pass met by the OTHER team: the passer just played it straight to
      // an opponent. Debug-only - this is a quality signal, not a rule.
#ifndef NDEBUG
      AddBadPassToOpponent(pendingPassTeamID);
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
      else {
        scrappyCompletions[completingTeam]++;
        // Only meaningful when a touch actually displaced it. A window that
        // simply expired with the ball still theirs is the clean case.
        if (pendingCleanChecks.front().lostTimeRecorded) {
          completionLossTimeSum_ms += pendingCleanChecks.front().lostTime_ms;
          completionLossCount++;
          if (pendingCleanChecks.front().lostTime_ms < 400) completionLossFastCount++;
        }
      }
      pendingCleanChecks.erase(pendingCleanChecks.begin());
    }
  }
  int GetCleanCompletions(int teamID) const { return cleanCompletions[teamID]; }
  int GetScrappyCompletions(int teamID) const { return scrappyCompletions[teamID]; }
  int GetCompletionLossCount() const { return completionLossCount; }
  int GetCompletionLossFastCount() const { return completionLossFastCount; }
  float GetCompletionLossMean_ms() const {
    if (completionLossCount == 0) return 0.0f;
    return static_cast<float>(completionLossTimeSum_ms) /
           static_cast<float>(completionLossCount);
  }
  int GetPendingCleanCheckCount() const { return static_cast<int>(pendingCleanChecks.size()); }
  // The ball left the pitch while a pass was still in flight: count it as an
  // out-of-bounds failure for the passer, then close the passing sequence.
  // The breakdown counter is release-safe (see the intercept one above).
  void FailPendingPassOutOfBounds() {
    if (pendingPassTeamID >= 0)
      passFailOob[pendingPassTeamID]++;
#ifndef NDEBUG
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
  // The match's timeline, for the pause menu's events card and the post-match
  // pages: what happened, to whom, when. Minute is the match clock's.
  struct Event {
    enum Kind { Goal, OwnGoal, Foul, YellowCard, RedCard, Substitution };
    Kind kind;
    int teamID;
    int minute;
    std::string text;  // scorer, offender, or "out -> in"
    // Only meaningful for Goal: a direct penalty conversion, sourced from the
    // referee's own penalty award and the taker who struck it (see
    // Match::SetPendingPenaltyTaken / IsPendingPenaltyGoal). Never true for
    // OwnGoal - there is no such thing as an own penalty.
    bool penalty = false;
  };
  void AddEvent(Event::Kind kind, int teamID, int minute, const std::string& text,
               bool penalty = false) {
    events.push_back({kind, teamID, minute, text, penalty});
  }
  const std::vector<Event>& GetEvents() const { return events; }

  void AddFoul(int teamID) { foulsCommitted[teamID]++; }
  int GetFouls(int teamID) const { return foulsCommitted[teamID]; }

  // The ball-winning challenges PES's engaging defender may pick
  // (docs/re/pes-re-defender-decisions.md 1): a standing tackle (the engine's
  // Interfere command reaching a contact) and a slide. Counted at the point the
  // challenge lands, so the report's tackle/slide figures are per match, not per
  // command queued.
  //
  // One challenge is one action start, so the counter is keyed on the last
  // challenge each player committed (player id, sliding flag) rather than on
  // the contact: `Referee::ContactEvent` can fire on every tick of a contact - a slide's
  // body is in the other man's way for a dozen frames and the tackle loop
  // reports each of them - and a challenge counted per contact reports
  // multiples of the actions actually taken.
  void AddChallenge(int teamID, bool sliding, int playerID) {
    const int key = playerID * 2 + (sliding ? 1 : 0);
    if (lastChallengeKey[teamID] == key) return;  // still the same challenge
    lastChallengeKey[teamID] = key;
    if (sliding)
      slidesAttempted[teamID]++;
    else
      tacklesAttempted[teamID]++;
  }
  // `GetTackles` above is PES's half-time ballot stat, counted by the ratings
  // layer; these are the challenges that actually landed (Referee::ContactEvent).
  int GetChallengesTackles(int teamID) const { return tacklesAttempted[teamID]; }
  int GetChallengesSlides(int teamID) const { return slidesAttempted[teamID]; }
  int GetYellowCards(int teamID) const { return yellowCards[teamID]; }
  int GetRedCards(int teamID) const { return redCards[teamID]; }
  void AddCard(int teamID, int foulType) {
    if (foulType == 2)
      yellowCards[teamID]++;
    else if (foulType == 3)
      redCards[teamID]++;
  }

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
  // `onTarget` is the caller's verdict on whether the ball it is stopping was
  // heading inside the frame (GameplayTuning::PredictGoalLineCrossing, from the
  // ball state at the moment of contact). Football counts a save only for a
  // shot that was on target - a keeper collecting a ball going wide is a
  // collection, not a save - and without the gate this counter read eight a
  // team per match, three times a real one. The touches that are not saves are
  // still counted, so nothing is lost.
  void RecordGoalkeeperTouch(int keeperTeamID, bool onTarget) {
    if (pendingShotTeamID >= 0 && pendingShotTeamID != keeperTeamID &&
        matchTime_ms - pendingShotTime_ms <= saveWindow_ms) {
      if (onTarget)
        saves[keeperTeamID]++;
      else
        keeperCollections[keeperTeamID]++;
    }
    pendingShotTeamID = -1;
  }
  int GetKeeperCollections(int teamID) const { return keeperCollections[teamID]; }

  // What the keeper DID with the ball he reached, which the save count cannot
  // say: a hold and a parry are both recorded as saves, and a parry put back
  // into play is how an easy catch becomes a goal. The owner reports him
  // "missing REALLY easy catches" and nothing here could tell a parry from a
  // catch, or say how hard the ball was when he let it go.
  void RecordKeeperHold(int keeperTeamID) { keeperHolds[keeperTeamID]++; }
  // `withinCatchSpeed` is judged by the caller, not here: this layer stays
  // engine-independent and knows nothing of the catching model.
  void RecordKeeperParry(int keeperTeamID, float relativeSpeed_ms, bool withinCatchSpeed) {
    keeperParries[keeperTeamID]++;
    keeperParrySpeedSum_ms[keeperTeamID] += relativeSpeed_ms;
    keeperParrySpeedCount[keeperTeamID]++;
    // Inside his own catch speed a parry is not a judgement about pace at all -
    // it is him letting the ball go when he could have held it. Counted
    // separately, because that is the case the complaint is about.
    if (withinCatchSpeed)
      keeperSlowParries[keeperTeamID]++;
  }
  int GetKeeperHolds(int teamID) const { return keeperHolds[teamID]; }
  int GetKeeperParries(int teamID) const { return keeperParries[teamID]; }
  int GetKeeperSlowParries(int teamID) const { return keeperSlowParries[teamID]; }
  // How often the deflect animation itself vetoed the catch, before pace was
  // even considered. Measured at 8 of 20 keeper touches in two matches, which
  // is the share of "easy catches" he was pushing away by script rather than by
  // judgement.
  void RecordKeeperAnimVeto(int keeperTeamID) { keeperAnimVeto[keeperTeamID]++; }
  int GetKeeperAnimVeto(int teamID) const { return keeperAnimVeto[teamID]; }
  float GetKeeperParryMeanSpeed_ms(int teamID) const {
    return keeperParrySpeedCount[teamID] > 0
               ? keeperParrySpeedSum_ms[teamID] / keeperParrySpeedCount[teamID]
               : 0.0f;
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
  void AddPassFailBadTrap(int teamID) { passFailTrap[teamID]++; }

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

  // Touches that never reach RecordBallTouch (interfere, failed deflect,
  // slide, body collision, keeper retention) while a pass is still pending.
  // These are the leaks the [pass-fail] breakdown cannot see; attributed to
  // the team whose pass was in flight, split by whether the toucher was an
  // opponent. Kinds: 0 interfere, 1 deflect-fail, 2 slide, 3 body collision,
  // 4 keeper-retain. NOT debug-gated: accuracy minus (intercept + out) left
  // ~21 points of passes with no recorded end in the Release batches, and
  // this is the census that says where they went.
  void AddGhostTouch(int touchingTeamID, int kind) {
    if (pendingPassTeamID < 0 || kind < 0 || kind >= ghostKindCount) return;
    const bool hostile = touchingTeamID != pendingPassTeamID;
    ghostTouches[hostile ? 1 : 0][pendingPassTeamID][kind]++;
  }
  static constexpr int ghostKindCount = 5;
  int GetGhostTouch(int hostile, int teamID, int kind) const {
    return ghostTouches[hostile][teamID][kind];
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
  int keeperCollections[2] = {0, 0};
  int keeperHolds[2] = {0, 0};
  int keeperParries[2] = {0, 0};
  int keeperSlowParries[2] = {0, 0};
  int keeperAnimVeto[2] = {0, 0};
  float keeperParrySpeedSum_ms[2] = {0.0f, 0.0f};
  int keeperParrySpeedCount[2] = {0, 0};
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
    unsigned long arrivalTime_ms;
    unsigned long lostTime_ms = 0;
    bool lostTimeRecorded = false;
  };
  int cleanCompletions[2] = {0, 0};
  int scrappyCompletions[2] = {0, 0};
  // When a completion lost the ball, and how fast: a reception gone in a
  // fraction of a second is a first-touch problem, one lost later is pressure.
  unsigned long completionLossTimeSum_ms = 0;
  int completionLossCount = 0;
  int completionLossFastCount = 0;
  // Which team touched the ball last, updated on every RecordBallTouch call.
  int lastTouchTeamID = -1;
  static constexpr unsigned long cleanCompletionWindow_ms = 1500;
  std::vector<PendingCleanCheck> pendingCleanChecks;
  std::vector<Event> events;
  int passGoalkeeperCatch[2] = {0, 0};
  int passRestart[2] = {0, 0};
  int passOverwritten[2] = {0, 0};
  int passShotPending[2] = {0, 0};
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
  // Match clock at the last deliberate act on the ball. Equal to matchTime_ms at
  // kickoff, so a match that never gets going reads as idle immediately.
  unsigned long lastBallActionTime_ms = 0;
  unsigned long possessionTime_ms[2];
  float possession60seconds;  // -600 to 600 for possession of team 1 / 2 respectively

  int shots[2];
  int shotsOnTarget[2];
  int shotDistanceBands[2][shotDistanceBandCount] = {};
  int shotMisses[2][shotMissKindCount] = {};
  float shotDistanceSum_m[2] = {0.0f, 0.0f};
  int keeperBeatenOutOfTime[2] = {0, 0};
  int keeperBeatenOutOfReach[2] = {0, 0};
  float keeperBeatenGapSum_m[2] = {0.0f, 0.0f};
  int keeperBeatenSavedByStanding[2] = {0, 0};
  int keeperBeatenSavedByReaction[2] = {0, 0};
  int keeperBeatenWhatIfCount[2] = {0, 0};
  float keeperBeatenLateralSum_m[2] = {0.0f, 0.0f};
  float keeperBeatenOverheadSum_m[2] = {0.0f, 0.0f};
  int keeperBeatenCount[2] = {0, 0};
  int shotGeometryCount[2] = {0, 0};
  int goalLineCrossings[2];
  int goalLineCrossingsOnTarget[2];
  int goalLineCrossingsUnattributed[2];
  int goalLineCrossingsUnattributedOnTarget[2];
  int lastShotTeamID = -1;
  unsigned long lastShotTime_ms = 0;

  int passAttempts[2];
  int clearances[2];
  int passesCompleted[2];
  int pendingPassTeamID;

  int foulsCommitted[2];
  // PES's challenges, counted where they land (see AddChallenge).
  int tacklesAttempted[2];
  int slidesAttempted[2];
  // The last challenge counted for this team, as playerID * 2 + sliding, so a
  // contact that outlasts its own action is not counted twice.
  int lastChallengeKey[2] = {-1, -1};
  int yellowCards[2];
  int redCards[2];

  bool historySaved;
};

#endif

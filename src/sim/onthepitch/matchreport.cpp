// The report block is GF's menu/ingame/gameover.cpp (GameOverPage::Process),
// moved here verbatim apart from printing to `out`.

#include "matchreport.hpp"

#include <cmath>
#include <cstring>

#include "../data/matchanalytics.hpp"
#include "match.hpp"

namespace {

// FNV-1a 64-bit parameters (Fowler/Noll/Vo).
constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

uint64_t FoldBytes(uint64_t hash, const void* data, size_t size) {
  const unsigned char* bytes = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < size; i++) {
    hash ^= bytes[i];
    hash *= kFnvPrime;
  }
  return hash;
}

uint64_t FoldVector(uint64_t hash, const Vector3& v) {
  return FoldBytes(hash, v.coords, sizeof(v.coords));
}

int PossessionPercent(MatchData* matchData, int teamID) {
  const float mine = static_cast<float>(matchData->GetPossessionTime_ms(teamID));
  const float total = mine + static_cast<float>(matchData->GetPossessionTime_ms(1 - teamID));
  return total > 0.0f ? static_cast<int>(std::round(mine / total * 100.0f)) : 50;
}

}  // namespace

uint64_t DeterminismHashSeed() {
  return kFnvOffsetBasis;
}

uint64_t FoldDeterminism(uint64_t hash, Match& match) {
  const uint64_t tick = match.GetSimTime_ms() / Match::kTick_ms;
  hash = FoldBytes(hash, &tick, sizeof(tick));
  hash = FoldVector(hash, match.GetBall()->Predict(0));
  for (int teamID = 0; teamID < 2; teamID++) {
    std::vector<Player*> players;
    match.GetActiveTeamPlayers(teamID, players);
    for (Player* player : players) hash = FoldVector(hash, player->GetPosition());
  }
  return hash;
}

void PrintMatchReport(Match& match, FILE* out) {
  // Short names: GF's simbatch reads the teams as single tokens around the score.
  std::fprintf(out, "[gfsim] Full match complete: %s %i - %i %s\n",
               match.GetTeam(0)->GetTeamData()->GetShortName().c_str(),
               match.GetMatchData()->GetGoalCount(0), match.GetMatchData()->GetGoalCount(1),
               match.GetTeam(1)->GetTeamData()->GetShortName().c_str());
  MatchData* matchData = match.GetMatchData();
  const int passes1 = matchData->GetPassAttempts(0);
  const int passes2 = matchData->GetPassAttempts(1);
  const int passAccuracy1 = passes1 > 0 ? (matchData->GetPassesCompleted(0) * 100) / passes1 : 0;
  const int passAccuracy2 = passes2 > 0 ? (matchData->GetPassesCompleted(1) * 100) / passes2 : 0;
  const int cleanPct1 = passes1 > 0 ? (matchData->GetCleanCompletions(0) * 100) / passes1 : 0;
  const int cleanPct2 = passes2 > 0 ? (matchData->GetCleanCompletions(1) * 100) / passes2 : 0;
  std::fprintf(out, "[balance-passing] passes %i-%i | accuracy %i%%-%i%% | clean %i%%-%i%% | clearances %i-%i\n",
         passes1, passes2, passAccuracy1, passAccuracy2, cleanPct1, cleanPct2,
         matchData->GetClearances(0), matchData->GetClearances(1));
  // Failure breakdown: where the incomplete passes actually went. Release-safe
  // so the accuracy target can be worked on in the build the batches run.
  std::fprintf(out, "[pass-fail] intercept %i-%i out %i-%i trap %i-%i\n",
         matchData->GetPassFailIntercept(0), matchData->GetPassFailIntercept(1),
         matchData->GetPassFailOutOfBounds(0), matchData->GetPassFailOutOfBounds(1),
         matchData->GetPassFailBadTrap(0), matchData->GetPassFailBadTrap(1));
  // Stalemate recovery: ticks on which the ball was forced to the nearest man
  // because it had lain untouched and nobody was playing it. Zero is the only
  // good answer; a non-zero count says a match stood still and says how often.
  std::fprintf(out, "[stalemate] forced %lu\n", match.GetStalemateCount());
  // Touches that never reached RecordBallTouch while a pass was pending:
  // the sink the [pass-fail] breakdown cannot see. hostile = an opponent's
  // body killed our pass in flight; kinds are interfere/deflect/slide/
  // collision/keeper. Release-safe with [pass-gk] and [pass-restart]: with
  // intercept and out these are every end a pass can have, so together
  // they account for the gap between attempts and completions.
  std::fprintf(out, "[pass-ghost] friendly %i/%i/%i/%i/%i-%i/%i/%i/%i/%i | hostile %i/%i/%i/%i/%i-%i/%i/%i/%i/%i\n",
         matchData->GetGhostTouch(0, 0, 0), matchData->GetGhostTouch(0, 0, 1),
         matchData->GetGhostTouch(0, 0, 2), matchData->GetGhostTouch(0, 0, 3),
         matchData->GetGhostTouch(0, 0, 4), matchData->GetGhostTouch(0, 1, 0),
         matchData->GetGhostTouch(0, 1, 1), matchData->GetGhostTouch(0, 1, 2),
         matchData->GetGhostTouch(0, 1, 3), matchData->GetGhostTouch(0, 1, 4),
         matchData->GetGhostTouch(1, 0, 0), matchData->GetGhostTouch(1, 0, 1),
         matchData->GetGhostTouch(1, 0, 2), matchData->GetGhostTouch(1, 0, 3),
         matchData->GetGhostTouch(1, 0, 4), matchData->GetGhostTouch(1, 1, 0),
         matchData->GetGhostTouch(1, 1, 1), matchData->GetGhostTouch(1, 1, 2),
         matchData->GetGhostTouch(1, 1, 3), matchData->GetGhostTouch(1, 1, 4));
  std::fprintf(out, "[pass-gk] %i-%i\n", matchData->GetPassGoalkeeperCatch(0),
         matchData->GetPassGoalkeeperCatch(1));
  std::fprintf(out, "[pass-restart] %i-%i\n", matchData->GetPassRestart(0),
         matchData->GetPassRestart(1));
  // The engaging defender's output: the reaction table's challenges, and what
  // the referee made of them. tackles/slides count one per challenge committed
  // (an action start, however many ticks its contact lasts); fouls count
  // whistles; cards count the yellows and reds actually shown.
  std::fprintf(out, "[challenges] tackles %i-%i | slides %i-%i | fouls %i-%i | yellow %i-%i | red %i-%i\n",
         matchData->GetChallengesTackles(0), matchData->GetChallengesTackles(1),
         matchData->GetChallengesSlides(0), matchData->GetChallengesSlides(1), matchData->GetFouls(0), matchData->GetFouls(1),
         matchData->GetYellowCards(0), matchData->GetYellowCards(1),
         matchData->GetRedCards(0), matchData->GetRedCards(1));
  std::fprintf(out, "[pass-overwritten] %i-%i | shot-pending %i-%i\n", matchData->GetPassOverwritten(0),
         matchData->GetPassOverwritten(1), matchData->GetPassShotPending(0),
         matchData->GetPassShotPending(1));
#ifndef NDEBUG
  // Questionable-play deny list, debug-only: no quality guarantee should depend on
  // somebody counting frames by hand.
  std::fprintf(out, "[deny-list] pass-to-opponent %i-%i | gk-lost %i-%i | own-third-giveaway %i-%i | bad-plays %i\n",
         matchData->GetBadPassToOpponent(0), matchData->GetBadPassToOpponent(1),
         matchData->GetGoalkeeperLost(0), matchData->GetGoalkeeperLost(1),
         matchData->GetOwnThirdGiveaway(0), matchData->GetOwnThirdGiveaway(1),
         matchData->GetBadPlayTotal());
  // Chosen pass-length distribution and support-web width for the whole
  // match, next to the failure breakdown: selection and execution on one
  // card.
  std::fprintf(out, "[pass-dist] bands %i/%i/%i/%i/%i/%i | rms %.1fm-%.1fm | web %.1fm-%.1fm\n",
         matchData->GetPassDistanceBand(0, 0), matchData->GetPassDistanceBand(0, 1),
         matchData->GetPassDistanceBand(0, 2), matchData->GetPassDistanceBand(0, 3),
         matchData->GetPassDistanceBand(0, 4), matchData->GetPassDistanceBand(0, 5),
         matchData->GetPassDistanceMeanRms_m(0), matchData->GetPassDistanceMeanRms_m(1),
         matchData->GetSupportWebWidthMean_m(0), matchData->GetSupportWebWidthMean_m(1));
#endif
  std::fprintf(out, "[balance-offside] offsides %i-%i | free kicks %i-%i\n",
         matchData->GetOffsides(0), matchData->GetOffsides(1),
         matchData->GetFreeKicks(0), matchData->GetFreeKicks(1));
  std::fprintf(out,
      // Saves are in here because they are the lever between on-target and
      // goals: on-target minus saves minus goals is what the keeper never
      // touched, and a keeper stopping 85% of the target is a different
      // problem from a side that cannot hit it (the stats card shows the same
      // number, so this is telemetry, not instrumentation).
      "[balance] shots %i-%i | on target %i-%i | saves %i-%i | xg %.2f-%.2f | goals %i-%i | "
      // `crossings` is the same question asked of the ball's own positions
      // instead of the launch sum: how many shots actually reached the
      // goal-line plane, and how many of those inside the posts. The
      // projection is clamped by the band it checks, so only this column can
      // confirm the band.
      "crossings %i-%i (in frame %i-%i) | "
      // Crossings with no shot in the flight window (a pass or a deflection
      // reaching the line): the audit that `goals <= in frame` holds.
      "crossings-no-shot %i-%i (in frame %i-%i) | "
      // Football's own definition: a shot is on target when it went in or the
      // keeper had to stop it. The saves above are gated on the ball actually
      // heading inside the frame, so this is not the projection restated - and
      // it is the column to compare against the ~20-30% a real match sees.
      "on target (goals+saves) %i-%i | "
      // The gate's audit: keeper touches that were NOT saves because the ball
      // was not heading inside the frame. If this is ~0 the gate is rejecting
      // nothing and the column above is just "any keeper touch".
      "keeper collections %i-%i | "
      "possession %i%%-%i%%\n",
      matchData->GetShots(0), matchData->GetShots(1), matchData->GetShotsOnTarget(0),
      matchData->GetShotsOnTarget(1), matchData->GetSaves(0), matchData->GetSaves(1),
      MatchAnalytics::GetExpectedGoals(match.GetShotTally(), 0),
      MatchAnalytics::GetExpectedGoals(match.GetShotTally(), 1), matchData->GetGoalCount(0),
      matchData->GetGoalCount(1), matchData->GetGoalLineCrossings(0),
      matchData->GetGoalLineCrossings(1), matchData->GetGoalLineCrossingsOnTarget(0),
      matchData->GetGoalLineCrossingsOnTarget(1), matchData->GetGoalLineCrossingsUnattributed(0),
      matchData->GetGoalLineCrossingsUnattributed(1),
      matchData->GetGoalLineCrossingsUnattributedOnTarget(0),
      matchData->GetGoalLineCrossingsUnattributedOnTarget(1),
      // Crossed: `saves[i]` are the saves team i's keeper MADE, so team i's
      // own shots on target are its goals plus the saves the OTHER keeper made.
      matchData->GetGoalCount(0) + matchData->GetSaves(1),
      matchData->GetGoalCount(1) + matchData->GetSaves(0), matchData->GetKeeperCollections(0),
      matchData->GetKeeperCollections(1), PossessionPercent(matchData, 0),
      PossessionPercent(matchData, 1));
  // Shot quality, the other half of the balance question: where the shots
  // came from (bands: inside 11 m, the rest of the box, just outside, long
  // range) and, for the ones the projection calls off target, which way they
  // missed. `^[balance` is what the batch runner greps, so this rides along
  // with the line above.
  std::fprintf(out, "[balance-shots] bands %i/%i/%i/%i-%i/%i/%i/%i | mean %.1fm-%.1fm | "
         "wide %i-%i | over %i-%i | short %i-%i\n",
         matchData->GetShotDistanceBand(0, 0), matchData->GetShotDistanceBand(0, 1),
         matchData->GetShotDistanceBand(0, 2), matchData->GetShotDistanceBand(0, 3),
         matchData->GetShotDistanceBand(1, 0), matchData->GetShotDistanceBand(1, 1),
         matchData->GetShotDistanceBand(1, 2), matchData->GetShotDistanceBand(1, 3),
         matchData->GetShotDistanceMean_m(0), matchData->GetShotDistanceMean_m(1),
         matchData->GetShotMiss(0, 0), matchData->GetShotMiss(1, 0),
         matchData->GetShotMiss(0, 1), matchData->GetShotMiss(1, 1),
         matchData->GetShotMiss(0, 2), matchData->GetShotMiss(1, 2));
  // Why the keeper was beaten. The point-blank sweep says a real 4cc keeper
  // saves ~92% of the measured shot mix and matches say ~56%: the sweep
  // varies REACH with him central and on his line, so it cannot see a keeper
  // in the wrong place. "time" is beaten inside his own reaction (no dive
  // was available - reach cannot fix it); "reach" is beaten with the flight
  // time to move, and the mean gap says how far short he actually was.
  std::fprintf(out, "[balance-keeper] beaten out of time %i-%i | out of reach %i-%i | "
         "mean gap %.2fm-%.2fm (lateral %.2fm-%.2fm, overhead %.2fm-%.2fm)\n",
         matchData->GetKeeperBeatenOutOfTime(0), matchData->GetKeeperBeatenOutOfTime(1),
         matchData->GetKeeperBeatenOutOfReach(0), matchData->GetKeeperBeatenOutOfReach(1),
         matchData->GetKeeperBeatenMeanGap_m(0), matchData->GetKeeperBeatenMeanGap_m(1),
         matchData->GetKeeperBeatenMeanLateral_m(0), matchData->GetKeeperBeatenMeanLateral_m(1),
         matchData->GetKeeperBeatenMeanOverhead_m(0),
         matchData->GetKeeperBeatenMeanOverhead_m(1));
  // What he did with the balls he actually reached. The save count cannot tell
  // a catch from a parry, and a parry put back into play is how "an easy catch
  // he missed" actually happens. `slow` is the sharp number: a parry inside
  // his own catch speed is not a judgement about pace, it is him letting go of
  // a ball he could hold.
  std::fprintf(out, "[balance-keeper-touch] holds %i-%i | parries %i-%i (slow %i-%i, anim veto "
         "was %i-%i) | parry pace %.1f-%.1f m/s\n",
         matchData->GetKeeperHolds(0), matchData->GetKeeperHolds(1),
         matchData->GetKeeperParries(0), matchData->GetKeeperParries(1),
         matchData->GetKeeperSlowParries(0), matchData->GetKeeperSlowParries(1),
         matchData->GetKeeperAnimVeto(0), matchData->GetKeeperAnimVeto(1),
         matchData->GetKeeperParryMeanSpeed_ms(0),
         matchData->GetKeeperParryMeanSpeed_ms(1));
  // Which lever would have saved them. Census only - nothing is applied - and
  // it is what settles the keeper on one batch instead of one batch per
  // guess: if a slightly longer standing frame takes several of the
  // short-flight ones and a quicker read takes none, the keeper is a
  // framing problem; the reverse means it is a reaction problem.
  std::fprintf(out, "[balance-keeper-whatif] of %i-%i beaten, longer standing frame saves %i-%i, "
         "quicker read saves %i-%i\n",
         matchData->GetKeeperBeatenWhatIfCount(0), matchData->GetKeeperBeatenWhatIfCount(1),
         matchData->GetKeeperBeatenSavedByStanding(0), matchData->GetKeeperBeatenSavedByStanding(1),
         matchData->GetKeeperBeatenSavedByReaction(0), matchData->GetKeeperBeatenSavedByReaction(1));
  // The model has a cliff in it: GetTrapKillStrength scrubs at most half the
  // incoming pace, and possession then needs the ball within the receiver's
  // 8 m/s plus a 6 m/s tolerance, so above ~28 m/s (101 km/h) a trap cannot
  // succeed at all. This says whether the failures actually live up there.
  std::fprintf(out, "[balance-passfail] failed traps: mean incoming %.1f m/s over %i\n",
         matchData->GetPassFailTrapMeanSpeed_mps(),
         matchData->GetPassFailTrapSpeedCount());
  // Whether the failed receptions are the pass's fault or the approach's:
  // possession needs the ball within 6 m/s of the receiver, so a receiver
  // standing still cannot take an ordinary ~10 m/s pass however well it was
  // weighted. Slow receiver + high bumpyRideBias = the approach.
  std::fprintf(out, "[balance-passfail] failed receptions n=%i: receiver %.1f m/s, "
         "bumpyRideBias %.2f\n",
         matchData->GetPassFailReceptionCount(),
         matchData->GetPassFailMeanReceiverSpeed_mps(),
         matchData->GetPassFailMeanBumpyRideBias());
  // Which of AI_HasPossession's gates rejected the reception. A lofted pass
  // touched while still above 0.5 m fails on height before it has had a tick
  // to settle, which would make this bucket count receptions that were about
  // to succeed.
  std::fprintf(out, "[balance-passfail] gates over %i: height >0.5m %i | distance >1m %i | "
         "ball z %.2f m, distance %.2f m\n",
         matchData->GetPassFailGateCount(), matchData->GetPassFailGateHeightCount(),
         matchData->GetPassFailGateDistanceCount(),
         matchData->GetPassFailGateMeanBallZ_m(),
         matchData->GetPassFailGateMeanDistance_m());
  // Passes that arrived and were then lost inside the clean window: how long
  // the receiver held them. Under 400 ms is a first touch; longer is
  // pressure winning it back, which is a different problem.
  std::fprintf(out, "[balance-passloss] completions lost n=%i, mean %.0f ms after arrival, "
         "%i under 400 ms\n",
         matchData->GetCompletionLossCount(), matchData->GetCompletionLossMean_ms(),
         matchData->GetCompletionLossFastCount());

  std::fprintf(out, "[determinism] %016llx\n",
               static_cast<unsigned long long>(match.GetDeterminismHash()));
}

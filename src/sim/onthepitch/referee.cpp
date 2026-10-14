// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "referee.hpp"

#include <limits>

#include "../data/playerskills.hpp"
#include "simcontext.hpp"
#include "AIsupport/AIfunctions.hpp"
#include "foulseverity.hpp"
#include "foulsequence.hpp"
#include "goalsequence.hpp"
#include "match.hpp"
#include "officials.hpp"
#include "player/playerofficial.hpp"
#include "matchprogression.hpp"
#include "offsiderule.hpp"
#include "setpiecelaws.hpp"

// How long a match may sit with play off, nothing armed and the ball out of
// play before the referee takes the ball's own position and restarts it. Long
// enough that a normal stoppage - a goal, a foul, a substitution - has armed
// its own restart well inside it; short enough that nobody watching thinks the
// game has hung. It exists for measured stalls: four in one showcase, each
// running until the period ended.
static constexpr unsigned long kDeadBallRestart_ms = 4000;

// RE-SHIM: how far a restart may be pushed back from the moment play stopped.
// This is GF's own safety net, NOT a PES value - docs/re/pes-re-foul-choreography.md
// 9.8 records that nothing in PES's step program caps a restart deferral (the
// only 30 s clock in PES runs after the restart is armed, and only for human
// pads). It exists here because several things push the restart out per tick -
// a goal celebration's beats, a cutscene, the referee's card animation - and
// nothing used to stop any of them going on for ever. Measured: a goal kick
// armed and prepared with `started 0`, `start-in` pinned at exactly 3.0 s for
// 107 s, the whole match standing still from 26:05 while the clock ran on.
static constexpr unsigned long kMaxRestartDeferral_ms = 30000;

namespace {

// PES reads the on-pitch performer's "perf tier" out of its own team slice
// (TeamSlice + 0x4C00 + playerIndex*4, docs/re/pes-re-tactics-clock.md 2.4/3.2)
// to gate the added-time bonus. This engine carries no such field: the tier is
// therefore an explicit input with no default, and until a caller supplies one
// the added time is the plain per-match-second term only.
constexpr int kPesNoPerfTier = 0;

// A player this injured has stayed down (docs/re/pes-re-injury-fatigue.md:
// one hard trip is 0.08), which is PES's condition for the +2.5 s hold
// (docs/re/pes-re-foul-choreography.md 9.3, FUN_1404441d0).
// ponytail: an injury level is not PES's `player+0x34` down byte, which lives
// in a struct this engine does not carry; the level is what writes it. Move to
// the real flag if a downed-player state is ever modelled.
const float kPesDownedInjuryLevel = 0.01f;

// PES's late card (docs/re/pes-re-foul-choreography.md 9.5/9.8): the card
// prop's own start tick `REF_CARD_Y/R` on the scene, 5..411 ticks into the cut
// at 60 fps, most variants 40. Rounded to the 3.0 s the report gives.
constexpr unsigned long kPesCardOnScreen_ms = 3000;

// The late-card window PES opens (9.5): a pending card is dropped unless the
// next ball-out or offside comes within `MI+0x1690 ∈ 7..0x13` game-minutes.
// ponytail: PES's window unit (its clock float divided by 3600 - minutes or
// hours, which 9.10 lists as open) is not resolved, so this engine's window is
// the whole match: the card is owed until it is shown, which is Law 5 either
// way. Narrow it here when 9.10 closes.

// PES's hold table indices (FUN_14044e460, docs/re/pes-re-foul-choreography.md
// 4.2/9.3): step 5 is a foul, step 0xb is every out-of-play stop.
constexpr int kPesHoldFoul = 1;
constexpr int kPesHoldOutOfPlay = 5;

// PES 2021's offside whistle hold (docs/re/pes-re-duels-fouls.md 5.1): gs/2
// frames from the involvement to the whistle, where a gs is one second of
// frames (pes-re-foul-choreography.md 9.1: every timer is seconds x 54), so the
// hold is half a second. A contact foul's hold is the 0.8 s
// JudgeConstants::whistleHold_s; PES's offside hold is a frame count rather
// than a .rdata float, so it is carried here with its derivation. Category 2
// waits longer than this on top: DelayWhistle holds the whistle while a shot is
// in progress.
constexpr unsigned long kPesOffsideWhistleHold_ms = 500;

}  // namespace

// PES's player-down test (FUN_1404441d0, docs/re/pes-re-foul-choreography.md
// 9.3): any on-pitch player whose record byte `player+0x34` is set. The engine
// has no such byte, but it does carry the injury level the trip collision
// wrote, which is what puts a man on the ground in the first place.
bool AnyPlayerDown(Match* match) {
  std::vector<Player*> squad;
  match->GetActiveTeamPlayers(0, squad);
  match->GetActiveTeamPlayers(1, squad);
  for (Player* p : squad)
    if (p && p->GetInjuryLevel() >= kPesDownedInjuryLevel) return true;
  return false;
}

// Arms the referee's restart schedule for a PES stop (report 9.2/9.3/9.4). The
// whistle has already gone and play is off: the players act out the hold, the
// wipe and the teleport hide the cut back to their marks, and the fade-in and
// the tail run before the kick. Foul and out-of-play stops differ only in
// their hold table index; FoulSequence owns the bracket.
//   holdKind = 1  a foul (step 5, table[1] = 5.0 s)
//   holdKind = 5  an out-of-play stop (step 0xb, table[5] = 2.5 s)
void ScheduleStop(Referee* referee, unsigned long stopTime_ms, bool downed, int holdKind) {
  RefereeBuffer& b = referee->Buffer();
  const pes::StoppageConstants& k = referee->GetStoppageConstants();
  b.stopTime = stopTime_ms;
  b.holdTime = FoulSequence::HoldEndsAt_ms(stopTime_ms, k, downed, holdKind);
  b.prepareTime = FoulSequence::RestartPrepareAt_ms(stopTime_ms, k, downed, holdKind);
  b.startTime = FoulSequence::RestartTakeAt_ms(stopTime_ms, k, downed, holdKind);
}

Referee::Referee(Match* match) : match(match) {
  buffer.desiredSetPiece = e_SetPiece_KickOff;
  buffer.teamID = 0;
  buffer.stopTime = 0;
  buffer.holdTime = 0;
  buffer.prepareTime = 0;
  buffer.startTime = buffer.prepareTime + 2000;
  buffer.restartPos = Vector3(0, 0, 0);
  buffer.taker = nullptr;
  buffer.endPhase = true;
  buffer.prepared = false;
  buffer.started = false;
  buffer.active = true;
        buffer.prepared = false;
        buffer.started = false;

  // Referee temperament, configurable per match; "standard" reproduces the
  // historical thresholds.

  foul.foulPlayer = nullptr;
  foul.foulType = 0;
  foul.advantage = false;
  foul.foulTime = 0;
  foul.hasBeenProcessed = true;

  afterSetPieceRelaxTime_ms = 0;

  addedTimeAnnouncedPhase = e_MatchPhase_PreMatch;
  deadBallSince_ms = 0;
}

Referee::~Referee() {}

const pes::StoppageConstants& Referee::GetStoppageConstants() const {
  return match->GetPesStoppageConstants();
}

void Referee::Process() {
  // printf("%i", match->GetMatchState());

  if (match->IsInPlay() && !match->IsInSetPiece()) {
    Vector3 ballPos = match->GetBall()->Predict(0);

    // some phase is over :[

    // PES 2021's period end (docs/re/pes-re-tactics-clock.md 2.1/3.2): the
    // period length comes from the phase table, and the allowance is the linear
    // formula evaluated once at the end of the period - no per-event accrual.
    //
    // The fourth official's board, announced once per period from that formula
    // as the period reaches its length.
    if (match->GetPesClock().IsPeriodDue() && addedTimeAnnouncedPhase != match->GetMatchPhase()) {
      addedTimeAnnouncedPhase = match->GetMatchPhase();
      // The on-pitch performer's perf tier, which PES reads out of its team slice
      // at TeamSlice + 0x4C00 and this engine has no equivalent for: an explicit
      // input with no default, so the plain per-match-second term is announced
      // until a caller supplies a tier.
      const int addedMinutes = match->GetPesClock().AddedMinutes(kPesNoPerfTier);
      if (addedMinutes > 0 && Verbose()) printf("referee: added time +%i min\n", addedMinutes);
    }

    // Blow for the end of the period at a neutral moment - no earlier than the
    // period's length plus the formula's allowance, PES's period end plus its
    // added time - but never let a period run away waiting for one. Both sides
    // are period-relative: PES's half length is per period (45, not 90).
    const pes::PesClock& clock = match->GetPesClock();
    const auto periodEnd_ms =
        static_cast<unsigned long>(clock.PeriodEndWithAdded(kPesNoPerfTier) * 60000.0f);
    const auto periodElapsed_ms = static_cast<unsigned long>(clock.PeriodElapsedSeconds() * 1000.0f);
    if (MatchProgression::ShouldEndPeriod(periodElapsed_ms, periodEnd_ms, ballPos.coords[0])) {
      foul.advantage = false;
      if (!CheckFoul()) {
        match->StopPlay();

        buffer.desiredSetPiece = e_SetPiece_KickOff;
        buffer.stopTime = match->GetActualTime_ms();
        // The walk-off plays first; the reset for the next period waits for it
        // (SetPieceLaws::kHalfTimeCutscene_ms is also what SetMatchPhase gives
        // the cutscene, so the two cannot drift apart).
        buffer.prepareTime = SetPieceLaws::HalfTimePrepareAt_ms(match->GetActualTime_ms());
        buffer.startTime = SetPieceLaws::HalfTimeTakeAt_ms(match->GetActualTime_ms());
        buffer.restartPos = Vector3(0);
        buffer.active = true;
        buffer.prepared = false;
        buffer.started = false;
        buffer.endPhase = true;
        if (match->GetMatchPhase() == e_MatchPhase_1stHalf ||
            match->GetMatchPhase() == e_MatchPhase_1stExtraTime) {
          buffer.teamID = 1;
        } else {
          buffer.teamID = 0;
        }

        // Extra time only when the match is level, both extra periods always
        // played, penalties only after a draw in them.
        const MatchProgression::Outcome progression = MatchProgression::GetNext(
            match->GetMatchPhase(), match->GetScore(0) == match->GetScore(1));
        if (progression.gameOver) {
          match->GameOver();
          return;
        }
        match->SetMatchPhase(progression.nextPhase);
      }
    }

    // goal kick / corner

    if (fabs(ballPos.coords[0]) > pitchHalfW + lineHalfW + 0.11) {
      foul.advantage = false;
      bool isFoul = false;
      if (!match->IsGoalScored())
        isFoul = CheckFoul();
      else
        foul.foulType = 0;
      if (isFoul == false) {
        match->StopPlay();
        // A pending pass died here rather than by interception: count it as
        // out-of-bounds before the restart is set up.
        match->GetMatchData()->FailPendingPassOutOfBounds();

        // corner, goal kick or kick off?
        signed int lastSide = -1;
        Team* lastTouchTeam = match->GetLastTouchTeam();
        if (lastTouchTeam == nullptr)
          lastTouchTeam = match->GetTeam(0);
        lastSide = lastTouchTeam->GetSide();

        if (match->IsGoalScored()) {
          buffer.desiredSetPiece = e_SetPiece_KickOff;
          buffer.stopTime = match->GetActualTime_ms();
          // Preparing the restart calls Match::ResetSituation, which clears the
          // goal state the replay trigger waits on - so it has to come after the
          // replay has fired. GoalSequence owns both timings for that reason;
          // the six seconds this used to wait killed the goal replay outright.
          buffer.prepareTime = GoalSequence::RestartPrepareAt_ms(match->GetActualTime_ms());
          buffer.startTime = GoalSequence::RestartKickOffAt_ms(match->GetActualTime_ms());
          buffer.restartPos = Vector3(0, 0, 0);
          buffer.teamID = abs(match->GetLastGoalTeamID() - 1);

        } else if ((ballPos.coords[0] > 0 && lastSide > 0) ||
                   (ballPos.coords[0] < 0 && lastSide < 0)) {
          buffer.desiredSetPiece = e_SetPiece_Corner;
          buffer.stopTime = match->GetActualTime_ms();
          // PES's 0xb out-of-play stop (9.3): hold, then wipe, teleport and
          // fade-in before the kick. It used to be two and two - two seconds
          // to prepare and two to take - which is not PES's bracket at all.
          ScheduleStop(this, buffer.stopTime, AnyPlayerDown(match), kPesHoldOutOfPlay);
          float y = ballPos.coords[1];
          if (y > 0)
            y = pitchHalfH;
          else
            y = -pitchHalfH;
          buffer.restartPos = Vector3(pitchHalfW * lastSide, y, 0);
          buffer.teamID = abs(lastTouchTeam->GetID() - 1);
          match->GetMatchData()->AddCorner(buffer.teamID);

        } else {
          buffer.desiredSetPiece = e_SetPiece_GoalKick;
          buffer.stopTime = match->GetActualTime_ms();
          // PES's 0xb out-of-play stop (9.3). Live: 139 frames without a man
          // down, 274 with one - 2.5 s and 5.0 s.
          ScheduleStop(this, buffer.stopTime, AnyPlayerDown(match), kPesHoldOutOfPlay);
          buffer.restartPos = Vector3(pitchHalfW * 0.92 * -lastSide, 0, 0);
          buffer.teamID = abs(lastTouchTeam->GetID() - 1);
        }

        buffer.active = true;
        buffer.prepared = false;
        buffer.started = false;
      }
    }

    // over sideline

    if (afterSetPieceRelaxTime_ms == 0) {
      if (fabs(ballPos.coords[1]) > pitchHalfH + lineHalfW + 0.11) {
        foul.advantage = false;
        if (!CheckFoul()) {
          match->StopPlay();
          match->GetMatchData()->FailPendingPassOutOfBounds();

          Team* lastTouchTeam = match->GetLastTouchTeam();
          if (lastTouchTeam == nullptr)
            lastTouchTeam = match->GetTeam(0);
          buffer.teamID = abs(lastTouchTeam->GetID() - 1);
          buffer.desiredSetPiece = e_SetPiece_ThrowIn;
          buffer.stopTime = match->GetActualTime_ms();
          // PES's 0xb out-of-play stop (9.3). Live: 135 frames, 2.5 s - the
          // wipe alone runs 5 frames, and the CPU taker's own wait after the
          // restart is armed is his business, not the referee's.
          ScheduleStop(this, buffer.stopTime, AnyPlayerDown(match), kPesHoldOutOfPlay);
          buffer.restartPos.coords[0] =
              clamp(ballPos.coords[0], -pitchHalfW + 0.6f, pitchHalfW - 0.6f);
          if (ballPos.coords[1] > 0)
            buffer.restartPos.coords[1] = pitchHalfH;
          if (ballPos.coords[1] <= 0)
            buffer.restartPos.coords[1] = -pitchHalfH;
          buffer.restartPos.coords[2] = 0;
          buffer.active = true;
        buffer.prepared = false;
        buffer.started = false;
        }
      }
    }

    CheckFoul(true);
    // PES 2021's pending offside (docs/re/pes-re-duels-fouls.md 5.1): the
    // whistle waits out the gs/2 hold, and a category-2 flag waits for a shot
    // in progress (DelayWhistle).
    CommitPendingOffside();

  } else {  // not in play, maybe something needs to happen?
    // PES's Judge runs only while the ball is in play (5.1/5.2 step 1), so a
    // whistle never lands on a dead ball: drop a pending offside.
    pendingOffside.player = nullptr;

    // Play is not on and nothing is being prepared. Every out-of-play test in
    // this file lives inside `if (IsInPlay() && !IsInSetPiece())` above, so once
    // anything stops play without arming a restart - and several things do -
    // the ball is never looked at again. It sits beyond the goal line, every
    // controller is gated on IsInPlay and queues nothing, and the match stands
    // there until the period ends. Measured: four such stalls in one showcase,
    // all with inPlay 0 and the ball at x 55.1, and the chaser "forced" to it
    // three metres away that never moved because designating a chaser does
    // nothing while play is not live.
    //
    // So: play off, nothing armed, nothing moving - arm the restart the ball's
    // own position calls for. buffer.active is the guard that keeps this out of
    // the windows where waiting is correct: the entrance, half-time and the end
    // of the match all hold play with a restart already armed.
    if (!match->IsInPlay() && !match->IsInSetPiece() && !buffer.active) {
      const Vector3 ballPos = match->GetBall()->Predict(0);
      const bool outOverGoalLine = std::fabs(ballPos.coords[0]) > pitchHalfW;
      const bool outOverTouchline = std::fabs(ballPos.coords[1]) > pitchHalfH;
      // Not gated on the ball being out of play. The first version was, and it
      // fired zero times in four matches: the commonest stall by far is the ball
      // sitting ON THE CENTRE SPOT with play off and nothing armed - a kick-off
      // never prepared. Measured: "[stalemate] inPlay 0 ... at 0.0,0.0" with the
      // forced-chaser count to 17007 ticks in a match.
      if (deadBallSince_ms == 0) {
          deadBallSince_ms = match->GetActualTime_ms();
        } else if (match->GetActualTime_ms() - deadBallSince_ms >= kDeadBallRestart_ms) {
          Team* lastTouchTeam = match->GetLastTouchTeam();
          if (lastTouchTeam == nullptr)
            lastTouchTeam = match->GetTeam(0);
          buffer.teamID = abs(lastTouchTeam->GetID() - 1);
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 500;
          buffer.startTime = buffer.prepareTime + 1000;
          // One decision, in one tested place. It used to be spelled out inline
          // here, which is precisely why nothing could check it: this is the
          // branch only a live match reaches, so the arm that ends the freeze
          // was the one part with no test behind it.
          const SetPieceLaws::DeadBallDecision dead =
              SetPieceLaws::GetDeadBallRestart(ballPos.coords[0], ballPos.coords[1], pitchHalfW,
                                               pitchHalfH, lastTouchTeam->GetID(),
                                               lastTouchTeam->GetSide(),
                                               match->GetLastGoalTeamID());
          switch (dead.kind) {
            case SetPieceLaws::e_DeadBallRestart::ThrowIn:
              buffer.desiredSetPiece = e_SetPiece_ThrowIn;
              break;
            case SetPieceLaws::e_DeadBallRestart::Corner:
              buffer.desiredSetPiece = e_SetPiece_Corner;
              break;
            case SetPieceLaws::e_DeadBallRestart::GoalKick:
              buffer.desiredSetPiece = e_SetPiece_GoalKick;
              break;
            case SetPieceLaws::e_DeadBallRestart::KickOff:
              buffer.desiredSetPiece = e_SetPiece_KickOff;
              break;
            case SetPieceLaws::e_DeadBallRestart::None:
              break;
          }
          buffer.restartPos = Vector3(dead.posX, dead.posY, dead.posZ);
          buffer.active = true;
          buffer.prepared = false;
          buffer.started = false;
          deadBallSince_ms = 0;
          printf("[dead-ball] %.0f m from play with nothing armed: restarting with a %s "
                 "for team %d\n",
                 std::fabs(ballPos.coords[0]) > pitchHalfW
                     ? (ballPos.coords[0] > 0 ? pitchHalfW - ballPos.coords[0]
                                              : -pitchHalfW - ballPos.coords[0])
                     : (ballPos.coords[1] > 0 ? pitchHalfH - ballPos.coords[1]
                                              : -pitchHalfH - ballPos.coords[1]),
                 buffer.desiredSetPiece == e_SetPiece_ThrowIn   ? "throw-in"
                 : buffer.desiredSetPiece == e_SetPiece_Corner  ? "corner"
                 : buffer.desiredSetPiece == e_SetPiece_KickOff ? "kick-off"
                                                                : "goal kick",
                 buffer.teamID);
        }
    } else {
      deadBallSince_ms = 0;
    }

    if (!match->IsInPlay() && !match->IsInSetPiece() && buffer.active == true) {
      // Play has stopped: settle any bookings owed from advantages played.
      // PES's late-card step (9.5) runs the card scene from the stop, with a
      // 2.5 s timeout, so the booking is issued at 3000 ms rather than 300.
      if (CardBook::HasPending(cardBook) && buffer.stopTime + kPesCardOnScreen_ms <=
                                                 match->GetActualTime_ms())
        IssueDeferredCards();

      if (!buffer.prepared && buffer.prepareTime <= match->GetActualTime_ms()) {
        buffer.prepared = true;
        if (buffer.endPhase == true) {
          if (match->GetMatchPhase() == e_MatchPhase_PreMatch) {
            match->SetMatchPhase(e_MatchPhase_1stHalf);
          } else {
            if (match->GetMatchPhase() == e_MatchPhase_Penalties) {
              // The shootout controller decides when this is over.
              return;
            }
          }
          buffer.endPhase = false;
        }

        PrepareSetPiece(buffer.desiredSetPiece);
      }

      // Law 16: the taker waits for the opponents to leave the penalty area. They walk
      // out on their own (elizacontroller.cpp); this holds the whistle until they have,
      // for a bounded time so a wedged player cannot freeze the match.
      Team* restartTeam = match->GetTeam(buffer.teamID);
      if (!buffer.started && buffer.startTime <= match->GetActualTime_ms() &&
          SetPieceLaws::ClearsThePenaltyArea(buffer.desiredSetPiece, buffer.restartPos.coords[0],
                                             restartTeam->GetSide(), pitchHalfW)) {
        Team* takerTeam = restartTeam;
        Team* opponents = match->GetTeam(abs(buffer.teamID - 1));
        std::vector<Player*> squad;
        opponents->GetActivePlayers(squad);
        int intruders = 0;
        for (unsigned int i = 0; i < squad.size(); i++) {
          const Vector3 at = squad.at(i)->GetPosition();
          if (SetPieceLaws::IntrudesOnPenaltyArea(at.coords[0], at.coords[1],
                                                 takerTeam->GetSide(), pitchHalfW))
            intruders++;
        }
        (void)opponents;
        // How far past the area's edge the nearest of them is, for the release notice.
        // Negative while a man is still inside it.
        float nearest = 1e9f;
        for (unsigned int i = 0; i < squad.size(); i++) {
          const Vector3 at = squad.at(i)->GetPosition();
          const int side = takerTeam->GetSide();
          const float depthIn = pitchHalfW - at.coords[0] * static_cast<float>(side);
          const float pastDepth = depthIn - SetPieceLaws::kAreaDepth;
          const float pastWidth = std::fabs(at.coords[1]) - SetPieceLaws::kAreaHalfWidth;
          nearest = std::min(nearest, std::max(pastDepth, pastWidth));
        }
        if (nearest < 1e8f) {
          clearingNearest_m = nearest;
          clearingRule = "the area";
        }
        if (!clearingAnnounced) {
          clearingAnnounced = true;
          // One line per restart that the law touches: which kind, and how many of them
          // have to move. A restart that never reports anyone is a restart where the
          // area was already clear, which is worth being able to tell apart from a
          // clearing that silently did nothing.
          Log(e_Notice, "Referee", "Process",
              "clearing the area for set piece " + int_to_str((int)buffer.desiredSetPiece) +
                  ": " + int_to_str(intruders) + " opponent(s) to move");
        }
        if (!SetPieceLaws::MayRestart(intruders, match->GetActualTime_ms() - buffer.prepareTime)) {
          buffer.startTime = match->GetActualTime_ms() + 10;
          if (!clearingLogged) {
            clearingLogged = true;
            clearingHeldFrom_ms = match->GetActualTime_ms();
            Log(e_Notice, "Referee", "Process",
                "restart held: " + int_to_str(intruders) + " opponent(s) still in the area");
          }
        }
      }

      // Law 13: and the same wait for a free kick or a corner, measured from the ball.
      if (!buffer.started && buffer.startTime <= match->GetActualTime_ms() &&
          SetPieceLaws::ClearsTheBallRadius(buffer.desiredSetPiece)) {
        std::vector<Player*> squad;
        match->GetTeam(abs(buffer.teamID - 1))->GetActivePlayers(squad);
        const Vector3 ball = match->GetBall()->Predict(0).Get2D();
        int tooClose = 0;
        float nearest = 1e9f;
        for (unsigned int i = 0; i < squad.size(); i++) {
          const Vector3 at = squad.at(i)->GetPosition();
          if (SetPieceLaws::IntrudesOnBallRadius(at.coords[0], at.coords[1], ball.coords[0],
                                                ball.coords[1]))
            tooClose++;
          const float dx = at.coords[0] - ball.coords[0], dy = at.coords[1] - ball.coords[1];
          nearest = std::min(nearest, std::sqrt(dx * dx + dy * dy) - SetPieceLaws::kRetreatRadius);
        }
        if (nearest < 1e8f) {
          clearingNearest_m = nearest;
          clearingRule = "nine metres";
        }
        if (!clearingAnnounced) {
          clearingAnnounced = true;
          Log(e_Notice, "Referee", "Process",
              "standing them off the ball for set piece " +
                  int_to_str((int)buffer.desiredSetPiece) + ": " + int_to_str(tooClose) +
                  " opponent(s) to move");
        }
        if (!SetPieceLaws::MayRestart(tooClose, match->GetActualTime_ms() - buffer.prepareTime)) {
          buffer.startTime = match->GetActualTime_ms() + 10;
          if (!clearingLogged) {
            clearingLogged = true;
            clearingHeldFrom_ms = match->GetActualTime_ms();
            Log(e_Notice, "Referee", "Process",
                "restart held: " + int_to_str(tooClose) + " opponent(s) inside nine metres");
          }
        }
      }

      if (!buffer.started && buffer.startTime <= match->GetActualTime_ms() &&
          // PES's set-piece AI holds a restart for a frame-rate multiple before
          // the kicker takes it (setpiecelaws.hpp): 189 frames for a corner, 162
          // for a free kick, 54 for a penalty, all measured live. `startTime` is
          // when the referee is ready for the kick; this adds PES's deliberate
          // hold on top of it, so the placement and the wait cannot be confused
          // for each other. The kick-off, throw-in and goal kick are released by
          // their own rule and the gate returns true for them immediately.
          SetPieceLaws::MayTakeRestart(
              buffer.desiredSetPiece,
              static_cast<int>((match->GetActualTime_ms() - buffer.startTime) /
                               static_cast<float>(Match::kTick_ms)),
              static_cast<float>(1000 / Match::kTick_ms),
              match->GetPesSetPieceTakerConstants())) {
        buffer.started = true;
        if (clearingLogged) {
          // How long the taker actually waited, and how far the nearest opponent had
          // got by the time he kicked. Both are the point of the law: a restart that
          // is released while a man is still standing on the line has cleared nothing.
          Log(e_Notice, "Referee", "Process",
              "restart released after " +
                  int_to_str((int)(match->GetActualTime_ms() - clearingHeldFrom_ms)) +
                  " ms: nearest opponent " + int_to_str((int)(clearingNearest_m * 100)) +
                  " cm clear of " + clearingRule);
        }
        clearingLogged = false;
        clearingAnnounced = false;
        match->StartPlay();
        match->StartSetPiece();
      }
    }
  }

  if (match->IsInSetPiece()) {
    // check if set piece has been taken
    if (buffer.taker->TouchAnim() && !buffer.taker->TouchPending()) {
      if (buffer.desiredSetPiece == e_SetPiece_Penalty)
        match->SetPendingPenaltyTaken(buffer.teamID, buffer.taker);
      buffer.active = false;
      buffer.chiefSpot.reset();
      match->StopSetPiece();
      match->GetTeam(0)->GetController()->PrepareSetPiece(e_SetPiece_None);
      match->GetTeam(1)->GetController()->PrepareSetPiece(e_SetPiece_None);
      afterSetPieceRelaxTime_ms = 400;
      foul.foulPlayer = 0;
      foul.foulType = 0;

      if (match->GetMatchPhase() == e_MatchPhase_PreMatch) {
        match->SetMatchPhase(e_MatchPhase_1stHalf);
      }
    }
  }

  if (afterSetPieceRelaxTime_ms > 0)
    afterSetPieceRelaxTime_ms -= 10;
}

void Referee::IssueDeferredCards() {
  const std::vector<CardBook::DeferredCard> cards = CardBook::Drain(cardBook);
  bool anyCardShown = false;
  for (const CardBook::DeferredCard& card : cards) {
    if (!card.player)
      continue;
    const std::string playerName =
        card.player->GetPlayerData() ? card.player->GetPlayerData()->GetLastName() : "";
    match->GetMatchData()->AddEvent(
        card.foulType == 3 ? MatchData::Event::RedCard : MatchData::Event::YellowCard,
        card.player->GetTeamID(), match->GetMatchMinute(), playerName);
    // PES's late card (9.5): step 6 starts the card scene on its second frame
    // (no referee-approach gate), the step runs max(2.5 s, the 340-tick scene
    // FOUL_CMN_CARD_Y_B), and the card prop REF_CARD_Y shows at 40 ticks in.
    // This engine holds the ceremony at 3.0 s, which is also the window the
    // card is on screen for, measured from the stop rather than the second.
    if (card.foulType == 2)
      card.player->GiveYellowCard(buffer.stopTime + kPesCardOnScreen_ms);
    else if (card.foulType == 3)
      card.player->GiveRedCard(buffer.stopTime + kPesCardOnScreen_ms);
    if (Verbose())
      printf("referee: deferred card issued (type %i)\n", card.foulType);
    anyCardShown = true;
  }

  if (anyCardShown) {
    // The ceremony the booking scene stands for: PES's step-6 timeout. The
    // restart the stop already armed is pushed out by this much, and PES's own
    // 340-tick scene sits inside it (5.6 s of cut + 0.5 s of fades), so 3.0 s
    // is the card being on screen, not the whole scene.
    buffer.holdTime = buffer.prepareTime;
    buffer.prepareTime += kPesCardOnScreen_ms;
    buffer.startTime = buffer.prepareTime + 2000;
    // Delayed, so both stages come round again - as they did when these were exact
    // time comparisons.
    buffer.prepared = false;
    buffer.started = false;
  }
}

void Referee::PrepareSetPiece(e_SetPiece setPiece) {
  // position players for set piece situation

  match->ResetSituation(buffer.restartPos);
  match->GetMatchData()->RecordPassRestart();

  match->GetTeam(0)->GetController()->PrepareSetPiece(setPiece, buffer.teamID);
  match->GetTeam(1)->GetController()->PrepareSetPiece(setPiece, buffer.teamID);

  buffer.taker = match->GetTeam(buffer.teamID)->GetController()->GetPieceTaker();

  // The chief referee's mark. PES teleports the officials under the restart's
  // wipe (docs/re/pes-re-setpiece-choreography.md 8.2); a throw-in has no wipe,
  // so there he walks. The box is this engine's Law 16 box, 14 cm short of
  // PES's FieldInfo line (docs/re/pes-re-stadium-camera.md 2).
  const pes::FieldMarks field{pitchHalfW + lineHalfW, goalAreaHalfWidth,
                              pitchHalfW - SetPieceLaws::kAreaDepth, SetPieceLaws::kAreaHalfWidth};
  const auto mark = pes::ChiefRefereeSpot(
      setPiece, {buffer.restartPos.coords[0], -buffer.restartPos.coords[1]},
      -match->GetTeam(buffer.teamID)->GetSide(), field, match->GetPesJudgeConstants().refereeSpot);
  buffer.chiefSpot.reset();
  if (mark) {
    buffer.chiefSpot = Vector3(mark->x, -mark->z, 0);
    if (setPiece != e_SetPiece_ThrowIn)
      match->GetOfficials()->GetReferee()->CastHumanoid()->ResetPosition(*buffer.chiefSpot,
                                                                         buffer.restartPos);
  }
}

void Referee::AlterSetPiecePrepareTime(unsigned long newTime_ms) {
  if (buffer.active) {
    // Bounded. This is called once per tick by several things - a goal
    // celebration's beats, a cutscene, and the referee playing his special
    // animation (match.cpp) - each pushing the restart further out so it does
    // not fire underneath them. Nothing stopped any of them going on for ever:
    // the referee's special animation pushed it one second at a time for as
    // long as it lasted, and the restart simply never arrived. Measured: a goal
    // kick armed and prepared, `started 0`, `start-in` pinned at exactly 3.0 s
    // on every sample for 107 s - the longest stall in the set, and the whole
    // match standing still from 26:05 while the clock ran on.
    //
    // So the deferral is capped from the moment play stopped. The cap is well
    // beyond any real celebration or replay, which run for seconds, so this
    // never truncates one; it exists so that whatever is holding the restart
    // stops holding it.
    const unsigned long deferDeadline_ms = buffer.stopTime + kMaxRestartDeferral_ms;
    if (newTime_ms > deferDeadline_ms) {
      if (!deferralRefusedLogged) {
        deferralRefusedLogged = true;
        printf("[dead-ball] restart deferred past %u ms by %u; taking it anyway\n",
               kMaxRestartDeferral_ms, newTime_ms - buffer.stopTime);
      }
      newTime_ms = deferDeadline_ms;
    }
    if (newTime_ms < buffer.stopTime + 500) {
      // And never pulled earlier than the stop itself, which is what a caller
      // asking for "now" means when the whistle has only just gone.
      newTime_ms = buffer.stopTime + 500;
    }
    deferralRefusedLogged = false;
    buffer.prepareTime = newTime_ms;
    buffer.startTime = buffer.prepareTime + 2000;
    buffer.prepared = false;
    buffer.started = false;
  }
}

int Referee::GetOffsideCategory(Player* player) const {
  const auto flagged = offsidePlayers.find(player);
  if (flagged == offsidePlayers.end()) return -1;
  return flagged->second.category;
}

void Referee::BallTouched() {
  // check for offside player receiving the ball

  int lastTouchTeamID = match->GetLastTouchTeamID();
  if (lastTouchTeamID == -1)
    return;  // shouldn't happen really ;)
  if (match->IsInPlay() && !match->IsInSetPiece() && buffer.active == false &&
      match->GetTeam(abs(lastTouchTeamID - 1))->GetActivePlayerCount() >
          1) {  // disable if only 1 player: that's debug mode with only keeper
    std::map<Player*, OffsideFlag>::iterator playerIter = offsidePlayers.begin();
    while (playerIter != offsidePlayers.end()) {
      if (match->GetTeam(lastTouchTeamID)->GetLastTouchPlayer() == playerIter->first) {
        // PES 2021's involvement test (docs/re/pes-re-duels-fouls.md 5.2 step
        // 2): a flagged player who plays the ball is involved at once.
        foul.advantage = false;
        // PES's offside scan only runs while no foul is pending, and it never
        // arms a flag over a foul already whistled.
        if (!CheckFoul() && foul.foulType == 0) {
          // PES 2021's pending offside (5.1): the Judge sets a pending foul
          // here and the whistle waits out the gs/2 hold in Process.
          pendingOffside.player = playerIter->first;
          pendingOffside.spot =
              OffsideRule::RestartPosition(playerIter->second.positionAtTouch,
                                           playerIter->first->GetPosition());
          pendingOffside.category = playerIter->second.category;
          pendingOffside.since_ms = match->GetActualTime_ms();
        }
        break;
      }
      playerIter++;
    }
  }

  // Law 11: a deliberate play by a defender resets the offside phase and makes
  // a previously offside attacker onside; a deflection or a save does not. A
  // touch by the flagged players' own team always starts a fresh phase, judged
  // at this touch.
  Player* toucher = match->GetTeam(lastTouchTeamID)->GetLastTouchPlayer();
  const bool opponentOfFlagged =
      !offsidePlayers.empty() &&
      offsidePlayers.begin()->first->GetTeam()->GetID() != lastTouchTeamID;
  const e_TouchType touchType =
      toucher ? toucher->GetLastTouchType() : e_TouchType_Intentional_Kicked;
  if (!OffsideRule::TouchResetsPhase(opponentOfFlagged, touchType))
    return;  // the flagged attackers stay flagged; no new snapshot is taken

  offsidePlayers.clear();

  // Law 11: a goal kick, throw-in or corner can never put its receivers
  // offside, so their delivery touch must not arm the flag.
  if (OffsideRule::ShouldSnapshot(match->IsInPlay(), buffer.active, buffer.desiredSetPiece)) {
    // PES 2021's detection (docs/re/pes-re-offside.md 4): at this ball event the
    // attacking team is judged against the defending team's line - its
    // second-rearmost defender's rear surface, capped by the ball and by
    // halfway - and the player on the ball is skipped, as PES skips the event's
    // own player.
    Team* team = match->GetTeam(lastTouchTeamID);
    const float offside =
        AI_GetOffsideLine(match, match->GetMentalImage(0), abs(lastTouchTeamID - 1));
    // PES's attacker test runs in the attacking team's own attack frame
    // (report 4: s*P.x + m > s*L + tol, s the team's attack direction).
    // Team::GetSide() points at the team's own goal, so the attack direction
    // is its negation - the same read as elizacontroller.cpp's attackDir and
    // JudgeContact's attackDirSign.
    const int attackingSide = -team->GetSide();
    const pes::JudgeConstants& constants = match->GetPesJudgeConstants();
    std::vector<Player*> players;
    team->GetActivePlayers(players);
    for (unsigned int i = 0; i < players.size(); i++) {
      Player* player = players.at(i);
      // The player on the ball is never judged against his own touch.
      if (player == team->GetLastTouchPlayer()) continue;
      const int category = OffsideRule::OffsideCategory(
          player->GetPosition().coords[0], offside, attackingSide, constants);
      // Only categories 0..2 are punishable (PES scans c < 3); category 3 is
      // onside near level and never flagged.
      if (category >= 0 && category <= 2) {
        offsidePlayers.insert(std::pair<Player*, OffsideFlag>(
            player, OffsideFlag{player->GetPosition(), category}));
      }
    }
  }
}

void Referee::CommitPendingOffside() {
  if (!pendingOffside.player) return;
  // PES's Judge only runs while the ball is in play, so a whistle never lands
  // once play has stopped - a goal, a ball out, a staged restart.
  if (!match->IsInPlay() || match->IsInSetPiece() || buffer.active) return;

  // DelayWhistle (duels-fouls 5.1): a tight call (category 2) waits while the
  // ball player is shooting - PES's SHOOT state - so the shot finishes first.
  if (pendingOffside.category == 2 &&
      pendingOffside.player->GetCurrentFunctionType() == e_FunctionType_Shot)
    return;

  if (match->GetActualTime_ms() - pendingOffside.since_ms < kPesOffsideWhistleHold_ms) return;

  // uooooga uooooga offside!
  match->StopPlay();
  buffer.desiredSetPiece = e_SetPiece_FreeKick;
  buffer.stopTime = match->GetActualTime_ms();
  // The offside is PES's 0xb stop (9.3): a 2.5 s hold, +2.5 s while a player
  // is down, then the wipe, teleport and fade-in. Live: 140 frames without a
  // man down, 275 end to end across a flag.
  ScheduleStop(this, buffer.stopTime, AnyPlayerDown(match), kPesHoldOutOfPlay);
  // Law 11: the free kick is taken where the offence occurred - the spot where
  // the flagged player became involved - not where he stood when the pass was
  // struck (that stored position only judged the offside position itself).
  buffer.restartPos = pendingOffside.spot;
  const int offendingTeamID = pendingOffside.player->GetTeamID();
  buffer.teamID = abs(offendingTeamID - 1);
  match->GetMatchData()->AddOffside(offendingTeamID);
  match->GetMatchData()->AddFreeKick(buffer.teamID);
  buffer.active = true;
  buffer.prepared = false;
  buffer.started = false;
  if (Verbose())
    printf("referee: offside\n");
  pendingOffside.player = nullptr;
}

int Referee::JudgeContact(Player* tripee, Player* tripper, pes::JudgeAction action) {
  // PES 2021's foul decision (docs/re/pes-re-duels-fouls.md 2): points from the
  // approach angle, the closing speed and the challenge, then thresholds. The
  // victim attacks the goal at x = -side * pitchHalfW.
  const pes::JudgeConstants& k = match->GetPesJudgeConstants();
  const Vector3 a = tripee->GetPosition(), b = tripper->GetPosition();
  const float attackDirSign = -tripee->GetTeam()->GetSide();
  constexpr float RAD_TO_DEG = 180.0f / static_cast<float>(M_PI);
  constexpr float HALF_TURN_DEG = 180.0f;
  const auto angleBetween = [](const Vector3& u, const Vector3& v) {
    return std::acos(clamp(u.GetNormalized(0).GetDotProduct(v.GetNormalized(0)), -1.0f, 1.0f)) *
           RAD_TO_DEG;
  };

  pes::JudgeContact c;
  c.action = action;
  // 180 = B came in along A's own facing, i.e. from directly behind.
  c.approachAngle_deg = HALF_TURN_DEG - angleBetween(a - b, tripee->GetDirectionVec());
  c.slideOffLine_deg = angleBetween(tripper->GetDirectionVec(), a - b);
  c.headingsDelta_deg = angleBetween(tripee->GetDirectionVec(), tripper->GetDirectionVec());
  c.relativeSpeed_mps = (tripee->GetMovement() - tripper->GetMovement()).GetLength();
  // This engine's standing tackle (2) and slide (3) put the man down; the light
  // contact (1) does not.
  c.victimFell = action != pes::JudgeAction::Jostle;
  const float boxHalfWidth = pesPenaltyAreaHalfWidth;  // FieldInfo F7
  c.dogso = FoulSeverity::DeniesObviousChance(
      attackDirSign, a.coords[0], a.coords[1],
      AI_GetOffsideLine(match, match->GetMentalImage(0), tripper->GetTeam()->GetID()),
      boxHalfWidth);
  // The spot moves spotBias towards A's own goal and the touchline, so a
  // contact on the box line is judged outside (FUN_1405b6870).
  const float spotX = a.coords[0] - attackDirSign * k.spotBias_m;
  const float spotY = a.coords[1] + (a.coords[1] < 0 ? -k.spotBias_m : k.spotBias_m);
  c.inAttackingBox =
      fabs(spotY) < boxHalfWidth && spotX * attackDirSign > pesPenaltyAreaLineX;

  // PES level 1 foul, 2 foul + warning, 3 yellow, 4 red; this engine's foul
  // types are 1 foul, 2 yellow, 3 red.
  const int level = pes::JudgeLevel(c, k);
  constexpr int FOUL_TYPE_FOR_LEVEL[] = {0, 1, 1, 2, 3};
  if (Verbose() && level > 0)
    printf("referee: judge points %i level %i\n", pes::JudgePoints(c, k), level);
  return FOUL_TYPE_FOR_LEVEL[level];
}

float Referee::NearestToBall_m(Team* team) const {
  const Vector3 ball = match->GetBall()->Predict(0).Get2D();
  float best = std::numeric_limits<float>::max();
  for (const auto& p : team->GetAllPlayers())
    if (p->IsActive()) best = std::min(best, (p->GetPosition() - ball).GetLength());
  return best;
}

void Referee::RecordFoul(Player* tripee, Player* tripper, int foulType) {
  foul.foulType = foulType;
  foul.advantage = false;
  foul.advantageDecided = false;
  foul.advantageRemaining_s = 0.0f;
  foul.advantageLastToucher = nullptr;
  foul.foulPlayer = tripper;
  foul.foulVictim = tripee;
  foul.foulTime = match->GetActualTime_ms();
  foul.foulPosition = tripee->GetPosition();
  foul.hasBeenProcessed = false;
}

void Referee::TripNotice(Player* tripee, Player* tripper, int tackleType) {
  if (buffer.active)
    return;
  if (tripper == foul.foulPlayer && foul.foulType != 0)
    return;

  // The challenge itself, counted whether or not it is a foul: the report's
  // tackles and slides are the reaction table's output, and a clean win of the
  // ball has to show up in them too. The key is the challenging player, so one
  // challenge is counted once no matter how many ticks its contact lasts.
  match->GetMatchData()->AddChallenge(tripper->GetTeamID(), tackleType == 3, tripper->GetID());

  if (tackleType == 1 || tackleType == 2) {  // standing challenges
    if (tripee->GetTeam()->GetFadingTeamPossessionAmount() > 1.1 &&
        (tripper->GetCurrentFunctionType() == e_FunctionType_Interfere ||
         tripper->GetCurrentFunctionType() == e_FunctionType_Sliding) &&
        (tripee->GetPosition() - match->GetBall()->Predict(0).Get2D()).GetLength() < 2.0 &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID()) {
      const int foulType = JudgeContact(
          tripee, tripper, tackleType == 1 ? pes::JudgeAction::Jostle : pes::JudgeAction::Tackle);
      if (foulType > 0) RecordFoul(tripee, tripper, foulType);
    }
  } else if (tackleType == 3) {  // sliding tackle
    if (match->GetActualTime_ms() - tripper->GetLastTouchTime_ms() > 600 &&
        tripper->GetCurrentFunctionType() == e_FunctionType_Sliding &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID() &&
        (match->GetBall()->Predict(0) - tripee->GetPosition()).GetLength() < 8.0) {
      const int foulType = JudgeContact(tripee, tripper, pes::JudgeAction::Slide);
      if (foulType > 0) RecordFoul(tripee, tripper, foulType);
    }
  }
}

void Referee::CancelSetPiece() {
  buffer.chiefSpot.reset();
  buffer.active = false;
  buffer.endPhase = false;
  buffer.desiredSetPiece = e_SetPiece_None;
  buffer.taker = nullptr;
  foul.foulType = 0;
  foul.foulPlayer = nullptr;
  foul.advantage = false;
  foul.hasBeenProcessed = true;
  match->StopSetPiece();
}

bool Referee::CheckFoul(bool heldWhistle) {
  bool penalty = false;
  if (foul.foulType != 0) {
    if (fabs(foul.foulPosition.coords[1]) < 20.15 - lineHalfW &&
        foul.foulPosition.coords[0] * -foul.foulVictim->GetTeam()->GetSide() >
            pitchHalfW - 16.5 + lineHalfW)
      penalty = true;
  }

  if (heldWhistle && foul.foulType != 0 && !foul.hasBeenProcessed) {
    // PES 2021's advantage (docs/re/pes-re-duels-fouls.md 3).
    const pes::JudgeConstants& k = match->GetPesJudgeConstants();
    const float since_s = (match->GetActualTime_ms() - foul.foulTime) / 1000.0f;
    Team* fouled = foul.foulVictim->GetTeam();
    Team* fouling = foul.foulPlayer->GetTeam();
    if (!foul.advantage) {
      if (!foul.advantageDecided && pes::InAdvantageDecisionWindow(since_s, k)) {
        foul.advantageDecided = true;
        constexpr int LEVEL_FOR_FOUL_TYPE[] = {0, 1, 3, 4};
        if (pes::ShouldPlayAdvantage(LEVEL_FOR_FOUL_TYPE[foul.foulType], penalty,
                                     foul.foulPlayer->GetCards() > 0, NearestToBall_m(fouled),
                                     NearestToBall_m(fouling))) {
          foul.advantage = true;
          foul.advantageRemaining_s = k.advantageWindow_s;
          foul.advantageLastToucher = match->GetLastTouchPlayer();
        }
      }
      // The whistle waits out the hold.
      if (!foul.advantage && since_s < k.whistleHold_s) return false;
    } else {
      Player* toucher = match->GetLastTouchPlayer();
      const bool newTouch = toucher && toucher != foul.advantageLastToucher;
      foul.advantageLastToucher = toucher;
      if (newTouch && toucher->GetTeam() == fouling) {
        foul.advantage = false;  // called back: the fouling team won the ball
      } else {
        // ponytail: every new fouled-team toucher counts as a pass or shot;
        // PES counts its ball events of types 1..5.
        foul.advantageRemaining_s = pes::AdvanceAdvantage(
            foul.advantageRemaining_s, Match::kTick_ms / 1000.0f,
            match->GetLastTouchTeam() == fouled, newTouch && toucher->GetTeam() == fouled);
        if (foul.advantageRemaining_s <= 0.0f) {
          // Advantage completed: no free kick, but a card is still owed and
          // shown at the next stoppage.
          CardBook::OnAdvantageExpired(cardBook, foul.foulPlayer, foul.foulType, foul.foulTime);
          foul.foulPlayer = 0;
          foul.foulType = 0;
          foul.advantage = false;
          return false;
        }
        return false;
      }
    }
  }

  if (foul.foulType != 0 && foul.advantage == false && !foul.hasBeenProcessed) {
    match->StopPlay();
    // PES's foul hold (9.2) is 5.0 s with no scene, and a scene never picks a
    // hold below its own; a player down adds PES's +2.5 s (9.3) to either.
    const bool downed =
        foul.foulVictim && foul.foulVictim->GetInjuryLevel() >= kPesDownedInjuryLevel;
    if (!penalty) {
      buffer.desiredSetPiece = e_SetPiece_FreeKick;
      buffer.stopTime = match->GetActualTime_ms();
      ScheduleStop(this, buffer.stopTime, downed, kPesHoldFoul);
      buffer.restartPos = foul.foulPosition;
    } else {
      buffer.desiredSetPiece = e_SetPiece_Penalty;
      buffer.stopTime = match->GetActualTime_ms();
      ScheduleStop(this, buffer.stopTime, downed, kPesHoldFoul);
      buffer.restartPos =
          Vector3((pitchHalfW - 11.0) * foul.foulPlayer->GetTeam()->GetSide(), 0, 0);
    }
    buffer.teamID = foul.foulVictim->GetTeam()->GetID();
    if (buffer.desiredSetPiece == e_SetPiece_FreeKick)
      match->GetMatchData()->AddFreeKick(buffer.teamID);
    buffer.active = true;
        buffer.prepared = false;
        buffer.started = false;
    // PES's late card (9.5): the card prop's own start tick `REF_CARD_Y/R`,
    // 5..411 ticks into the cut at 60 fps - most variants about 30, and the
    // card on screen for the 3.0 s the whole step's timeout is. The old six
    // seconds was GF's own guess ("need to find out proper moment").
    if (foul.foulType == 2)
      foul.foulPlayer->GiveYellowCard(buffer.stopTime + kPesCardOnScreen_ms);
    if (foul.foulType == 3)
      foul.foulPlayer->GiveRedCard(buffer.stopTime + kPesCardOnScreen_ms);

    // The statistic counts whistles, not collisions: it used to be incremented
    // by the collision producer before the referee had decided anything.
    match->GetMatchData()->AddFoul(foul.foulPlayer->GetTeamID());
    match->GetMatchData()->AddCard(foul.foulPlayer->GetTeamID(), foul.foulType);
    {
      PlayerData* foulPlayerData = foul.foulPlayer->GetPlayerData();
      const std::string who = foulPlayerData ? foulPlayerData->GetLastName() : "";
      const MatchData::Event::Kind kind = foul.foulType == 3   ? MatchData::Event::RedCard
                                          : foul.foulType == 2 ? MatchData::Event::YellowCard
                                                               : MatchData::Event::Foul;
      match->GetMatchData()->AddEvent(kind, foul.foulPlayer->GetTeamID(), match->GetMatchMinute(),
                                      who);
    }
    if (Verbose())
      printf("referee: foul (type %i)%s\n", foul.foulType, penalty ? " penalty" : "");

    foul.hasBeenProcessed = true;

    return true;
  }

  return false;
}

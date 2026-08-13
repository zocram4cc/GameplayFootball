#include "penaltyshootoutcontroller.hpp"

#include <vector>

#include "ball.hpp"
#include "base/log.hpp"
#include "base/math/bluntmath.hpp"
#include "match.hpp"
#include "player/player.hpp"
#include "referee.hpp"
#include "team.hpp"

using namespace blunted;

namespace {

// The penalty spot sits 11 metres out from the goal line.
const float penaltySpotDistance = 11.0f;
// Below this speed the ball counts as having come to rest.
const float ballAtRestSpeed = 0.6f;
// The keeper counts as having the ball inside this radius.
const float keeperGrabDistance = 1.2f;

std::string OutcomeText(PenaltyShootout::e_Outcome outcome) {
  switch (outcome) {
    case PenaltyShootout::e_Outcome_Goal:
      return "GOAL!";
    case PenaltyShootout::e_Outcome_Save:
      return "SAVED!";
    case PenaltyShootout::e_Outcome_Miss:
      return "MISSED!";
    default:
      return "";
  }
}

}  // namespace

PenaltyShootoutController::PenaltyShootoutController(Match* match) : match(match) {
  Reset();
}

void PenaltyShootoutController::Reset() {
  state = PenaltyShootout::Create(0);
  started = false;
  nextEventTime_ms = 0;
  kickStartTime_ms = 0;
  aimY = 0.0f;
  aimPower = 0.8f;
  baseScore[0] = 0;
  baseScore[1] = 0;
  takenMask[0] = 0;
  takenMask[1] = 0;
  currentTaker = nullptr;
  keeperTouchedBall = false;
  shootoutEndSide = 1;
  plannedOutcome = PenaltyShootout::e_Outcome_Pending;
}

void PenaltyShootoutController::Start() {
  if (started)
    return;

  // Coin toss for who goes first.
  const int firstTeam = random(0.0f, 1.0f) < 0.5f ? 0 : 1;
  state = PenaltyShootout::Create(firstTeam);
  started = true;
  takenMask[0] = 0;
  takenMask[1] = 0;
  currentTaker = nullptr;

  // One end is drawn for the whole shootout, as in a real one.
  shootoutEndSide = random(0.0f, 1.0f) < 0.5f ? -1 : 1;

  // Shootout goals do not change the match score, so remember what it was.
  baseScore[0] = match->GetScore(0);
  baseScore[1] = match->GetScore(1);

  // Throw away the restart the referee queued when the phase changed, otherwise
  // a kick-off gets staged underneath the shootout.
  match->StopPlay();
  match->StopSetPiece();
  match->GetReferee()->CancelSetPiece();

  nextEventTime_ms =
      match->GetActualTime_ms() + PenaltyShootout::GetPhaseDuration_ms(state.phase);

  match->SpamMessage("PENALTY SHOOTOUT", 4000);
  Log(e_Notice, "PenaltyShootoutController", "Start",
      "penalty shootout at the x=" + real_to_str(GetGoalX()) + " end: " +
          match->GetTeam(firstTeam)->GetTeamData()->GetShortName() + " shoot first");
}

Player* PenaltyShootoutController::SelectTaker(int teamID) {
  Team* team = match->GetTeam(teamID);

  std::vector<Player*> candidates;
  team->GetActivePlayers(candidates);

  // The keeper only takes one when there is nobody else left.
  std::vector<Player*> outfield;
  for (Player* candidate : candidates) {
    if (candidate != team->GetGoalie())
      outfield.push_back(candidate);
  }
  if (outfield.empty())
    outfield = candidates;
  if (outfield.empty())
    return nullptr;

  std::vector<float> ratings;
  ratings.reserve(outfield.size());
  for (Player* candidate : outfield) {
    // Composure counts as much as technique from twelve yards.
    ratings.push_back(candidate->GetStat("technical_shot") * 0.6f +
                      candidate->GetStat("mental_calmness") * 0.4f);
  }

  const int index = PenaltyShootout::SelectTakerIndex(ratings, takenMask[teamID]);
  if (index < 0)
    return nullptr;

  takenMask[teamID] =
      PenaltyShootout::MarkTaken(takenMask[teamID], index, static_cast<int>(outfield.size()));
  return outfield.at(index);
}

float PenaltyShootoutController::GetGoalX() const {
  return pitchHalfW * static_cast<float>(shootoutEndSide);
}

Vector3 PenaltyShootoutController::GetPenaltySpot() const {
  // Eleven metres in front of the shootout goal, towards the centre.
  return Vector3(GetGoalX() - penaltySpotDistance * static_cast<float>(shootoutEndSide), 0.0f,
                 0.0f);
}

Vector3 PenaltyShootoutController::GetGoalCentre() const {
  return Vector3(GetGoalX(), 0.0f, 0.0f);
}

void PenaltyShootoutController::SetUpKick() {
  const Vector3 spot = GetPenaltySpot();
  const int keepingTeamID = state.shootingTeam == 0 ? 1 : 0;

  // Place the ball on the spot properly: SetPosition alone is overwritten by the
  // ball's own prediction buffers, which is how kicks ended up being taken from
  // the halfway line.
  match->ResetSituation(spot);
  match->GetBall()->ResetSituation(spot);
  match->SetBallRetainer(nullptr);
  keeperTouchedBall = false;
  keeperHauledBack = false;

  // Where this taker puts it comes from his stats; the engine's own penalty set
  // piece then plays the kick out with the real shot and dive animations.
  currentTaker = SelectTaker(state.shootingTeam);

  PenaltyShootout::Shooter shooter;
  if (currentTaker) {
    shooter.vision = currentTaker->GetStat("mental_vision");
    shooter.shot = currentTaker->GetStat("technical_shot");
  }
  const int keepingTeam = state.shootingTeam == 0 ? 1 : 0;
  Player* keeperPlayer = match->GetTeam(keepingTeam)->GetGoalie();
  PenaltyShootout::Keeper keeper;
  if (keeperPlayer) {
    keeper.reaction = keeperPlayer->GetStat("physical_reaction");
    keeper.defensivePositioning = keeperPlayer->GetStat("mental_defensivepositioning");
  }

  // The outcome comes from the players' stats, as the roadmap prescribes: the
  // engine's open-play shot physics are far too unreliable from twelve yards to
  // decide a shootout with. The kick is then aimed so that the animation the
  // player sees matches the outcome that was rolled.
  const PenaltyShootout::Aim aim =
      PenaltyShootout::CalculateAim(shooter, random(-1.0f, 1.0f), random(-1.0f, 1.0f));
  if (!PenaltyShootout::IsOnTarget(aim)) {
    plannedOutcome = PenaltyShootout::e_Outcome_Miss;
  } else if (random(0.0f, 1.0f) < PenaltyShootout::GetSaveChance(keeper, aim)) {
    plannedOutcome = PenaltyShootout::e_Outcome_Save;
  } else {
    plannedOutcome = PenaltyShootout::e_Outcome_Goal;
  }

  const float placementSide = aim.x >= 0.0f ? 1.0f : -1.0f;
  switch (plannedOutcome) {
    case PenaltyShootout::e_Outcome_Goal:
      // Placed inside the post, away from the keeper.
      aimY = placementSide * clamp(std::fabs(aim.x), 1.8f, goalHalfWidth - 0.6f);
      aimPower = clamp(0.8f + aim.z * 0.1f, 0.75f, 1.0f);
      break;
    case PenaltyShootout::e_Outcome_Save:
      // Straight at the keeper, who is standing in the middle of his goal.
      aimY = placementSide * 0.6f;
      aimPower = 0.55f;
      break;
    default:
      // Dragged wide of the post.
      aimY = placementSide * (goalHalfWidth + 1.6f);
      aimPower = clamp(0.7f + aim.z * 0.2f, 0.6f, 1.0f);
      break;
  }

  const Vector3 goalCentre = GetGoalCentre();
  const Vector3 towardsGoal = (goalCentre - spot).GetNormalized(Vector3(1, 0, 0));

  // Standing the chosen taker next to the ball makes him the closest player, so
  // the set-piece preparation picks him as the taker of the penalty.
  if (currentTaker)
    currentTaker->ResetPosition(spot - towardsGoal, goalCentre);

  match->GetTeam(state.shootingTeam)
      ->GetController()
      ->PrepareSetPiece(e_SetPiece_Penalty, state.shootingTeam);

  // The set piece lines both teams up around the box, which is not what a
  // shootout looks like: only the taker belongs in the area, only the defending
  // keeper belongs in the goal, and everybody else waits on the halfway line.
  PositionPlayers();
  match->GetBall()->ResetSituation(spot);
}

void PenaltyShootoutController::PositionPlayers() {
  const Vector3 spot = GetPenaltySpot();
  const Vector3 goalCentre = GetGoalCentre();
  const Vector3 towardsGoal = (goalCentre - spot).GetNormalized(Vector3(1, 0, 0));
  const int keepingTeamID = state.shootingTeam == 0 ? 1 : 0;
  Player* defendingKeeper = match->GetTeam(keepingTeamID)->GetGoalie();

  // Waiting players line up along the halfway line, each team on its own side of
  // it, well clear of the penalty area.
  int waiting[2] = {0, 0};
  for (int teamID = 0; teamID < 2; teamID++) {
    std::vector<Player*> players;
    match->GetTeam(teamID)->GetActivePlayers(players);
    for (Player* player : players) {
      if (player == currentTaker || player == defendingKeeper)
        continue;

      // Two metres behind the halfway line, on the side away from the shootout
      // goal, spread out across the width of the pitch.
      const float lineX = -static_cast<float>(shootoutEndSide) * (2.0f + teamID * 3.0f);
      const float lineY = -18.0f + waiting[teamID] * 3.6f;
      player->ResetPosition(Vector3(lineX, lineY, 0.0f), spot);
      waiting[teamID]++;
    }
  }

  if (currentTaker)
    currentTaker->ResetPosition(spot - towardsGoal * 2.5f, goalCentre);
  AnchorKeeper();
}

void PenaltyShootoutController::AnchorKeeper() {
  const int keepingTeamID = state.shootingTeam == 0 ? 1 : 0;
  Player* keeper = match->GetTeam(keepingTeamID)->GetGoalie();
  if (!keeper)
    return;

  // Middle of his goal, just off the line, facing the ball. Absolute rather
  // than relative, so he is properly reset for every kick.
  const Vector3 goalCentre = GetGoalCentre();
  keeper->ResetPosition(
      Vector3(goalCentre.coords[0] - 0.4f * static_cast<float>(shootoutEndSide), 0.0f, 0.0f),
      GetPenaltySpot());
}

void PenaltyShootoutController::BeginKick() {
  // Live play, so the taker shoots and the keeper dives for real.
  match->StartSetPiece();
  match->StartPlay();
  kickStartTime_ms = match->GetActualTime_ms();
}

std::string PenaltyShootoutController::GetScoreLine() const {
  return match->GetTeam(0)->GetTeamData()->GetShortName() + " " + int_to_str(state.score[0]) +
         " - " + int_to_str(state.score[1]) + " " +
         match->GetTeam(1)->GetTeamData()->GetShortName();
}

PenaltyShootout::KickObservation PenaltyShootoutController::ObserveBall() {
  const int keepingTeamID = state.shootingTeam == 0 ? 1 : 0;
  Player* keeper = match->GetTeam(keepingTeamID)->GetGoalie();
  const Vector3 ballPos = match->GetBall()->Predict(0);

  PenaltyShootout::KickObservation observation;
  observation.goalDetected = match->IsGoalScored() || match->IsBallInGoal();

  if (keeper) {
    const float keeperDistance = (keeper->GetPosition() - ballPos.Get2D()).GetLength();
    if (keeperDistance < keeperGrabDistance)
      keeperTouchedBall = true;
    observation.keeperHasBall =
        match->GetBallRetainer() == keeper ||
        (keeperDistance < keeperGrabDistance &&
         match->GetBall()->GetMovement().GetLength() < ballAtRestSpeed);
  }
  observation.keeperTouchedBall = keeperTouchedBall;

  observation.ballLeftField = std::fabs(ballPos.coords[0]) > pitchHalfW + goalDepth ||
                              std::fabs(ballPos.coords[1]) > pitchHalfH;
  // Only counts once the ball has actually been struck.
  observation.ballStopped = match->GetActualTime_ms() > kickStartTime_ms + 1200 &&
                            match->GetBall()->GetMovement().GetLength() < ballAtRestSpeed;

  return observation;
}

void PenaltyShootoutController::FinishKick(PenaltyShootout::e_Outcome outcome) {
  PenaltyShootout::ApplyOutcome(state, outcome);

  // Back to a dead pitch, and the match score is left as it was at full time.
  match->StopPlay();
  match->StopSetPiece();
  match->GetReferee()->CancelSetPiece();
  match->SetGoalScored(false);
  match->SetScore(0, baseScore[0]);
  match->SetScore(1, baseScore[1]);
  match->SetBallRetainer(nullptr);

  if (outcome == PenaltyShootout::e_Outcome_Goal)
    match->AddExcitementBoost(0.9f, 2500);
  else
    match->AddExcitementBoost(0.6f, 2000);

  const std::string takerName =
      currentTaker ? currentTaker->GetPlayerData()->GetLastName()
                   : match->GetTeam(state.shootingTeam)->GetTeamData()->GetShortName();
  match->SpamMessage(takerName + ": " + OutcomeText(outcome) + "   " + GetScoreLine(), 2200);
  Log(e_Notice, "PenaltyShootoutController", "FinishKick",
      "penalty kick at x=" + real_to_str(GetGoalX()) + ": " + takerName + " " +
          OutcomeText(outcome) + " (" + GetScoreLine() + ")");
}

void PenaltyShootoutController::AnnounceOutcome() {
  const int winner = PenaltyShootout::GetWinner(state);
  if (winner == -1)
    return;

  match->SpamMessage(match->GetTeam(winner)->GetTeamData()->GetName() + " win the shootout " +
                         GetScoreLine(),
                     6000);
  match->AddExcitementBoost(1.0f, 6000);
  Log(e_Notice, "PenaltyShootoutController", "AnnounceOutcome",
      "penalty shootout won by " + match->GetTeam(winner)->GetTeamData()->GetShortName() + " (" +
          GetScoreLine() + ")");
}

void PenaltyShootoutController::Process() {
  if (!started) {
    Start();
    return;
  }
  if (IsFinished())
    return;

  // While a kick is live, let it play out on the pitch. The rolled outcome is
  // what gets recorded; watching the ball is only what paces the sequence.
  if (state.phase == PenaltyShootout::e_Phase_Execution) {
    // One of the two keepers is defending the end he did not defend during the
    // match, so his instinct is to walk back to his own goal. He is only hauled
    // back if he abandons this one altogether, and at most once per kick:
    // resetting a humanoid cancels the animation it is playing, so doing it
    // every tick would leave him frozen instead of diving.
    const int keepingTeamID = state.shootingTeam == 0 ? 1 : 0;
    Player* keeper = match->GetTeam(keepingTeamID)->GetGoalie();
    if (keeper && !keeperHauledBack &&
        std::fabs(keeper->GetPosition().coords[0] - GetGoalX()) > 12.0f) {
      AnchorKeeper();
      keeperHauledBack = true;
    }

    const PenaltyShootout::e_Outcome observed =
        PenaltyShootout::ObserveKick(ObserveBall(), match->GetActualTime_ms() - kickStartTime_ms);
    if (observed == PenaltyShootout::e_Outcome_Pending)
      return;

    FinishKick(plannedOutcome);
    nextEventTime_ms =
        match->GetActualTime_ms() + PenaltyShootout::GetPhaseDuration_ms(state.phase);
    return;
  }

  if (match->GetActualTime_ms() < nextEventTime_ms)
    return;

  switch (state.phase) {
    case PenaltyShootout::e_Phase_Positioning:
      SetUpKick();
      PenaltyShootout::BeginKick(state);
      BeginKick();
      break;

    case PenaltyShootout::e_Phase_Resolution:
      PenaltyShootout::NextKick(state);
      if (IsFinished()) {
        AnnounceOutcome();
        match->GameOver();
        return;
      }
      break;

    default:
      return;
  }

  nextEventTime_ms =
      match->GetActualTime_ms() + PenaltyShootout::GetPhaseDuration_ms(state.phase);
}

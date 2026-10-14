// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "playercontroller.hpp"

#include <cmath>

#include "simcontext.hpp"
#include "data/playerskills.hpp"
#include "../../gameplaytuning.hpp"
#include "../../AIsupport/AIfunctions.hpp"
#include "../../AIsupport/mentalimage.hpp"
#include "../../match.hpp"
#include "../../pesballphysics.hpp"
#include "../../team.hpp"
#include "../humanoid/humanoid_utils.hpp"
#include "../player.hpp"
#include "onthepitch/gameplaytuning.hpp"

PlayerController::PlayerController(Match* match) : IController(match) {
  Reset();
}

void PlayerController::Process() {
  int reactionTime_ms = GetReactionTime_ms();
  if (match->GetLastTouchPlayer() == CastPlayer() &&
      CastPlayer()->GetLastTouchType() != e_TouchType_Accidental)
    reactionTime_ms = 0;
  // instant knowledge 'cheat' (aka. teamplay; players know what their teammates will do. maybe make
  // dynamic based on some teamplay stat in the future)
  // if (match->GetLastTouchTeamID() == team->GetID() && team->GetLastTouchType() !=
  // e_TouchType_Accidental) reactionTime_ms *= 0.8f;

  _mentalImage = match->GetMentalImage(reactionTime_ms);
}

void PlayerController::SetPlayer(PlayerBase* player) {
  IController::SetPlayer(player);
  this->team = CastPlayer()->GetTeam();
  this->oppTeam = match->GetTeam(abs(this->team->GetID() - 1));
  assert(this->oppTeam);
}

Player* PlayerController::CastPlayer() {
  return static_cast<Player*>(player);
}

int PlayerController::GetReactionTime_ms() {
  // A keeper's latency is his GK Reflexes; outfielders keep physical_reaction.
  int reactionTime_ms = CastPlayer()->GetFormationEntry().role == e_PlayerRole_GK
                            ? GameplayTuning::GetReactionTime_ms(player->GetStat("gk_reflexes"))
                            : IController::GetReactionTime_ms();
  // GF ran CPU players slower to react at lower difficulty; every player here is CPU.
  reactionTime_ms += (1.0f - GetMatch()->GetMatchDifficulty()) * 100;
  return reactionTime_ms;
}

void PlayerController::AddDefensiveComponent(Vector3& desiredPosition, float bias,
                                             int forcedOppID) {
  int opponentID;
  if (forcedOppID == -1)
    opponentID = CastPlayer()->GetManMarkingID();
  else
    opponentID = forcedOppID;

  if (match->IsInPlay() && !match->IsInSetPiece() &&
      CastPlayer()->GetFormationEntry().role != e_PlayerRole_GK) {
    if (opponentID != -1) {
      Player* opp = match->GetTeam(abs(team->GetID() - 1))->GetPlayer(opponentID);

      // PES 2021's mark spot (docs/re/pes-re-defensive-shape.md 14.3): the
      // goal-side point 6 - 4*ramp(depth) metres from the opponent, 2 m further
      // when our team is pressing. GF's shooting-point threshold and 4 m buffer
      // are replaced by it.
      Vector3 defendPosition = team->GetController()->GetMarkSpot(CastPlayer(), opp);

      desiredPosition = desiredPosition * (1.0f - bias) + defendPosition * bias;
    }
  }
}

Vector3 PlayerController::GetDefendPosition(Player* opp, float distance) {
  PlayerImage oppImage = _mentalImage->GetPlayerImage(opp->GetID());

  // find the position on the opp -> goal line where we want to go to intercept. this point is the
  // same distance away from opp as it is from us. to find this point: 1) create a line AB between
  // us and op 2) create another line CD perpendicular to AB, that intersects the middle point of AB
  // 3) find the intersection point of CD with the original opp to goal line. if this intersection
  // point is between opp and goal, it's our target. (if not, we're too late, just run to goal :p)

  Vector3 goalPos = Vector3(pitchHalfW * team->GetSide(), 0, 0);
  Vector3 oppPosition = oppImage.position;  // lol @ varname
  Vector3 oppToGoal = goalPos - oppPosition;

  Line oppToGoalLine;
  oppToGoalLine.SetVertex(0, oppPosition);
  oppToGoalLine.SetVertex(1, goalPos);
  // 1
  Line meToOppLine;
  meToOppLine.SetVertex(0, player->GetPosition());
  meToOppLine.SetVertex(1, oppPosition);
  Vector3 midPoint = meToOppLine.GetVertex(0) * 0.5f + meToOppLine.GetVertex(1) * 0.5f;
  // 2
  Line midToOppPerpendicularLine;
  midToOppPerpendicularLine.SetVertex(0, midPoint);
  midToOppPerpendicularLine.SetVertex(
      1, midPoint + (meToOppLine.GetVertex(0) - midPoint).GetRotated2D(0.5 * pi));
  // 3
  float u;
  Vector3 intersect = oppToGoalLine.GetIntersectionPoint(midToOppPerpendicularLine, u);

  u = clamp(u, 0.0f, 1.0f);
  Vector3 target =
      oppToGoalLine.GetVertex(0) + (oppToGoalLine.GetVertex(1) - oppToGoalLine.GetVertex(0)) * u;

  /*
    if (player->GetDebug()) {
      SetRedDebugPilon(intersect + Vector3(0, 0, 0.1f));
      SetBlueDebugPilon(target);
    }
  */

  target += (oppToGoal.GetNormalized(0) * sprintVelocity * 0.1f) + (oppImage.movement * 0.14f) +
            (oppToGoal.GetNormalized(0) * distance);

  return target;
}

void PlayerController::Reset() {
  _mentalImage = 0;
  inputDirection = Vector3(0, -1, 0);
  inputVelocityFloat = 0;

  hasPossession = false;
  hasUniquePossession = false;
  teamHasPossession = false;
  teamHasUniquePossession = false;
  oppTeamHasPossession = false;
  oppTeamHasUniquePossession = false;
  hasBestChanceOfPossession = false;
  teamHasBestPossession = false;
  possessionAmount = 0.9f;
  teamPossessionAmount = 1.0f;
  fadingTeamPossessionAmount = 1.0f;
  timeNeededToGetToBall = 100;
  oppTimeNeededToGetToBall = 100;
  hasBestPossession = false;
}

float PlayerController::OppBetweenBallAndMeDot() {
  Player* opp = match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer();
  Vector3 MeToOpp = (opp->GetPosition() + opp->GetMovement() * 0.1f) -
                    (CastPlayer()->GetPosition() + CastPlayer()->GetMovement() * 0.1f);
  Vector3 oppToBall = _mentalImage->GetBallPrediction(100).Get2D() -
                      (opp->GetPosition() + opp->GetMovement() * 0.1f);
  float dot = MeToOpp.GetNormalized(0).GetDotProduct(oppToBall.GetNormalized(0));

  // if dot nears 1, it means opp is somewhat between ball and me
  return dot;
}

float PlayerController::CouldWinABallDuelLikeliness() {
  // uses the OppBetweenBallAndMeDot to check for correct angle, but also checks ball distance from
  // opp
  float dot = OppBetweenBallAndMeDot() * 0.5f + 0.5f;

  /* taking into account the relative distance to ball doesn't really seem to work all too well
  Vector3 meToBall = _mentalImage->GetBallPrediction(0).Get2D() - player->GetPosition();

  Player *opp = match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer();
  Vector3 oppToBall = _mentalImage->GetBallPrediction(0).Get2D() - opp->GetPosition();

  float meBallDistFactor = NormalizedClamp(meToBall.GetLength(), 0.0f, 2.0f);
  float oppBallDistFactor = NormalizedClamp(oppToBall.GetLength(), 0.0f, 2.0f);

  return (1.0f - dot) * NormalizedClamp(oppBallDistFactor - meBallDistFactor, -1.0f, 0.0f); // 'same
  distance' is considered best now
  */

  // An interceptor backs himself to get there first more often.
  return PlayerSkills::GetBallDuelLikeliness(CastPlayer()->GetPlayerData()->GetSkills(),
                                             1.0f - dot);
}

void PlayerController::_Preprocess() {
  _oppPlayer =
      match->GetPlayer(match->GetTeam(abs(team->GetID() - 1))->GetBestPossessionPlayerID());
  _timeNeeded_ms =
      CastPlayer()->GetTimeNeededToGetToBall_ms();  // needed for synced version - if we would ask
                                                    // on the spot and compare to opp, opp may
                                                    // already have calculated a new one while ours
                                                    // has not been processed yet
}

void PlayerController::_KeeperDeflectCommand(PlayerCommandQueue& commandQueue,
                                             bool onlyPickupAnims) {
  if (CastPlayer()->GetFormationEntry().role != e_PlayerRole_GK)
    return;
  if (match->GetBall()->Predict(400).GetDistance(player->GetPosition()) >
      ballDistanceOptimizeThreshold + 10.0f)
    return;

  // can't use hands if teammate intentionally kicked ball to us
  if (match->GetLastTouchTeamID() == team->GetID() && match->GetLastTouchPlayer() != CastPlayer() &&
      match->GetLastTouchTeamID(e_TouchType_Intentional_Kicked) == team->GetID())
    return;

  if (match->GetBallRetainer() != 0)
    return;

  // can't use hands outside of keeper's 16 yard box (todo: make precise, probably in humanoid.cpp's
  // getbestcheatableanim)
  if (fabs(match->GetBall()->Predict(160).coords[1]) > 20.05f)
    return;
  if (match->GetBall()->Predict(160).coords[0] * -team->GetSide() > -pitchHalfW + 16.4)
    return;

  // Whether he gets across at all is the geometry of the save
  // (Player::KeeperAttemptsSave), consulted by the body-collision pass too - so
  // a beaten keeper is beaten everywhere. The stock engine always played the
  // save, which is why almost nothing went in.
  if (!CastPlayer()->KeeperAttemptsSave())
    return;

  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Deflect;
  command.useDesiredMovement = false;
  command.useDesiredLookAt = false;
  if (onlyPickupAnims)
    command.onlyDeflectAnimsThatPickupBall = true;
  commandQueue.push_back(command);
}

void PlayerController::_SetPieceCommand(PlayerCommandQueue& commandQueue) {
  // do not allow running away :p
  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Movement;
  command.useDesiredMovement = true;
  command.useDesiredLookAt = true;
  command.desiredDirection = CastPlayer()->GetDirectionVec();
  command.desiredVelocityFloat = idleVelocity;
  if (match->GetBallRetainer() == player) {
    command.desiredLookAt = player->GetPosition() + inputDirection * 10.0f;
  } else {
    command.desiredLookAt = match->GetBall()->Predict(0).Get2D();
  }
  commandQueue.push_back(command);
}

void PlayerController::_BallControlCommand(PlayerCommandQueue& commandQueue,
                                           bool idleTurnToOpponentGoal, bool knockOn,
                                           bool stickyRunDirection, bool keepCurrentBodyDirection) {
  if (match->GetBall()->Predict(200).GetDistance(player->GetPosition()) >
      ballDistanceOptimizeThreshold)
    return;
  if (match->GetBallRetainer() != 0)
    return;

  if (!CastPlayer()->HasPossession() && !CastPlayer()->AllowLastDitch()) {
    float strictnessInv = 3.0f;
    if (fabs(match->GetBall()->GetMovement().coords[2]) > 5.0f * strictnessInv)
      return;
    if ((match->GetBall()->GetMovement().Get2D() - CastPlayer()->GetMovement()).GetLength() >
        5.0f * strictnessInv)
      return;
  }

  if (match->GetDesignatedPossessionPlayer() == player ||
      (team->GetDesignatedTeamPossessionPlayer() == player &&
       CouldWinABallDuelLikeliness() >= 0.25f)) {
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_BallControl;
    command.useDesiredMovement = true;
    command.desiredDirection = inputDirection;
    if (quantizeDirection)
      QuantizeDirection(command.desiredDirection, GetQuantizedDirectionBias());
    command.desiredVelocityFloat = inputVelocityFloat;

    if (FloatToEnumVelocity(command.desiredVelocityFloat) == e_Velocity_Idle &&
        idleTurnToOpponentGoal) {
      command.desiredDirection = Vector3(-team->GetSide(), 0, 0);
    }

    if (knockOn)
      command.modifier |= e_PlayerCommandModifier_KnockOn;

    if (hasPossession && hasBestPossession && command.desiredVelocityFloat > walkSprintSwitch &&
        CastPlayer()->GetFloatVelocity() > dribbleWalkSwitch && stickyRunDirection) {
      radian angle = command.desiredDirection.GetAngle2D(CastPlayer()->GetDirectionVec());
      if (fabs(angle) > 0.125f * pi && fabs(angle) < 0.7f * pi) {
        if (angle > 0)
          command.desiredDirection = CastPlayer()->GetDirectionVec().GetRotated2D(0.125f * pi);
        if (angle < 0)
          command.desiredDirection = CastPlayer()->GetDirectionVec().GetRotated2D(0.125f * -pi);
      }
    }

    command.useDesiredLookAt = true;
    if (keepCurrentBodyDirection &&
        (CastPlayer()->GetEnumVelocity() == e_Velocity_Walk ||
         CastPlayer()->GetEnumVelocity() == e_Velocity_Dribble) &&
        FloatToEnumVelocity(command.desiredVelocityFloat) == e_Velocity_Dribble) {
      // keep facing where we are facing and shuffle sideways at walking pace
      command.desiredVelocityFloat = walkVelocity;
      command.desiredLookAt = CastPlayer()->GetPosition() + CastPlayer()->GetDirectionVec() * 10.0f;
    } else {
      command.desiredLookAt = CastPlayer()->GetPosition() + CastPlayer()->GetMovement() * 0.2f +
                              command.desiredDirection * 10.0f;
    }

    // Trick moves: with a man in front of him, a player who carries the skill
    // (or the COM Trickster card) tries the imported feint clips first. The
    // trick command carries the family as specialVar1, so the selector sees
    // only that move's clips; when none fits the geometry it falls through to
    // the plain ball control queued behind it.
    if (hasPossession) {
      Player* opp = AI_GetClosestPlayer(match->GetTeam(abs(team->GetID() - 1)),
                                        CastPlayer()->GetPosition(), 0);
      PlayerSkills::FeintSituation situation;
      if (opp) {
        const Vector3 toOpp = opp->GetPosition() - CastPlayer()->GetPosition();
        situation.opponentDistance = toOpp.GetLength();
        situation.opponentAngle = fabs(toOpp.GetAngle2D(CastPlayer()->GetDirectionVec()));
      }
      situation.turnAngle =
          fabs(command.desiredDirection.GetAngle2D(CastPlayer()->GetDirectionVec()));
      const PlayerSkills::Feint feint = PlayerSkills::PickFeint(
          CastPlayer()->GetPlayerData()->GetSkills(), CastPlayer()->GetPlayerData()->GetComStyles(),
          situation, random(0.0f, 1.0f), random(0.0f, 1.0f));
      if (feint != PlayerSkills::Feint::None) {
        PlayerCommand trick = command;
        trick.useSpecialVar1 = true;
        trick.specialVar1 = static_cast<int>(feint);
        // A poor dribbler's trick can go wrong; the touch decides at contact.
        if (PlayerSkills::FeintFumbled(CastPlayer()->GetStat("technical_tightpossession"),
                                       CastPlayer()->GetStat("technical_dribble"),
                                       random(0.0f, 1.0f)))
          trick.modifier |= e_PlayerCommandModifier_Fumble;
        if (Verbose())
          printf("feint: %s queues %s (opp %.1fm at %.2f rad, turn %.2f rad)\n",
                 CastPlayer()->GetPlayerData()->GetLastName().c_str(),
                 PlayerSkills::GetFeintName(feint), situation.opponentDistance,
                 situation.opponentAngle, situation.turnAngle);
        commandQueue.push_back(trick);
      }
    }

    commandQueue.push_back(command);
  }
}

namespace {
// PES's logic frame rate (FUN_141e532d0, live 54).
constexpr float kPesFps = 1.0f / pesball::kPesTick_s;

float Heading(const Vector3& v) { return pes::HeadingDeg(v.coords[0], v.coords[1]); }
}  // namespace

// The reaction table's receive entry (pes-re-receiving.md §1, §3): a predicate
// so the walk can stop at the first true one.
bool PlayerController::_ReceiveCandidate() {
  // FUN_1409fa400: his entry in the ball-distance table (squared metres).
  const pes::ReceiveConstants& k = match->GetPesDefenderDecisionConstants().receive;
  const float ballDistance =
      (match->GetBall()->Predict(0).Get2D() - player->GetPosition().Get2D()).GetLength();
  if (!pes::WithinBallDistanceTable(k, ballDistance * ballDistance)) return false;
  if (match->GetBallRetainer() != 0) return false;
  if (hasPossession) return false;
  // Who goes: the PASS_GET receiver / the team's designated man
  // (Team::PassGetReceiver feeds the designation).
  if (match->GetDesignatedPossessionPlayer() != player &&
      team->GetDesignatedTeamPossessionPlayer() != player)
    return false;

  // FUN_1409fa720: the re-touch lockout.
  const Match::BallEvent& ev = match->GetBallEvent(0);
  const Match::BallEvent& prev = match->GetBallEvent(1);
  const unsigned long now = match->GetActualTime_ms();
  const Vector3 ballPos = match->GetBall()->Predict(0);
  const Vector3 ballMove = match->GetBall()->GetMovement();
  pes::LockoutInput in;
  in.ballOwned = ev.player && ev.player->HasPossession();
  in.ownerIsMe = ev.player == player;
  in.eventKind = ev.kind;
  in.eventByMe = ev.player == player;
  in.eventByMyTeam = ev.teamID == team->GetID();
  in.previousEventByMe = prev.player == player;
  in.previousEventAge_s = (now - prev.time_ms) * 0.001f;
  // GF's stat is PES's 40..99 rating scaled to 0..1 (team.cpp's stamina map).
  in.ballStat = k.statFloor + CastPlayer()->GetStat("technical_ballcontrol") * k.statSpan;
  in.ballSpeed_mps = ballMove.Get2D().GetLength();
  in.ballHeight_m = ballPos.coords[2];
  if (!pes::RetouchAllowed(k, in, (now - ev.time_ms) * 0.001f, kPesFps)) return false;

  // The shot-through rule: my team's shot on target runs on to goal.
  pes::ShotThroughInput shot;
  shot.ownTeamShot = ev.kind == pes::BallEventKind::kShot && ev.teamID == team->GetID();
  shot.ballX = ballPos.coords[0];
  shot.ballY = ballPos.coords[1];
  shot.ballVelX = ballMove.coords[0];
  shot.ballVelY = ballMove.coords[1];
  shot.goalX = -team->GetSide() * pitchHalfW;
  shot.goalHalfWidth = goalHalfWidth;
  return !pes::ShotRunsThrough(k, shot);
}

// FUN_1409f8a50: the first touch. The planned direction is the carrier brain's
// (GetOnTheBallCommands runs for the designated man before the ball arrives
// and leaves its direction in inputDirection); a one-touch kick is that brain's
// queued pass, so kind 3 needs nothing here.
void PlayerController::_TrapCommand(PlayerCommandQueue& commandQueue) {
  const pes::ReceiveConstants& k = match->GetPesDefenderDecisionConstants().receive;
  const Match::BallEvent& ev = match->GetBallEvent(0);
  const Vector3 ballMove = match->GetBall()->GetMovement().Get2D();
  Vector3 planned = inputDirection;
  if (quantizeDirection) QuantizeDirection(planned, GetQuantizedDirectionBias());

  pes::FirstTouchInput in;
  in.hasPlannedDirection =
      match->GetDesignatedPossessionPlayer() == player && planned.GetLength() > 0.0f;
  in.plannedHeading_deg = Heading(planned);
  in.bodyHeading_deg = Heading(CastPlayer()->GetDirectionVec());
  in.moveHeading_deg = Heading(CastPlayer()->GetMovement());
  in.moveSpeed_mps = CastPlayer()->GetMovement().Get2D().GetLength();
  in.eventByMyTeam = ev.teamID == team->GetID();
  in.eventIsKick = ev.kind == pes::BallEventKind::kPass || ev.kind == pes::BallEventKind::kHighPass;
  in.eventByMe = ev.player == player;
  in.eventAge_s = (match->GetActualTime_ms() - ev.time_ms) * 0.001f;
  in.ballSpeedSq_m2s2 = ballMove.GetDotProduct(ballMove);
  in.ballHeading_deg = Heading(ballMove);
  in.posX = player->GetPosition().coords[0];
  in.posY = player->GetPosition().coords[1];
  in.halfLength = pitchHalfW;
  in.halfWidth = pitchHalfH;
  // GF has no DELAY / PRESS movement state; the team's pressure man is its
  // presser (the same stand-in the defender port uses).
  in.pressOrDelay = team->GetController()->GetTeamPressurePlayer() == player;
  const pes::FirstTouch touch = pes::ChooseFirstTouch(k, in, kPesFps);

  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Trap;
  command.useDesiredMovement = true;
  if (touch.kind == pes::TouchKind::kDirectional) {
    command.desiredDirection = planned;
    command.desiredVelocityFloat = inputVelocityFloat;
  } else {
    // Kind 0: control along the body, at his own pace.
    command.desiredDirection = CastPlayer()->GetDirectionVec();
    command.desiredVelocityFloat = CastPlayer()->GetFloatVelocity();
  }
  if (CastPlayer()->GetFormationEntry().role == e_PlayerRole_GK) {
    command.desiredDirection = Vector3(-team->GetSide(), 0, 0);
    command.desiredVelocityFloat = idleVelocity;
  }
  command.useDesiredLookAt = true;
  command.desiredLookAt = CastPlayer()->GetPosition() + CastPlayer()->GetMovement() * 0.3f +
                          command.desiredDirection * 10.0f;
  commandQueue.push_back(command);
}

void PlayerController::_MovementCommand(PlayerCommandQueue& commandQueue, bool forceMagnet,
                                        bool extraHaste) {
  int defaultLookAtTime_ms = 40;

  Vector3 quantizedInputDirection = inputDirection;
  if (quantizeDirection)
    QuantizeDirection(quantizedInputDirection, GetQuantizedDirectionBias());

  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Movement;
  command.useDesiredMovement = true;
  command.useDesiredLookAt = true;

  // defaults
  Vector3 manualDirection = quantizedInputDirection;
  float manualVelocityFloat = inputVelocityFloat;

  Vector3 defaultLookDirection = manualDirection;
  float desiredVeloFactor =
      1.0f - std::pow(NormalizedClamp(std::min(inputVelocityFloat, player->GetFloatVelocity()),
                                      idleDribbleSwitch, sprintVelocity),
                      0.5f) *
                 0.3f;
  Vector3 focusPos = _mentalImage->GetBallPrediction(defaultLookAtTime_ms).Get2D();
  focusPos += player->GetDirectionVec() * 0.5f;  // to keep looking forward if ball is very close
  radian toFocusAngle =
      (focusPos - player->GetPosition()).GetNormalized(manualDirection).GetAngle2D(manualDirection);
  defaultLookDirection =
      manualDirection.GetRotated2D(toFocusAngle * std::pow(desiredVeloFactor, 0.7f));

  command.desiredDirection = manualDirection;
  command.desiredVelocityFloat = manualVelocityFloat;

  Vector3 autoDirection = command.desiredDirection;
  float autoVelocityFloat = command.desiredVelocityFloat;
  Vector3 autoLookDirection = defaultLookDirection;

  float adaptedPossessionAmount = possessionAmount;

  CastPlayer()->SetDesiredTimeToBall_ms(0);

  float autoBias = 0.0;

  // decide what type of magnet is to be used

  float inputDirIsOwnHalfFactor =
      NormalizedClamp(inputDirection.GetDotProduct(Vector3(team->GetSide(), 0, 0)), -1.0f, 1.0f);

  if (match->GetBallRetainer() == player) {
    autoBias = 0.0f;

  } else if (forceMagnet || match->GetDesignatedPossessionPlayer() == player ||
             ((CastPlayer()->GetLastTouchBias(2000) > 0.01f && possessionAmount > 0.5f) &&
              team->GetDesignatedTeamPossessionPlayer() == player) ||
             ((!oppTeamHasPossession && possessionAmount > 0.5f) &&
              team->GetDesignatedTeamPossessionPlayer() == player) ||
             (possessionAmount > 0.99f &&
              team->GetDesignatedTeamPossessionPlayer() ==
                  player) ||  // for air balls and such, where multiple players are as likely to get
                              // to the ball first
             (hasBestPossession && team->GetDesignatedTeamPossessionPlayer() ==
                                       player)) {  // this is constructed inefficiently (double
                                                   // teamplayer thing) on purpose, for clarity.

    // WE ARE THE MAN OF THE MOMENT! WOOHOO

    if (hasBestPossession) {
      Vector3 autoLookAt;  // dud
      CastPlayer()->SetDesiredTimeToBall_ms(AI_GetBallControlMovement(
          _mentalImage, CastPlayer(), quantizedInputDirection, inputVelocityFloat, autoDirection,
          autoVelocityFloat, autoLookAt));
      autoLookDirection = (autoLookAt - player->GetPosition()).GetNormalized(0);
      autoBias = 1.0f;

    } else {  // !hasPossession

      float haste = 0.0f;
      if (extraHaste || forceMagnet) {
        haste = 1.0f;
      } else {
        float thresholdPossessionAmountForHaste =
            1.1f;  // 2.0f - AI_GetMindSet(CastPlayer()->GetFormationEntry().role) * 2.0f; //
                   // attacking players may want to gamble on the defenders missing the ball
        if (adaptedPossessionAmount < thresholdPossessionAmountForHaste)
          haste = 1.0f;
      }

      Vector3 autoLookAt;  // dud
      CastPlayer()->SetDesiredTimeToBall_ms(AI_GetToBallMovement(
          match, _mentalImage, CastPlayer(), quantizedInputDirection, inputVelocityFloat,
          autoDirection, autoVelocityFloat, autoLookAt, haste));
      // todo: leave to auto dir? (for now, yes)
      autoLookDirection = (autoLookAt - player->GetPosition()).GetNormalized(0);
      autoBias = 1.0f;

      if (match->GetDesignatedPossessionPlayer() != player) {
        float sameDirFactor =
            1.0f - clamp(fabs(autoDirection.GetAngle2D(manualDirection)) / pi, 0.0f, 1.0f);
        sameDirFactor = sameDirFactor * 0.5f +
                        0.5f;  // todo: can't fully trust this; it just isn't a 100% indication of
                               // the player's intentions. just use this hax for now
        autoBias = 0.0f;
        if (CastPlayer()->GetLastTouchBias(2000) > 0.01f) {
          if (CastPlayer()->GetTimeNeededToGetToBall_ms() < 1700)
            autoBias = std::pow(CastPlayer()->GetLastTouchBias(2000), 0.4f) *
                       std::pow(sameDirFactor, 0.5f);
        }
        if (forceMagnet) {
          autoBias = 1.0f;
        }
      }
    }

  } else if (match->GetDesignatedPossessionPlayer()->GetTeamID() != team->GetID()) {
    // OTHER TEAM IS IN BALL CONTROL, DEM BASTERDS

    if (team->GetDesignatedTeamPossessionPlayer() == player) {
      // WE ARE THE BEST OUR TEAM HAS GOT, CHOOSE OUR ACTIONS WISELY

      // if we aren't the designated possession player, we don't ever want to just run straight to
      // the ball like that. we want some combination of manual movement, defensive movement, and
      // to-ball movement.

      // virtual action area in front of opponent
      PlayerImage oppImage = _mentalImage->GetPlayerImage(_oppPlayer->GetID());
      Vector3 oppPos = oppImage.position + oppImage.movement * 0.14f + oppImage.directionVec * 0.6f;
      Vector3 oppToGoalDirection =
          (Vector3(pitchHalfW * team->GetSide(), 0, 0) - oppPos).GetNormalized(0);
      float actionRadius = 5.0f;
      Vector3 focusPosition = oppPos + (oppToGoalDirection * actionRadius * 0.7f);
      float actionBias =
          1.0f - curve(NormalizedClamp((player->GetPosition() - focusPosition).GetLength(), 0.0f,
                                       actionRadius),
                       0.7f);

      // if we're close to the focus position, go do more defending or ballhuntin' (as opposed to
      // manual movement)
      autoBias = actionBias * 0.0f;  // not sure what would be a proper value here. needs more testing

      autoBias *= 1.0f - inputDirIsOwnHalfFactor;  // if we want to run back to more defensive
                                                   // positions, don't autobias

      // now decide on what auto direction to use, if applicable
      // when farther away from ball/opp, get towards ball/opp, when already close, mimic opponent's
      // movement (sort of an auto 'man marking' thing)
      if (autoBias > 0.0f) {
        float playerOppDistance = (oppPos - player->GetPosition()).GetLength();
        float manMarkingBias =
            std::pow(1.0f - curve(NormalizedClamp(playerOppDistance, 0.0f, 7.0f), 0.7f), 1.2f);

        Vector3 autoDirection_manMarking = oppImage.movement.GetNormalized(inputDirection);
        float autoVelocityFloat_manMarking = oppImage.velocity;
        Vector3 autoLookDirection_manMarking =
            (_mentalImage->GetBallPrediction(defaultLookAtTime_ms).Get2D() - player->GetPosition())
                .GetNormalized(0);

        Vector3 huntTarget = oppPos + oppToGoalDirection * playerOppDistance * 0.3f;
        Vector3 huntMovement = huntTarget - player->GetPosition();
        Vector3 autoDirection_hunt = huntMovement.GetNormalized(inputDirection);
        float autoVelocityFloat_hunt = clamp(
            huntMovement.GetLength() * distanceToVelocityMultiplier, idleVelocity, sprintVelocity);
        Vector3 autoLookDirection_hunt =
            (_mentalImage->GetBallPrediction(defaultLookAtTime_ms).Get2D() - player->GetPosition())
                .GetNormalized(0);

        autoDirection = (autoDirection_manMarking * manMarkingBias +
                         autoDirection_hunt * (1.0f - manMarkingBias))
                            .GetNormalized(inputDirection);
        autoVelocityFloat = clamp(autoVelocityFloat_manMarking * manMarkingBias +
                                      autoVelocityFloat_hunt * (1.0f - manMarkingBias),
                                  idleVelocity, sprintVelocity);
        autoLookDirection = ((autoLookDirection_manMarking * manMarkingBias +
                              autoLookDirection_hunt * (1.0f - manMarkingBias)))
                                .GetNormalized(0);
      }
    }
  }

  {
    Vector3 autoMovement = autoDirection * autoVelocityFloat;
    Vector3 manualMovement = manualDirection * manualVelocityFloat;
    Vector3 resultingMovement = manualMovement * (1.0 - autoBias) + autoMovement * autoBias;
    command.desiredDirection = resultingMovement.GetNormalized(quantizedInputDirection);
    const float maximumInputVelocity = sprintVelocity;
    command.desiredVelocityFloat =
        clamp(resultingMovement.GetLength(), idleVelocity, maximumInputVelocity);

    if (command.desiredVelocityFloat < idleDribbleSwitch)
      command.desiredDirection = autoLookDirection;

    Vector3 resultLookDirection = autoLookDirection;
    command.desiredLookAt = player->GetPosition() + resultLookDirection * 10.0f;
  }

  commandQueue.push_back(command);
}

void PlayerController::_CalculateSituation() {
  hasPossession = CastPlayer()->HasPossession();
  hasUniquePossession = CastPlayer()->HasUniquePossession();
  teamHasPossession = team->HasPossession();
  teamHasUniquePossession = team->HasUniquePossession();
  oppTeamHasPossession = match->GetTeam(abs(team->GetID() - 1))->HasPossession();
  oppTeamHasUniquePossession = match->GetTeam(abs(team->GetID() - 1))->HasUniquePossession();
  hasBestChanceOfPossession = team->GetDesignatedTeamPossessionPlayer() == player;
  teamHasBestPossession = match->GetBestPossessionTeamID() == team->GetID();
  possessionAmount =
      (float)(match->GetTeam(abs(team->GetID() - 1))->GetTimeNeededToGetToBall_ms() + 200) /
      (float)(_timeNeeded_ms + 200);
  teamPossessionAmount = team->GetTeamPossessionAmount();
  fadingTeamPossessionAmount = team->GetFadingTeamPossessionAmount();

  // when ball is close, don't fade possession bias (so we can take immediate action)
  if (hasBestChanceOfPossession) {
    float distanceBias =
        std::pow(NormalizedClamp(
                     (match->GetBall()->Predict(300).Get2D() - player->GetPosition()).GetLength(),
                     2.0f, 14.0f),
                 2.0f);
    fadingTeamPossessionAmount =
        fadingTeamPossessionAmount * distanceBias + teamPossessionAmount * (1.0f - distanceBias);
  }

  timeNeededToGetToBall = _timeNeeded_ms;
  oppTimeNeededToGetToBall = match->GetTeam(abs(team->GetID() - 1))->GetTimeNeededToGetToBall_ms();
  hasBestPossession = hasPossession && possessionAmount >= 1.0f;
}

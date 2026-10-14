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

#include "elizacontroller.hpp"

#include <cmath>

#include "simcontext.hpp"
#include "../../AIsupport/AIfunctions.hpp"
#include "../../AIsupport/mentalimage.hpp"
#include "../../aitactics.hpp"
#include "../humanoid/humanoid_utils.hpp"
#include "../playerofficial.hpp"
#include "data/playerskills.hpp"
#include "data/playingstyles.hpp"
#include "onthepitch/gameplaytuning.hpp"
#include "onthepitch/matchpressure.hpp"
#include "onthepitch/peskeeper.hpp"
#include "onthepitch/setpiecelaws.hpp"
#include "onthepitch/penaltyshootoutcontroller.hpp"
#include "onthepitch/teamphilosophy.hpp"
#include "strategies/offtheball/default_def.hpp"
#include "strategies/offtheball/default_mid.hpp"
#include "strategies/offtheball/default_off.hpp"
#include "strategies/offtheball/goalie_default.hpp"
#include "strategies/strategy.hpp"
#include "onthepitch/goalcelebration.hpp"
#include "onthepitch/goalsequence.hpp"
#include "onthepitch/pescarrier.hpp"

namespace {

// PES's angle fold (0x140481b00, report 4): wrap at 360, fold at 180.
inline float WrapDeg(float a) {
  while (a > 180.0f) a -= 360.0f;
  while (a < -180.0f) a += 360.0f;
  return a;
}

}  // namespace

ElizaController::ElizaController(Match* match) : PlayerController(match) {
  lastDesiredDirection = Vector3(0);
  lastDesiredVelocity = 0;
}

ElizaController::~ElizaController() {
  if (Verbose())
    printf("exiting elizacontroller.. ");
  if (defenseStrategy)
    delete defenseStrategy;
  if (midfieldStrategy)
    delete midfieldStrategy;
  if (offenseStrategy)
    delete offenseStrategy;
  if (goalieStrategy)
    delete goalieStrategy;
  if (Verbose())
    printf("done\n");
}

void ElizaController::RequestCommand(PlayerCommandQueue& commandQueue) {
  CastPlayer()->SetDesiredTimeToBall_ms(0);

  _CalculateSituation();

  _Preprocess();  // calculate some variables

  FormationEntry entry = CastPlayer()->GetDynamicFormationEntry();
  float mindSet = AI_GetMindSet(entry.role);

  // input

  Vector3 rawInputDirection = player->GetDirectionVec();
  float rawInputVelocityFloat = idleVelocity;
  Vector3 manualMovementDirection = player->GetDirectionVec();
  float manualMovementVelocityFloat = idleVelocity;

  bool manualMovement = false;
  bool extraHaste = false;

  // celebrate good times come on!

  if (!match->IsInPlay() && match->IsGoalScored()) {
    _AddCelebration(commandQueue);
    return;
  }

  // look at referee

  else if (!match->IsInPlay() && match->GetReferee()->GetBuffer().active == true &&
           (match->GetReferee()->GetCurrentFoulType() == 2 ||
            match->GetReferee()->GetCurrentFoulType() == 3) &&
           match->GetReferee()->GetBuffer().stopTime < match->GetActualTime_ms() - 1000 &&
           match->GetReferee()->GetBuffer().prepareTime > match->GetActualTime_ms()) {
    // look at referee
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Movement;
    command.useDesiredMovement = true;
    command.useDesiredLookAt = true;
    command.desiredDirection = CastPlayer()->GetDirectionVec();
    assert(command.desiredDirection.coords[2] == 0.0f);
    command.desiredVelocityFloat = idleVelocity;
    command.desiredLookAt = match->GetOfficials()->GetReferee()->GetPosition();
    commandQueue.push_back(command);

    return;
  }

  // stand still

  else if (!match->IsInPlay()) {  // this whole if/then/else structure is ugly and unclear

    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Movement;
    command.useDesiredMovement = true;
    if (match->GetBallRetainer() == player) {
      command.desiredDirection =
          (Vector3(0) - player->GetPosition()).GetNormalized(player->GetDirectionVec());
    } else {
      command.desiredDirection = player->GetDirectionVec();
    }
    command.desiredVelocityFloat = idleVelocity;

    // Law 16: the opponents leave the penalty area before a goal kick is taken. They
    // walk out under their own steam - the taker waits for them (referee.cpp) - rather
    // than being teleported clear at the whistle. Without this a striker standing in
    // the six-yard box when the kick was awarded simply stayed there, and the kick
    // could be played to his feet.
    const e_SetPiece pending = team->GetController()->GetSetPieceType();
    const bool ourKick = team->GetController()->GetPieceTaker() != nullptr;
    // The box being kicked out of is the taker's own, which is the side we attack.
    const int defendedSide = -team->GetSide();
    if (!ourKick &&
        SetPieceLaws::ClearsThePenaltyArea(
            pending, match->GetReferee()->GetBuffer().restartPos.coords[0], defendedSide,
            pitchHalfW)) {
      const Vector3 here = player->GetPosition();
      if (SetPieceLaws::IntrudesOnPenaltyArea(here.coords[0], here.coords[1], defendedSide,
                                             pitchHalfW)) {
        float targetX = here.coords[0], targetY = here.coords[1];
        SetPieceLaws::ClearingTarget(here.coords[0], here.coords[1], defendedSide, pitchHalfW,
                                    &targetX, &targetY);
        const Vector3 target(targetX, targetY, 0);
        command.desiredDirection = (target - here).GetNormalized(player->GetDirectionVec());
        // Jogging, not sprinting: a player asked to step out of the box is not in a
        // hurry, and a sprint reads as a scramble.
        command.desiredVelocityFloat = walkVelocity;
        command.useDesiredLookAt = true;
        command.desiredLookAt = target;
        commandQueue.push_back(command);
        return;
      }
    }

    // Law 13: and at a free kick or a corner they stand off the ball. A defender who
    // was standing over it backs straight out of the circle - facing the ball, because
    // that is what a man retreating from a free kick does - rather than being shoved.
    if (!ourKick && SetPieceLaws::ClearsTheBallRadius(pending)) {
      const Vector3 here = player->GetPosition();
      const Vector3 ball = match->GetBall()->Predict(0).Get2D();
      if (SetPieceLaws::IntrudesOnBallRadius(here.coords[0], here.coords[1], ball.coords[0],
                                            ball.coords[1])) {
        float targetX = here.coords[0], targetY = here.coords[1];
        SetPieceLaws::RetreatTarget(here.coords[0], here.coords[1], ball.coords[0],
                                   ball.coords[1], &targetX, &targetY);
        const Vector3 target(targetX, targetY, 0);
        command.desiredDirection = (target - here).GetNormalized(player->GetDirectionVec());
        command.desiredVelocityFloat = walkVelocity;
        command.useDesiredLookAt = true;
        command.desiredLookAt = ball;
        commandQueue.push_back(command);
        return;
      }
    }

    if (!match->IsInSetPiece()) {
      command.desiredDirection =
          (player->GetDirectionVec() * 0.6f +
           (Vector3(0) - player->GetPosition()).GetNormalized(player->GetDirectionVec()) * 0.4f)
              .GetNormalized(player->GetDirectionVec());
      command.desiredVelocityFloat =
          ClampVelocity(player->GetFloatVelocity() * 0.95f - random(0.0f, 3.2f));
    }
    command.useDesiredLookAt = true;
    command.desiredLookAt =
        player->GetPosition() + (match->GetBall()->Predict(0).Get2D() - player->GetPosition())
                                        .GetNormalized(command.desiredDirection) *
                                    10.0f;
    commandQueue.push_back(command);

    return;
  }

  // set piece taking

  else if ((match->IsInSetPiece() && team->GetController()->GetPieceTaker() == player) ||
           match->GetBallRetainer() == player) {
    PlayerCommand actionCommand;

    if (team->GetController()->GetSetPieceType() == e_SetPiece_Penalty) {
      actionCommand.desiredFunctionType = e_FunctionType_Shot;
      actionCommand.useDesiredMovement = false;
      actionCommand.useDesiredLookAt = false;

      // During a shootout the placement comes from the taker's stats; a penalty
      // in open play stays as unpredictable as it was.
      float aimY = random(-5, 5);
      float aimPower = random(0.4f, 1.0f);
      float aimGoalX = -team->GetSide() * pitchHalfW;
      PenaltyShootoutController* shootout = match->GetPenaltyShootout();
      if (shootout && shootout->IsStarted() && !shootout->IsFinished()) {
        aimY = shootout->GetAimY();
        aimPower = shootout->GetAimPower();
        // The whole shootout is taken at one end, which may be the end this team
        // was defending during the match.
        aimGoalX = shootout->GetGoalX();
      }

      actionCommand.touchInfo.inputDirection =
          (Vector3(aimGoalX, aimY, 0) - CastPlayer()->GetPosition())
              .GetNormalized(Vector3(-team->GetSide(), 0, 0));
      actionCommand.touchInfo.inputPower = aimPower;
      actionCommand.touchInfo.autoDirectionBias = 0.0f;
      actionCommand.touchInfo.desiredDirection = actionCommand.touchInfo.inputDirection;
      actionCommand.touchInfo.desiredPower = aimPower;
      commandQueue.push_back(actionCommand);

    } else {
      actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
      actionCommand.useDesiredMovement = false;
      actionCommand.useDesiredLookAt = false;
      actionCommand.touchInfo.inputDirection = player->GetDirectionVec();
      actionCommand.touchInfo.inputPower = 0.5f;
      actionCommand.touchInfo.autoDirectionBias = 1.0f;
      actionCommand.touchInfo.autoPowerBias = 1.0f;

      actionCommand.touchInfo.forcedTargetPlayer = 0;

      Vector3 desiredTargetPosition;
      bool doCommand = true;

      if (team->GetController()->GetSetPieceType() == e_SetPiece_GoalKick) {
        if (random(0.0f, 1.0f) > 0.4f) {
          actionCommand.desiredFunctionType = e_FunctionType_HighPass;
          desiredTargetPosition = Vector3((pitchHalfW * -team->GetSide()) * 0.2f,
                                          random(-pitchHalfH, pitchHalfH), 0.0f);
        } else {
          actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
          desiredTargetPosition = Vector3(player->GetPosition().coords[0] * 0.9f,
                                          random(-pitchHalfH, pitchHalfH), 0.0f);
        }

      } else if (team->GetController()->GetSetPieceType() == e_SetPiece_KickOff) {
        actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
        desiredTargetPosition = player->GetPosition() + player->GetDirectionVec() * 1.0f;

      } else if (team->GetController()->GetSetPieceType() == e_SetPiece_FreeKick) {
        if (random(0.0f, 1.0f) > 0.5f) {
          actionCommand.desiredFunctionType = e_FunctionType_HighPass;
          desiredTargetPosition =
              Vector3(pitchHalfW * -team->GetSide(), random(-10.0f, 10.0f), 0.0f);
        } else {
          actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
          desiredTargetPosition = player->GetPosition() +
                                  Vector3(-team->GetSide() * 10.0f, random(-10.0f, 10.0f), 0.0f);
        }

      } else if (team->GetController()->GetSetPieceType() == e_SetPiece_Corner) {
        if (random(0.0f, 1.0f) > 0.3f) {
          actionCommand.desiredFunctionType = e_FunctionType_HighPass;
          desiredTargetPosition =
              Vector3((pitchHalfW * -team->GetSide()) * (0.99f - random(0.0f, 0.12f)),
                      random(-10.0f, 10.0f), 0.0f);
        } else {
          actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
          desiredTargetPosition = Vector3((pitchHalfW * -team->GetSide()) * 0.8f,
                                          player->GetPosition().coords[1] * 0.8f, 0.0f);
        }

      } else if (team->GetController()->GetSetPieceType() == e_SetPiece_ThrowIn) {
        actionCommand.desiredFunctionType = e_FunctionType_ShortPass;
        desiredTargetPosition = player->GetPosition();  // closest to player
        // Long Throw: hurled towards the box rather than to the nearest man.
        if (PlayerSkills::Has(CastPlayer()->GetPlayerData()->GetSkills(),
                              PlayerSkills::Skill::LongThrow)) {
          actionCommand.desiredFunctionType = e_FunctionType_HighPass;
          desiredTargetPosition += Vector3(-team->GetSide() * 18.0f, 0, 0);
        }

      } else if (match->GetBallRetainer() == player) {  // keeper fetched ball, probably
        // PES's after-catch distribution (ActionKeeperAfterCatchMove::exec
        // 0x1409e8880, docs/re/pes-re-keeper-port.md 2): the ball is carried
        // up-field first - at least 4 m, and never back across his own line -
        // and the teammate is picked from there, then the target steps 4 m
        // farther on the bearing to that teammate. The choice itself (throw /
        // short throw / punt) stays this engine's: PES reads a request kind,
        // and GF's pass types already are the three kinds.
        // ponytail: PES picks the teammate from a per-keeper 11-entry table
        // (team+0x22e4c) that is install data this engine does not carry, so
        // the nearest man to the carry point stands in for it.
        const pes::KeeperConstants& keeperConstants = match->GetPesKeeperConstants();
        // His own penalty-area line, signed with the attack direction: the Law-1
        // box line on his half (pitchHalfW - kAreaDepth), and the goal-area half
        // width for the lateral reflection.
        const int attackDir = -team->GetSide();
        const float ownPenaltyLineX =
            -attackDir * (pitchHalfW - SetPieceLaws::kAreaDepth);
        const pes::DistributionPoint carry = pes::PesAfterCatchCarry(
            player->GetPosition().coords[0], player->GetPosition().coords[1], attackDir,
            ownPenaltyLineX, SetPieceLaws::kAreaHalfWidth, keeperConstants);
        Player* targetPlayer = AI_GetClosestPlayer(team, Vector3(carry.x, carry.z, 0), CastPlayer());

        if (targetPlayer) {
          // check if this player is away from opponents, before throwing ball to him
          Player* closestOpp = AI_GetClosestPlayer(match->GetTeam(abs(team->GetID() - 1)),
                                                   targetPlayer->GetPosition(), 0);
          if (closestOpp) {
            if (((closestOpp->GetPosition() + closestOpp->GetMovement() * 0.1f) -
                 targetPlayer->GetPosition())
                        .GetLength() > 10.0f ||
                CastPlayer()->GetPossessionDuration_ms() >
                    4000) {  // last touch bias is maximum so he won't hold forever
              actionCommand.touchInfo.forcedTargetPlayer = targetPlayer;
            } else {
              doCommand = false;
            }
          }
        }

        if (doCommand) {
          // With no reachable teammate the carry point itself is the target,
          // which PES's fallback (the keeper, displaced 2 m) also lands on.
          const float targetX = targetPlayer ? targetPlayer->GetPosition().coords[0] : carry.x;
          const float targetZ = targetPlayer ? targetPlayer->GetPosition().coords[1] : carry.z;
          const pes::DistributionPoint aimed = pes::PesAfterCatchStep(
              carry.x, carry.z, targetX, targetZ, attackDir, ownPenaltyLineX,
              SetPieceLaws::kAreaHalfWidth, keeperConstants);
          desiredTargetPosition = Vector3(aimed.x, aimed.z, 0.0f);
        }
      }

      if (doCommand) {
        if (actionCommand.touchInfo.forcedTargetPlayer == 0)
          actionCommand.touchInfo.forcedTargetPlayer =
              AI_GetClosestPlayer(team, desiredTargetPosition, CastPlayer());
        AI_GetPass(CastPlayer(), actionCommand.desiredFunctionType,
                   actionCommand.touchInfo.inputDirection, actionCommand.touchInfo.inputPower,
                   actionCommand.touchInfo.autoDirectionBias, actionCommand.touchInfo.autoPowerBias,
                   actionCommand.touchInfo.desiredDirection, actionCommand.touchInfo.desiredPower,
                   actionCommand.touchInfo.targetPlayer,
                   actionCommand.touchInfo.forcedTargetPlayer);
        commandQueue.push_back(actionCommand);
      }
    }

    // remember, we are still in the 'set piece / ballretainer' if{}

    if (match->GetBallRetainer() != player) {  // must be set piece taker, then
      PlayerCommand command;
      command.desiredFunctionType = e_FunctionType_Movement;
      command.useDesiredMovement = true;
      command.useDesiredLookAt = true;

      AI_GetBallControlMovement(_mentalImage, CastPlayer(), player->GetDirectionVec(), walkVelocity,
                                command.desiredDirection, command.desiredVelocityFloat,
                                command.desiredLookAt);
      assert(command.desiredDirection.coords[2] == 0.0f);

      commandQueue.push_back(command);
      return;
    } else {  // must be ball retainer
      PlayerCommand command;
      command.desiredFunctionType = e_FunctionType_Movement;
      command.useDesiredMovement = true;
      command.desiredDirection =
          (actionCommand.touchInfo.desiredDirection.GetLength() > 0.0f)
              ? actionCommand.touchInfo.desiredDirection.Get2D().GetNormalized(
                    player->GetDirectionVec())
              : (Vector3(0) - player->GetPosition()).GetNormalized(player->GetDirectionVec());
      command.desiredVelocityFloat = idleVelocity;
      command.useDesiredLookAt = true;
      command.desiredLookAt = player->GetPosition() + command.desiredDirection * 10.0f;
      commandQueue.push_back(command);
      if (team->GetController()->GetSetPieceType() == e_SetPiece_ThrowIn) {
        // printf("elizacontroller throw in doCommand (green pilon == target)\n");
        // SetGreenDebugPilon(command.desiredLookAt);
      }
      return;
    }

  }

  // default strategies for defensively, midfielders and offensively positioned players

  else if (match->IsInPlay() && !match->IsInSetPiece() && match->GetBallRetainer() != player &&
           match->GetDesignatedPossessionPlayer() != player &&
           CastPlayer()->GetFormationEntry().role !=
               e_PlayerRole_GK) {  //(match->GetDesignatedPossessionPlayer() != player ||
                                   //(CastPlayer()->GetFormationEntry().role == e_PlayerRole_GK &&
                                   //! CastPlayer()->HasPossession()))) {
    if (CastPlayer()->GetFormationEntry().role == e_PlayerRole_LB ||
        CastPlayer()->GetFormationEntry().role == e_PlayerRole_CB ||
        CastPlayer()->GetFormationEntry().role == e_PlayerRole_RB) {
      defenseStrategy->RequestInput(_mentalImage, rawInputDirection, rawInputVelocityFloat);
    } else if (CastPlayer()->GetFormationEntry().role == e_PlayerRole_DM ||
               CastPlayer()->GetFormationEntry().role == e_PlayerRole_LM ||
               CastPlayer()->GetFormationEntry().role == e_PlayerRole_CM ||
               CastPlayer()->GetFormationEntry().role == e_PlayerRole_RM ||
               CastPlayer()->GetFormationEntry().role == e_PlayerRole_AM ||
               CastPlayer()->GetFormationEntry().role == e_PlayerRole_LW ||
               CastPlayer()->GetFormationEntry().role == e_PlayerRole_RW) {
      midfieldStrategy->RequestInput(_mentalImage, rawInputDirection, rawInputVelocityFloat);
    } else if (CastPlayer()->GetFormationEntry().role == e_PlayerRole_CF ||
               CastPlayer()->GetFormationEntry().role == e_PlayerRole_SS) {
      offenseStrategy->RequestInput(_mentalImage, rawInputDirection, rawInputVelocityFloat);
    }

  }

  // dribble, pass, etcetera
  else if (match->IsInPlay() && !match->IsInSetPiece() &&
           match->GetDesignatedPossessionPlayer() == player &&
           CastPlayer()->GetFormationEntry().role != e_PlayerRole_GK) {
    if (CastPlayer()->GetTimeNeededToGetToBall_ms() < 1000) {
      // Movement first, decision second. A designated chaser standing next to
      // a still ball he does not have is the shape the 14-second stalls take:
      // inPlay 1, ball at rest, forced man 0.4 m off it, nothing queued that
      // closes the gap. Pass selection runs and the step onto the ball never
      // does. So step onto it first - once he has it, the branch below is
      // business as usual.
      const float ballSpeed_ms = match->GetBall()->GetMovement().GetLength();
      const bool hasIt = match->GetBallRetainer() == player || CastPlayer()->HasPossession();
      // Not gated on the stalemate clock. The inPlay-1 stalls this exists for
      // idle at 5-14 s, well under ShouldReleaseBall's 15, so gating on it would
      // leave the measured case untouched. Close, designated, and without it:
      // step onto it.
      if (!hasIt && ballSpeed_ms <= GameplayTuning::kStalemateBallSpeed_ms) {
        const Vector3 toBall =
            match->GetBall()->GetPositionBuffer() - CastPlayer()->GetPosition();
        if (toBall.GetLength() > 0.1f) {
          PlayerCommand step;
          step.desiredFunctionType = e_FunctionType_Movement;
          step.useDesiredMovement = false;
          step.useDesiredLookAt = true;
          step.desiredDirection = toBall.GetNormalized(CastPlayer()->GetDirectionVec());
          step.desiredVelocityFloat = sprintVelocity;
          step.desiredLookAt = match->GetBall()->GetPositionBuffer();
          commandQueue.push_back(step);
          extraHaste = true;
        }
      } else {
        GetOnTheBallCommands(commandQueue, rawInputDirection, rawInputVelocityFloat);
      }
    } else {
      // Out of reach of the ball, so he cannot pass or dribble it - but he can
      // still run at it, and he must. This branch used to queue NOTHING: the
      // designated ball-goer stood still while every other player held his
      // shape, and a ball at rest more than a second from everyone froze the
      // whole pitch for the rest of the half (measured: 32:19 to 45:00).
      const Vector3 toBall = match->GetBall()->GetPositionBuffer() - CastPlayer()->GetPosition();
      if (toBall.GetLength() > 0.5f) {
        PlayerCommand run;
        run.desiredFunctionType = e_FunctionType_Movement;
        run.useDesiredMovement = false;
        run.useDesiredLookAt = true;
        run.desiredDirection = toBall.GetNormalized(CastPlayer()->GetDirectionVec());
        // Sprint: he is chasing a ball nobody else is going for.
        run.desiredVelocityFloat = sprintVelocity;
        run.desiredLookAt = match->GetBall()->GetPositionBuffer();
        commandQueue.push_back(run);
        extraHaste = true;
      }
    }
    extraHaste = false;  // todo: extra haste when touch anim is queued?
  }

  else if (match->IsInPlay() && !match->IsInSetPiece() &&
           CastPlayer()->GetFormationEntry().role == e_PlayerRole_GK) {
    // keeper's mental image for deflections is near-instant; let's just call it premonition ;)
    const MentalImage* keeperMentalImage = _mentalImage;
    static_cast<GoalieDefaultStrategy*>(goalieStrategy)
        ->CalculateIfBallIsBoundForGoal(keeperMentalImage);
    bool boundForGoal = static_cast<GoalieDefaultStrategy*>(goalieStrategy)->IsBallBoundForGoal();
    if (boundForGoal)
      manualMovement = true;
    if (CastPlayer() != match->GetDesignatedPossessionPlayer())
      manualMovement = true;
    goalieStrategy->RequestInput(keeperMentalImage, manualMovementDirection,
                                 manualMovementVelocityFloat);

    // if (boundForGoal && hasUniquePossession) printf("has unique possession, so no deflect anims
    // (poss: %f)\n", possessionAmount);
    if (!hasUniquePossession || possessionAmount < 3.4f) {
      // A ball bound for goal is exactly when he must be able to HOLD it.
      // `boundForGoal` used to switch the pickup anims off, so the harder the
      // shot to stop, the less he was allowed to catch it - and a ball he meets
      // but does not hold is a rebound. Measured: save rate 25%, beaten gaps of
      // 1-2 m, holds rising while saves fell, 14.25 goals a match against the
      // 3.88 the owner's target asks for. Whether he can make the save at all
      // is KeeperAttemptsSave's business, one call down; this decides only
      // whether the catch he makes can stick.
      bool onlyPickupAnims = false;
      if (possessionAmount > 1.3f)
        onlyPickupAnims = true;
      _KeeperDeflectCommand(commandQueue, onlyPickupAnims);
    }

    if (CastPlayer()->GetTimeNeededToGetToBall_ms() < 1000 &&
        match->GetDesignatedPossessionPlayer() == player) {
      GetOnTheBallCommands(commandQueue, rawInputDirection, rawInputVelocityFloat);
    }
  }

  bool forceMagnet = false;

  // team pressure
  float ballDistance =
      _mentalImage->GetBallPrediction(0).Get2D().GetDistance(player->GetPosition());
  if (match->IsInPlay() && !match->IsInSetPiece() &&
      ((team->GetController()->GetEndApplyTeamPressure_ms() > match->GetActualTime_ms() && team->GetController()
                                                                                                   ->GetTeamPressurePlayer() ==
                                                                                               player) /*|| (player == team->GetDesignatedTeamPossessionPlayer() && ballDistance < 8.0f && CastPlayer()->GetManMarkingID() == -1) */) &&
      !teamHasBestPossession && match->GetDesignatedPossessionPlayer() != player &&
      CastPlayer()->GetFormationEntry().role != e_PlayerRole_GK) {
    forceMagnet = true;
  }

  _SetInput(rawInputDirection, rawInputVelocityFloat);
  if (rawInputDirection.GetLength() != 0)
    lastDesiredDirection = rawInputDirection;
  else
    lastDesiredDirection = player->GetDirectionVec();
  lastDesiredVelocity = rawInputVelocityFloat;

  if (match->IsInPlay() && !match->IsInSetPiece()) {
    // ball control?
    _BallControlCommand(commandQueue, false, false,
                        false);  // last param true == enable sticky run direction.

    // trap?
    _TrapCommand(commandQueue);

    // interfere?
    bool byAnyMeans = false;
    _InterfereCommand(commandQueue, byAnyMeans);

    // sliding?
    _SlidingCommand(commandQueue);
  }

  // movement
  if (!manualMovement && match->IsInPlay() && !match->IsInSetPiece()) {
    Player* opp = match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer();

    if (CastPlayer() != match->GetDesignatedPossessionPlayer()) {
      float mindSet = AI_GetMindSet(CastPlayer()->GetDynamicFormationEntry().role);
      float huntDistanceThreshold = 10.0f + (1.0f - mindSet) * 10.0f;  // 10 + .. * 10
      huntDistanceThreshold *= 0.5f * CastPlayer()->GetStaminaPct() +
                               0.5f * (1.0f - NormalizedClamp(CastPlayer()->GetAverageVelocity(10),
                                                              idleVelocity, sprintVelocity));
      huntDistanceThreshold *= 0.3f + GetMatch()->GetMatchDifficulty() * 0.7f;

      if (forceMagnet) {
        // make sure the movement command magnets get the best input (which is then used as 'hint'
        // for the 'toball' functions)

        inputDirection = Vector3(team->GetSide() * pitchHalfW, 0, 0) - CastPlayer()->GetPosition();
        inputVelocityFloat = idleVelocity;
        inputDirection.Normalize(CastPlayer()->GetDirectionVec());

      } else if (!teamHasBestPossession && CastPlayer()->GetManMarkingID() == -1 &&
                 ((opp->GetPosition() + opp->GetMovement() * 0.12f) -
                  (CastPlayer()->GetPosition() + CastPlayer()->GetMovement() * 0.04f))
                         .GetLength() < huntDistanceThreshold) {  // defend player

        if (player == team->GetDesignatedTeamPossessionPlayer() && possessionAmount > 0.8f) {
          forceMagnet = true;  // don't give up battles too easily
          extraHaste = true;
        }

        // or, defend an opponent, if we don't have anything better to do anyway

        Player* opp = match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer();

        int huntingPlayersNum = 2;  // remember, this includes players with man marking id that are
                                    // not controlled by this hunting code
        std::vector<Player*> closestPlayers;
        AI_GetClosestPlayers(team, opp->GetPosition() + opp->GetMovement() * 0.1f,
                             closestPlayers, huntingPlayersNum);
        bool close = false;
        for (unsigned int i = 0; i < closestPlayers.size(); i++) {
          if (closestPlayers.at(i) == player) {
            close = true;
            break;
          }
        }

        if (close) {
          /*
          // 'easy off' method
          Vector3 defendPosition_easy = CastPlayer()->GetPosition();
          AddDefensiveComponent(defendPosition_easy, 1.0f, opp->GetID());
          inputDirection = (defendPosition -
          CastPlayer()->GetPosition()).GetNormalized(inputDirection); inputVelocityFloat =
          clamp((defendPosition - CastPlayer()->GetPosition()).GetLength() *
          distanceToVelocityMultiplier, 0.0f, sprintVelocity);
          //forceMagnet = true; // more aggressive!
          */

          // more 'hunting' method
          Vector3 defendPosition = GetDefendPosition(opp);
          if (NeedDefendingMovement(team->GetSide(), player->GetPosition(), defendPosition)) {
            inputDirection =
                (defendPosition - CastPlayer()->GetPosition()).GetNormalized(inputDirection);
            inputVelocityFloat = clamp((defendPosition - CastPlayer()->GetPosition()).GetLength() *
                                           distanceToVelocityMultiplier,
                                       0.0f, sprintVelocity);
            forceMagnet = true;  // more aggressive!
          }

        }  // close

      }  // else (!forceMagnet)

    }  // !designated

    inputVelocityFloat = RangeVelocity(inputVelocityFloat);  // emulate controller quantization

    _MovementCommand(commandQueue, forceMagnet, extraHaste);

  } else {  // keeper?

    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Movement;
    command.useDesiredMovement = true;
    command.desiredDirection = manualMovementDirection;
    assert(command.desiredDirection.coords[2] == 0.0f);
    command.desiredVelocityFloat = manualMovementVelocityFloat;
    command.useDesiredLookAt = true;
    command.desiredLookAt =
        player->GetPosition() +
        (_mentalImage->GetBallPrediction(40).Get2D() - player->GetPosition()).GetNormalized(0) *
            10.0f;
    commandQueue.push_back(command);
  }
}

void ElizaController::Process() {
  PlayerController::Process();
}

Vector3 ElizaController::GetDirection() {
  return lastDesiredDirection;
}

float ElizaController::GetFloatVelocity() {
  return lastDesiredVelocity;
}

void ElizaController::LoadStrategies() {
  defenseStrategy = new DefaultDefenseStrategy(this);
  midfieldStrategy = new DefaultMidfieldStrategy(this);
  offenseStrategy = new DefaultOffenseStrategy(this);
  goalieStrategy = new GoalieDefaultStrategy(this);
}

bool PreRatingSortFunc(const PreRating& a, const PreRating& b) {
  return a.totalRating > b.totalRating;
}

bool SortPlayersDeepestFirst(Player* a, Player* b) {
  return a->GetPosition().coords[0] * a->GetTeam()->GetSide() <
         b->GetPosition().coords[0] * b->GetTeam()->GetSide();
}

float ElizaController::GetLazyVelocity(float desiredVelocityFloat) {
  // input is unclamped! use large values (less lazy, apparently we are more off-position)
  float adaptedDesiredVelocityFloat = desiredVelocityFloat;
  // don't use full overflow though
  if (adaptedDesiredVelocityFloat > sprintVelocity)
    adaptedDesiredVelocityFloat =
        sprintVelocity + (adaptedDesiredVelocityFloat - sprintVelocity) * 0.1f;

  float startLazinessDistance = 20.0f * (CastPlayer()->GetStaminaPct() * 0.8f + 0.2f);
  float endLazinessDistance = 65.0f * (CastPlayer()->GetStaminaPct() * 0.5f + 0.5f);

  Vector3 oppPos =
      match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer()->GetPosition();
  float actionDistance = (player->GetPosition() - oppPos).GetLength();
  float teamPossession = clamp(GetFadingTeamPossessionAmount() - 0.5f, 0.0f, 1.0f);
  float mindSet = AI_GetMindSet(CastPlayer()->GetDynamicFormationEntry().role);

  // if mindSet is high (offensive), be lazy when teamPossession is low. and the other way round for
  // defenders. (midfielders are always half-lazy by role this way; pow() them a bit if this is
  // undesired; this will of course wear them out more than backs and strikers)
  float lazinessByRole = mindSet + teamPossession * (1.0f - mindSet * 2.0f);
  float lazinessByPosition =
      NormalizedClamp(actionDistance, startLazinessDistance, endLazinessDistance);

  float lazyFactor = lazinessByPosition * (0.5f + lazinessByRole * 0.5f);
  float resultingVelocityFloat = adaptedDesiredVelocityFloat * (1.0f - lazyFactor);

  bool clampToDribble = false;
  if (desiredVelocityFloat >= dribbleVelocity)
    clampToDribble =
        true;  // no standing still when initially undesired; it would get us further and further
               // away from our desired position *edit: no longer true with the unclamped input
  if (clampToDribble && resultingVelocityFloat < dribbleVelocity)
    resultingVelocityFloat = dribbleVelocity;

  // short term fatigue/work rate shortage ;)
  // does not heed dribble clamp above, as to simulate players having to stop to catch their breath
  float breathLeftFactor =
      1.0f - NormalizedClamp(CastPlayer()->GetAverageVelocity(10), idleVelocity, sprintVelocity);
  float workRate = CastPlayer()->GetStat("mental_workrate");
  breathLeftFactor = std::pow(breathLeftFactor, 0.8f - workRate * 0.2f);
  breathLeftFactor =
      clamp(breathLeftFactor * 1.2f, 0.0f, 1.0f);  // make sure beginning of sprint is full speed
  breathLeftFactor = breathLeftFactor * lazyFactor +
                     1.0f * (1.0f - lazyFactor);  // sometimes, we really need to force it
  resultingVelocityFloat = std::min(resultingVelocityFloat, sprintVelocity * breathLeftFactor);

  return resultingVelocityFloat;
}

Vector3 ElizaController::GetSupportPosition(const MentalImage* mentalImage,
                                            const Vector3& basePosition) {
  float offenseWeight =
      0.8f + (AI_GetMindSet(CastPlayer()->GetDynamicFormationEntry().role)) *
                 clamp((GetFadingTeamPossessionAmount() - 0.5f) * 2.0f, 0.0f, 1.0f);
  float distanceWeight = 1.0f;
  float passWeight = 0.8f;
  float movementWeight = 1.5f;
  float formationWeight =
      1.6f +
      clamp(
          (CastPlayer()->GetPosition() - mentalImage->GetBallPrediction(100).Get2D()).GetLength() /
              30.0f,
          0.0, 1.0f);
  float offsideWeight = 10.0f;

  Vector3 playerPos = player->GetPosition() + player->GetMovement() * 0.1f;  // + physics slowness

  // get closest opponents

  std::vector<Player*> opponents;
  std::vector<PlayerImage> opponentImages;
  AI_GetClosestPlayers(match->GetTeam(abs(team->GetID() - 1)), playerPos, opponents, 4);
  for (unsigned int i = 0; i < opponents.size(); i++) {
    PlayerImage oppImage = mentalImage->GetPlayerImage(opponents.at(i)->GetID());
    oppImage.position += oppImage.movement * 0.15f;
    opponentImages.push_back(oppImage);
  }

  // add candidate positions

  std::vector<Vector3> candidatePositions;
  candidatePositions.push_back(playerPos);
  float step = (2 * pi) / 12.0f;
  Vector3 pos;
  for (float rad = 0; rad < 2 * pi; rad += step) {
    pos.coords[0] = cos(rad);
    pos.coords[1] = sin(rad);
    candidatePositions.push_back(playerPos + pos * dribbleVelocity);
    candidatePositions.push_back(playerPos + pos * walkVelocity);
    candidatePositions.push_back(playerPos + pos * sprintVelocity);
  }

  // rate candidates

  Vector3 goalPos = Vector3(-team->GetSide() * pitchHalfW, playerPos.coords[1] * 0.8f,
                            0);  // y element is because goal is wider than 0
  float goalDistance = fabs((goalPos - playerPos).GetLength() - 5.0f);  // and not all too close
  float ballDistance = (playerPos - mentalImage->GetBallPrediction(100).Get2D()).GetLength();
  float formationDistance = (basePosition - playerPos).GetLength();
  float offside = AI_GetOffsideLine(match, mentalImage, abs(team->GetID() - 1));

  float passRating;  // are opponents blocking pass space to this position?

  float bestRating = 0;
  int bestCandidate = 0;

  std::vector<PreRating> preRatings;

  for (unsigned int i = 0; i < candidatePositions.size(); i++) {
    if (fabs(candidatePositions.at(i).coords[0]) < pitchHalfW &&
        fabs(candidatePositions.at(i).coords[1]) < pitchHalfH) {
      PreRating preRating;
      preRating.candidateID = i;

      // offense rating
      preRating.offenseRating =
          clamp((goalDistance - fabs((goalPos - candidatePositions.at(i)).GetLength() - 5.0)) /
                        sprintVelocity * 0.5 +
                    0.5,
                0.0, 1.0);

      // distance rating
      float candidateBallDistance =
          (candidatePositions.at(i) - mentalImage->GetBallPrediction(100).Get2D()).GetLength();
      float desiredBallDistance = 10.0f;
      // too close is bad as well..
      if (candidateBallDistance < desiredBallDistance)
        candidateBallDistance =
            desiredBallDistance + fabs(candidateBallDistance - desiredBallDistance);
      float candidateBallVsBallDistance = (ballDistance - candidateBallDistance);

      preRating.distanceRating =
          clamp((candidateBallVsBallDistance / sprintVelocity) * 0.5f + 0.5f, 0.0f, 1.0f);

      // movement rating
      preRating.movementRating =
          1.0 - clamp((player->GetMovement() - (candidatePositions.at(i) - playerPos)).GetLength() /
                          (2 * sprintVelocity),
                      0.0, 1.0);

      // formation rating
      preRating.formationRating =
          clamp(((formationDistance - (basePosition - candidatePositions.at(i)).GetLength()) /
                 sprintVelocity) *
                        0.5 +
                    0.5,
                0.0, 1.0);

      // offside rating
      bool isOffside = false;
      if (candidatePositions.at(i).coords[0] * -team->GetSide() >= offside * -team->GetSide())
        isOffside = true;
      preRating.offsideRating = isOffside ? 0.0 : 1.0;

      preRating.totalRating =
          preRating.offenseRating * offenseWeight + preRating.distanceRating * distanceWeight +
          preRating.movementRating * movementWeight + preRating.formationRating * formationWeight +
          preRating.offsideRating * offsideWeight;

      // small bonus for 'current position'
      if (i == 0)
        preRating.totalRating += 0.1f;

      preRatings.push_back(preRating);
    }
  }

  std::sort(preRatings.begin(), preRatings.end(), PreRatingSortFunc);

  for (unsigned int i = 0; i < preRatings.size() / 3; i++) {
    const PreRating& preRating = preRatings.at(i);
    int candidateID = preRating.candidateID;

    // pass rating
    passRating = 1.0;
    Vector3 checkPosition =
        candidatePositions.at(candidateID) +
        (mentalImage->GetBallPrediction(240).Get2D() - candidatePositions.at(candidateID))
                .GetNormalized(0) *
            2.0f;
    for (unsigned int o = 0; o < opponentImages.size(); o++) {
      float oppDist =
          pow(clamp((opponentImages.at(o).position - checkPosition).GetLength() / 8.0f, 0.0f, 1.0f),
              1.2f);  // 0 .. 1 == worst, best
      if (oppDist < passRating)
        passRating = oppDist;
    }

    // total rating
    float totalRating = preRating.totalRating + passRating * passWeight;
    if (totalRating > bestRating) {
      bestRating = totalRating;
      bestCandidate = candidateID;
    }
  }

  Vector3 result = candidatePositions.at(bestCandidate);

  return result;
}

Vector3 ElizaController::GetSupportPosition_ForceField(const MentalImage* mentalImage,
                                                       const Vector3& basePosition, bool makeRun) {
  Player* designatedPlayer = team->GetDesignatedTeamPossessionPlayer();

  Vector3 currentPos = player->GetPosition() + CastPlayer()->GetMovement() * 0.1f;  // basePosition;
  Vector3 mainManPos = designatedPlayer->GetPosition() + designatedPlayer->GetMovement() * 0.1f;

  std::vector<ForceSpot> forceField;

  // support position

  float dynamicMindSet = AI_GetMindSet(CastPlayer()->GetDynamicFormationEntry().role);
  float ballDistance = (match->GetBall()->Predict(250).Get2D() - player->GetPosition()).GetLength();
  float ballDistanceX =
      fabs(match->GetBall()->Predict(250).coords[0] - player->GetPosition().coords[0]);

  bool forceNoOffside = true;

  float basePositionWeight = 0.7f;
  float overallWeight = 1.0f;
  float opponentRepelWeight = 0.3f * overallWeight;
  float teammateRepelWeight = 0.4f * overallWeight;
  float ballRepelWeight = 1.0f * overallWeight;
  float runWeight = 1.0f * overallWeight;
  float flockToPossessionPlayerWeight = 0.45f * overallWeight;

  const float webScale = AITactics::GetSupportWebScale(
      team->GetController()->GetLiveTacticReal("support_distance", 0.5f));
#ifndef NDEBUG
  // [pass-dist] instrumentation: the mean spread of this player's nearest
  // teammates is the width of the passing network he actually lives in.
  {
    std::vector<Player*> nearMates;
    AI_GetClosestPlayers(team, currentPos, nearMates, 5);
    float mateSpreadSum = 0.0f;
    int mateCount = 0;
    for (Player* mate : nearMates) {
      if (mate == CastPlayer() || mate->GetFormationEntry().role == e_PlayerRole_GK) continue;
      mateSpreadSum += (mate->GetPosition() - player->GetPosition()).Get2D().GetLength();
      mateCount++;
    }
    if (mateCount > 0)
      match->GetMatchData()->AddSupportWebSample(team->GetID(), mateSpreadSum / mateCount);
  }
#endif

  switch (CastPlayer()->GetDynamicFormationEntry().role) {
    case e_PlayerRole_CB:
    case e_PlayerRole_LB:
    case e_PlayerRole_RB:
      opponentRepelWeight *= 2.2f;
      break;
    case e_PlayerRole_DM:
      opponentRepelWeight *= 2.0f;
      break;
    case e_PlayerRole_CM:
    case e_PlayerRole_LM:
    case e_PlayerRole_RM:
    case e_PlayerRole_LW:
    case e_PlayerRole_RW:
      opponentRepelWeight *= 1.6f;
      break;
    case e_PlayerRole_AM:
      opponentRepelWeight *= 1.2f;
      break;
    case e_PlayerRole_CF:
    case e_PlayerRole_SS:
      opponentRepelWeight *= 1.0f;
      break;
    default:
      break;
  }

  float offsideX = AI_GetOffsideLine(match, _mentalImage, abs(team->GetID() - 1));
  float adaptedMakeRun = makeRun;

  // actual base position
  {
    ForceSpot spot;
    spot.origin = basePosition;

    // let left and right flank flow forwards and backwards every period_sec seconds, for some
    // dynamics except for one close player, who should always run forward for support
    Player* forwardSupportPlayer = team->GetController()->GetForwardSupportPlayer();
    if (player == forwardSupportPlayer) {
      spot.origin.coords[0] += -team->GetSide() * (0.3f + 0.7f * dynamicMindSet) * 12.0f;
    } else {
      /* sine version
      float amount = 8.0f;//14.0f;//8.0f;
      float period_sec = 7.0f;
      float phase_offset = NormalizedClamp(currentPos.coords[1], -pitchHalfH, pitchHalfH);
      amount *= sin(match->GetActualTime_ms() * 0.001f * ((1.0f / period_sec) * 2.0f * pi) +
      (phase_offset * 2.0f * pi)) * 0.5f + 0.5f; float delta = -team->GetSide() *
      curve(dynamicMindSet, 0.5f) * (1.0f - pow(NormalizedClamp(ballDistanceX, 1.0f, 30.0f), 1.5f))
      * amount; // offensive crew spot.origin.coords[0] += delta;
      */
      // lane version
      float amount = 22.0f;
      float laneY = -signSide(mainManPos.coords[1]) * 8.0f;
      amount *=
          curve(1.0f - NormalizedClamp(fabs(laneY - currentPos.coords[1]), 0.0f, 30.0f), 1.0f);
      float delta = -team->GetSide() * std::pow(dynamicMindSet, 1.5f) * amount;
      spot.origin.coords[0] += delta;
    }

    spot.magnetType = e_MagnetType_Attract;
    spot.decayType = e_DecayType_Constant;
    spot.power = 1.0f * basePositionWeight;
    // the farther away from this position, the more we are attracted to it
    spot.power *=
        0.3f + 0.7f * NormalizedClamp((spot.origin - currentPos).GetLength(), 0.0f, 20.0f);
    forceField.push_back(spot);
  }

  if (adaptedMakeRun) {
    ForceSpot spot;
    spot.origin =
        Vector3(-team->GetSide() * pitchHalfW, currentPos.coords[1] * 0.5f,
                0.0f);  // player->GetDirectionVec() * 100.0f;// / distanceToVelocityMultiplier;
    spot.magnetType = e_MagnetType_Attract;
    spot.decayType = e_DecayType_Constant;
    spot.power = 2.0f * runWeight;
    forceField.push_back(spot);
  }

  // stay away from opponents
  std::vector<Player*> opponents;
  AI_GetClosestPlayers(match->GetTeam(abs(team->GetID() - 1)),
                       mainManPos * 0.3f + currentPos * 0.7f, opponents, 3);
  for (unsigned int i = 0; i < opponents.size(); i++) {
    const PlayerImage& oppImg = mentalImage->GetPlayerImage(opponents.at(i)->GetID());
    ForceSpot spot;
    Vector3 oppPos = oppImg.position + oppImg.movement * 0.1f;
    spot.origin =
        oppPos + (oppPos - mainManPos).GetNormalized(0) *
                     2.0f;  // anti-magnet behind opponent, because the pass-way must be cleared
    spot.magnetType = e_MagnetType_Repel;
    spot.decayType = e_DecayType_Variable;
    spot.power = 1.0f * opponentRepelWeight;
    spot.scale = 5.0f;
    if (adaptedMakeRun) {
      spot.scale = 2.0f;
      spot.power *= 0.5f;
    }
    spot.exp = 0.7f;
    forceField.push_back(spot);
  }

  // stay away from teammates
  if (team->GetFadingTeamPossessionAmount() >= 1.02f) {
    std::vector<Player*> players;
    AI_GetClosestPlayers(team, currentPos, players, 6);
    for (unsigned int i = 0; i < players.size(); i++) {
      if (players.at(i) != CastPlayer()) {
        const PlayerImage& mateImg = mentalImage->GetPlayerImage(players.at(i)->GetID());
        ForceSpot spot;
        spot.origin = mateImg.position + mateImg.movement * 0.1f;
        spot.magnetType = e_MagnetType_Repel;
        spot.decayType = e_DecayType_Variable;
        spot.power = 1.0f * teammateRepelWeight;
        spot.scale = 14.0f * webScale;
        spot.exp = 1.0f;
        forceField.push_back(spot);
      }
    }
  }

  // stay away from ball (to not get in the way of passes and possessionplayer)
  if (CastPlayer() != designatedPlayer && team->GetFadingTeamPossessionAmount() >= 1.06f) {
    ForceSpot spot;
    spot.magnetType = e_MagnetType_Repel;
    spot.decayType = e_DecayType_Variable;
    spot.power = 1.0f * ballRepelWeight;
    spot.scale = 2.0f;
    spot.exp = 0.5f;
    spot.origin = mentalImage->GetBallPrediction(200).Get2D();
    forceField.push_back(spot);
    spot.origin = mentalImage->GetBallPrediction(350).Get2D();
    forceField.push_back(spot);
    spot.origin = mentalImage->GetBallPrediction(500).Get2D();
    forceField.push_back(spot);
    spot.origin = mentalImage->GetBallPrediction(650).Get2D();
    forceField.push_back(spot);
  }

  if (CastPlayer() != designatedPlayer) {
    // attract to teammate in possession
    {
      ForceSpot spot;
      spot.origin = mainManPos;
      spot.magnetType = e_MagnetType_Attract;
      spot.decayType = e_DecayType_Variable;
      spot.power = 1.0f * flockToPossessionPlayerWeight;
      spot.scale = 28.0f * webScale;
      spot.exp = 1.0f;
      forceField.push_back(spot);
    }

    // ..yet not too close
    {
      ForceSpot spot;
      spot.origin = mainManPos;
      spot.magnetType = e_MagnetType_Repel;
      spot.decayType = e_DecayType_Variable;
      spot.power = 1.0f * flockToPossessionPlayerWeight;
      spot.scale = 16.0f * webScale;
      spot.exp = 1.0f;
      forceField.push_back(spot);
    }
  }

  Vector3 forceFieldPosition =
      currentPos + AI_GetForceFieldMovement(forceField, currentPos, 7);  // 8);

  float margin = 0.08f;
  if (forceNoOffside)
    if (forceFieldPosition.coords[0] * -team->GetSide() > (offsideX * -team->GetSide()) - margin)
      forceFieldPosition.coords[0] = offsideX - (margin * -team->GetSide());

  forceFieldPosition.coords[0] = clamp(forceFieldPosition.coords[0], -pitchHalfW, pitchHalfW);
  forceFieldPosition.coords[1] = clamp(forceFieldPosition.coords[1], -pitchHalfH, pitchHalfH);

  return forceFieldPosition;
}

void ElizaController::Reset() {
  lastDesiredDirection = Vector3(0);
  lastDesiredVelocity = 0;
  carrierPlan = 0;
  carrierPlanStamp = 0;
  carrierBusyStamp = 0;
}

void ElizaController::GetOnTheBallCommands(std::vector<PlayerCommand>& commandQueue,
                                           Vector3& rawInputDirection,
                                           float& rawInputVelocityFloat) {
  float oneTouchIsHard = 0.0f;
  float movementDiff = NormalizedClamp(
      (match->GetBall()->GetMovement() - CastPlayer()->GetMovement()).GetLength(), 0.0f, 10.0f);
  oneTouchIsHard =
      movementDiff - CastPlayer()->GetStat("technical_shortpass") * movementDiff * 0.8f;

  // Skills, playing style and philosophy shape how this player uses the ball.
  const PlayerSkills::Mask skills = CastPlayer()->GetPlayerData()->GetSkills();
  const PlayingStyles::Player style = CastPlayer()->GetPlayerData()->GetPlayingStyle();
  const PlayingStyles::ComMask comStyles = CastPlayer()->GetPlayerData()->GetComStyles();
  const TeamPhilosophy::e_Philosophy philosophy = team->GetController()->GetPhilosophy();
  // A one-touch passer does not pay the control penalty on a quick release.
  oneTouchIsHard = PlayerSkills::GetQuickReleaseAccuracyPenalty(
      skills, CastPlayer()->GetPossessionDuration_ms(), oneTouchIsHard);

  std::vector<PlayerImage> opponentPlayerImages;
  _mentalImage->GetTeamPlayerImages(abs(team->GetID() - 1), -1, opponentPlayerImages);

  // Panic under pressure: count opponents closing in and let temperament decide
  // whether the player loses his composure (proposal 3B).
  {
    int pressuringOpponents = 0;
    for (const PlayerImage& oppImage : opponentPlayerImages) {
      if ((oppImage.position - CastPlayer()->GetPosition()).GetLength() < 3.5f)
        pressuringOpponents++;
    }

    // A captain on the pitch steadies everybody; fighting spirit steadies himself.
    bool captainOnPitch = false;
    std::vector<Player*> mates;
    team->GetActivePlayers(mates);
    for (Player* mate : mates) {
      if (PlayerSkills::Has(mate->GetPlayerData()->GetSkills(), PlayerSkills::Skill::Captaincy))
        captainOnPitch = true;
    }
    const float stumbleChance =
        MatchPressure::GetStumbleChance(CastPlayer()->GetStat("mental_calmness"),
                                        CastPlayer()->GetPlayerData()->GetAge(),
                                        pressuringOpponents) *
        PlayerSkills::GetStumbleChanceMultiplier(skills, captainOnPitch);
    if (MatchPressure::ShouldStumble(stumbleChance, random(0.0f, 1.0f))) {
      _AddPanicPass(commandQueue, opponentPlayerImages);
      if (Verbose())
        printf("panic under pressure! chance %f, %i opponents\n", stumbleChance,
               pressuringOpponents);
    }
  }

  // panic
  float mindSet = AI_GetMindSet(CastPlayer()->GetDynamicFormationEntry().role);
  if (mindSet < 0.25f) {
    float panicProneness = 1.0f - mindSet * 2.0f;
    float goalCloseness =
        1.0f - NormalizedClamp((CastPlayer()->GetPosition() -
                                Vector3(pitchHalfW * CastPlayer()->GetTeam()->GetSide(), 0, 0))
                                   .GetLength(),
                               2.0f, 16.0f);  // 8.0f, 32.0f);
    if (CastPlayer()->GetDynamicFormationEntry().role != e_PlayerRole_GK) {
      // No pass found and no sight of goal is what a panic pass is for; the
      // possessionAmount term still keeps the carrier in control.
      if (possessionAmount < 0.9f + panicProneness * goalCloseness * 0.8f) {
        _AddPanicPass(commandQueue, opponentPlayerImages);
        if (Verbose())
          printf("panic! %f, %f, %f\n", possessionAmount, panicProneness, goalCloseness);
      }
    } else {  // keeper
      if (possessionAmount < 3.0f) {
        _AddPanicPass(commandQueue, opponentPlayerImages);
      }
    }
  }


  // PES 2021's ball-carrier decision layer (docs/re/pes-re-carrier-ai.md):
  // the plan the carrier runs, then the shoot decision, then the pass
  // decision, in the order PES adopts them. The cadence is report 2's: the
  // default dribble/keep answer is recomputed every tick, and a full think
  // only runs once the carrier has been free of a committed action for longer
  // than five PES frames (0.1 @0x142588200 + 0.5 @0x142565600 on the 54 Hz
  // game speed, converted to this engine's 10 ms tick).
  const pes::CarrierConstants& carrier = match->GetPesCarrierConstants();
  const int attackDir = -team->GetSide();
  const float attackSign = static_cast<float>(attackDir);
  const Vector3 carrierPos = CastPlayer()->GetPosition();
  const Vector3 ballPos = match->GetBall()->GetPositionBuffer();
  const Vector3 ballMove = match->GetBall()->GetMovement();
  const float ballHeight = ballPos.coords[2];
  const float degreesPerRadian = 180.0f / 3.14159265358979f;
  // PES's logic frame: one GF tick is 0.54 of a PES tick, so the carrier's
  // clock (its possession, in GF ticks) converts by the same factor
  // pesballphysics.hpp uses for the ball's per-tick factors.
  const int nowFrame =
      pes::PesFramesToGfTicks(static_cast<int>(CastPlayer()->GetPossessionDuration_ms() / Match::kTick_ms));
  // GF's committed-action equivalent of PES's `state+8 bit0` reset: inside a
  // pass, shot, trap or ball-control action the carrier is committed, so the
  // think timer restarts every such tick (0x140a6e320's property bits 0x30000).
  const e_FunctionType currentAction = CastPlayer()->GetCurrentFunctionType();
  if (currentAction == e_FunctionType_ShortPass || currentAction == e_FunctionType_LongPass ||
      currentAction == e_FunctionType_HighPass || currentAction == e_FunctionType_Shot ||
      currentAction == e_FunctionType_Trap || currentAction == e_FunctionType_BallControl) {
    carrierBusyStamp = nowFrame;
  }
  const bool rethink = pes::RethinkDue(nowFrame, carrierBusyStamp);
  bool shooting = false;
  bool passAnswered = false;

  // The shoot zone, over the ball in PES's frame (this engine's y is PES's z).
  const float finishing = CastPlayer()->GetStat("technical_shot");
  const bool longShotSkill = PlayerSkills::Has(skills, PlayerSkills::Skill::LongRangeShooting) ||
                             PlayerSkills::Has(skills, PlayerSkills::Skill::KnuckleShot) ||
                             PlayerSkills::Has(skills, PlayerSkills::Skill::DippingShots);
  const pes::ShootZone zone = pes::ComputeShootZone(ballPos.coords[0], ballPos.coords[1],
                                                   attackSign, finishing, longShotSkill,
                                                   pitchHalfW, goalHalfWidth, carrier);

  if (rethink) {
    // The plan (report 4.4): rolled whenever its own conditions hold, in the
    // priority order 9, 8, 2, 3, 1, 4, 5, 6, else Basic. Each plan's roll is
    // its own throw-away generator seeded with int(carrierClock * K) on slot
    // 2 and drawn once through RangeRaw(2, 100) (0x14211a89..0x14211aac2).
    struct PlanChoice {
      int plan;
      float seedK;
      float probability;
      bool eligible;
    };
    const float statBreakthrough = CastPlayer()->GetStat("technical_dribble");
    const bool skillBreakthrough = PlayerSkills::Has(skills, PlayerSkills::Skill::DoubleTouch);
    const bool comBreakthrough =
        PlayingStyles::Has(comStyles, PlayingStyles::Com::SpeedingBullet) ||
        PlayingStyles::Has(comStyles, PlayingStyles::Com::MazingRun) ||
        PlayingStyles::Has(comStyles, PlayingStyles::Com::IncisiveRun);
    const float carrierX = attackSign * carrierPos.coords[0];
    const PlanChoice choices[] = {
        {pes::kPlanBreakthrough, carrier.breakthroughSeedK,
         pes::PlanStartProbability(pes::kPlanBreakthrough, carrier, statBreakthrough,
                                   skillBreakthrough, comBreakthrough, false),
         // Breakthrough needs the mid band or a spot past the penalty-area
         // line (report 4.4's start conditions).
         pes::IsMid(zone) || carrierX > SetPieceLaws::kAreaDepth},
        {pes::kPlanMiddleShot, carrier.middleShotSeedK,
         pes::PlanStartProbability(pes::kPlanMiddleShot, carrier, finishing, longShotSkill, false,
                                   false),
         // MiddleShot needs the mid band (or the far sector past the line).
         pes::IsMid(zone)},
        {pes::kPlanCross, carrier.crossSeedK,
         pes::PlanStartProbability(
             pes::kPlanCross, carrier, finishing,
             PlayerSkills::Has(skills, PlayerSkills::Skill::PinpointCrossing),
             PlayingStyles::Has(comStyles, PlayingStyles::Com::EarlyCross), false),
         // Cross needs the carrier inside the cross zone: the close band.
         pes::IsClose(zone)},
        {pes::kPlanLong, carrier.longSeedK,
         pes::PlanStartProbability(pes::kPlanLong, carrier,
                                   CastPlayer()->GetStat("technical_highpass"), false, false,
                                   false),
         true},
    };
    int chosen = pes::kPlanBasic;
    for (const PlanChoice& c : choices) {
      if (!c.eligible || c.probability <= 0.0f) continue;
      if (pes::PlanRoll(nowFrame, c.seedK, match->GetPesRngConstants(), carrier) <=
          static_cast<int>(c.probability)) {
        chosen = c.plan;
        break;
      }
    }
    carrierPlan = chosen;
    carrierPlanStamp = nowFrame;
  }

  // The shoot decision (report 5.1): the shoot zone, the "just shoot" rule
  // and the box/header rule.
  const Vector3 facing = CastPlayer()->GetDirectionVec();
  const float facingAngle = std::atan2(-facing.coords[1], -facing.coords[0]) * degreesPerRadian;
  const float aimVsFacing =
      std::fabs(WrapDeg((attackSign < 0.0f ? 180.0f : 0.0f) - facingAngle));
  Player* keeper = match->GetTeam(abs(team->GetID() - 1))->GetGoalie();
  pes::ShootInput shoot;
  shoot.zone = zone;
  shoot.ballHeight_m = ballHeight;
  shoot.height_m = CastPlayer()->GetStat("physical_height") * 100.0f;
  shoot.owned = CastPlayer()->GetPossessionDuration_ms() > 0;
  shoot.planId = carrierPlan;
  shoot.aimVsFacing_deg = aimVsFacing;
  shoot.keeperPresent = keeper != nullptr;
  shoot.keeperSettled =
      keeper != nullptr && keeper->GetCurrentFunctionType() == e_FunctionType_Movement;
  if (pes::ShootSubKind(shoot, carrier) == 1) {
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Shot;
    command.useDesiredMovement = false;
    command.useDesiredLookAt = false;
    command.desiredVelocityFloat = rawInputVelocityFloat;
    // The aim point: the goal centre, biased to the post the ball's sector
    // favours. ponytail: PES's aim function 0x14211f090 is not decoded (report
    // 9.1), so this engine's own aim stands in for it.
    const float y = zone.sector == pes::kSectorSideLow
                        ? -goalHalfWidth
                        : (zone.sector == pes::kSectorSideHigh ? goalHalfWidth : 0.0f);
    command.touchInfo.desiredDirection =
        (Vector3((pitchHalfW + 1.0f) * -team->GetSide(), y, 0) -
         (carrierPos + CastPlayer()->GetMovement() * 0.2f))
            .GetNormalized(Vector3(-team->GetSide(), 0, 0));
    command.touchInfo.desiredDirection =
        (command.touchInfo.desiredDirection * 0.7f +
         -CastPlayer()->GetDirectionVec() *
             (CastPlayer()->GetFloatVelocity() / sprintVelocity) * 0.3f)
            .GetNormalized();
    command.touchInfo.autoDirectionBias = 1.0f;
    const float goalDist = NormalizedClamp(
        (Vector3(pitchHalfW * -team->GetSide(), 0, 0) - carrierPos).GetLength(), 0.0f, 32.0f);
    command.touchInfo.desiredPower =
        random(0.7f * (0.6f + goalDist * 0.4f), 1.0f * (0.6f + goalDist * 0.4f));
    commandQueue.push_back(command);
    shooting = true;
  }

  // The pass decision (report 4.3 + 5.2): enumerate the mates, open each
  // lane with the interception formula, and let PassForward pick the tier.
  // Enumerated on every think, gated on the same re-think clock as the plan,
  // so a carrier who has just collected the ball does not re-roll his passing
  // every tick.
  if (!shooting && rethink) {
    // The carrier's pressure class from the nearest opponent: the report 4.2
    // radii are 4 m in the own third and 2 m in the attacking third, which is
    // what a nearest-opponent distance gives when the class ladder is 0
    // (inside r1), 1 (r2), 2 (r3), 3 (free).
    const float pressureRadius = attackDir * carrierPos.coords[0] < -pitchHalfW / 3.0f ? 4.0f : 2.0f;
    const float nearestOpp = [&] {
      float best = 1000.0f;
      for (const PlayerImage& opp : opponentPlayerImages) {
        const float d = (opp.position - carrierPos).GetLength();
        if (d < best) best = d;
      }
      return best;
    }();
    int carrierPressureClass = 3;
    if (nearestOpp < pressureRadius) carrierPressureClass = 0;
    else if (nearestOpp < 2.3f * pressureRadius) carrierPressureClass = 1;
    else if (nearestOpp < 3.0f * pressureRadius) carrierPressureClass = 2;

    // The lanes: 11 frames ahead, the owned-ball value of report 4.3's `F`.
    constexpr float kLaneFrames = 11.0f;
    const float laneTime_s = kLaneFrames * Match::kTick_ms * 0.001f / pes::kGfTicksPerPesTick;
    const float lowPass = CastPlayer()->GetStat("technical_shortpass");
    const float carrierSpeed_kmh = CastPlayer()->GetMovement().GetLength() * 3.6f;

    pes::PassForwardInput in;
    in.owned = true;
    in.close = pes::IsClose(zone);
    in.carrierPressureClass = carrierPressureClass;
    in.attackSign = attackSign;
    in.planAngle_deg = 0.0f;  // ponytail: the plan's desired angle (+0xC0) is
                              // not decoded (report 9.3); the attack direction
                              // stands in for it.
    in.imageState = 0;  // ponytail: the image state (+0x32B0+8) has no GF
                        // counterpart; state 0 is the neutral one.
    in.halfLen_m = pitchHalfW;
    in.goalHalfWidth_m = goalHalfWidth;
    in.goalAreaHalfWidth_m = SetPieceLaws::kAreaHalfWidth;
    in.mateLaneOk = true;  // ponytail: 0x140a4c8e0 is not decoded.

    std::vector<pes::PassCandidate> candidates;
    std::vector<Player*> mates;
    team->GetActivePlayers(mates);
    for (Player* mate : mates) {
      if (mate == CastPlayer()) continue;
      if (mate->GetFormationEntry().role == e_PlayerRole_GK) continue;  // status 5
      // The mate's target is his position a lane-time ahead (record A, "to
      // feet"); record B ("into space") is not ported.
      const Vector3 matePos = mate->GetPosition();
      const Vector3 matePred = matePos + mate->GetMovement() * laneTime_s;
      const Vector3 target3(matePred.coords[0], matePred.coords[1], ballPos.coords[2]);
      const float dist = (target3 - ballPos).GetLength();
      if (dist > in.halfLen_m) continue;  // off the pitch
      const float dx = target3.coords[0] - ballPos.coords[0];
      const float dz = target3.coords[1] - ballPos.coords[1];
      // The pass's forward angle is its bearing against the attack direction,
      // and its turn angle the same bearing against the carrier's facing
      // (report 4.3's +0xE4 and +0xE8). The absolute bearing stays local: the
      // openness loop measures each opponent against the pass direction from
      // it.
      const float bearing = std::atan2(dz, dx) * degreesPerRadian;
      const Vector3 facing = CastPlayer()->GetDirectionVec();
      const float facingAngle = std::atan2(-facing.coords[1], -facing.coords[0]) * degreesPerRadian;
      const float fwdAngle = WrapDeg(bearing - (attackSign < 0.0f ? 180.0f : 0.0f));
      const float turnAngle = WrapDeg(bearing - facingAngle);

      // The openness: the minimum over the opponents in front of the pass.
      float bestMargin = 150.0f;
      for (const PlayerImage& opp : opponentPlayerImages) {
        const float oppDx = opp.position.coords[0] - ballPos.coords[0];
        const float oppDz = opp.position.coords[1] - ballPos.coords[1];
        if (std::fabs(WrapDeg(std::atan2(oppDz, oppDx) * degreesPerRadian - bearing)) >
            carrier.openInFront_deg)
          continue;
        // The opponent's predicted point along the lane.
        const Vector3 oppPred =
            opp.position + opp.movement * laneTime_s;
        const float odx = oppPred.coords[0] - ballPos.coords[0];
        const float odz = oppPred.coords[1] - ballPos.coords[1];
        const float laneLen = std::sqrt(dx * dx + dz * dz);
        const float along = (odx * dx + odz * dz) / std::max(laneLen, 0.001f);
        const float perp = std::fabs(odx * dz - odz * dx) / std::max(laneLen, 0.001f);
        bestMargin = std::min(bestMargin,
                              pes::LaneOpennessMargin(dx, dz, along, perp, 1.0f, 0.0f, carrier));
      }
      const float difficulty = pes::ReceiverDifficulty(
          ballMove.GetLength(), dist, 0.0f,
          PlayerSkills::Has(skills, PlayerSkills::Skill::OneTouchPass), carrierSpeed_kmh, false,
          turnAngle, ballHeight, lowPass, carrier);

      pes::PassCandidate c;
      c.index = mate->GetID();
      c.record.target = {target3.coords[0], target3.coords[1]};
      c.record.passAngle_deg = fwdAngle;
      c.record.fwdAngle_deg = fwdAngle;
      c.record.turnAngle_deg = turnAngle;
      c.record.ballToTarget_m = dist;
      c.record.difficulty = difficulty;
      c.record.openness = bestMargin;
      c.record.opennessClass = pes::OpennessClassFromMargin(bestMargin, carrier);
      c.record.dirClass = pes::DirectionClass(fwdAngle, carrier);
      c.record.turnClass = pes::TurnClass(turnAngle, carrier);
      // The mate's own pressure: the nearest opponent to him decides both the
      // short-forward gate and the lofted tier's keeper-distance stand-in.
      float mateNearest = 1000.0f;
      for (const PlayerImage& opp : opponentPlayerImages) {
        const float d = (opp.position - matePos).GetLength();
        if (d < mateNearest) mateNearest = d;
      }
      c.nearestOppToMate_m = mateNearest;
      c.pressureClass = mateNearest < 2.0f ? 0 : (mateNearest < 4.6f ? 1 : (mateNearest < 6.0f ? 2 : 3));
      // ponytail: the pressure entry's distance to opponent slot 0 (INF the
      // keeper) is the nearest opponent's distance; the lofted tier compares a
      // linear distance against a squared one, which never passes on a full
      // pitch, so this stands in for it.
      c.pressureDistOpp0_m = mateNearest * mateNearest;
      candidates.push_back(c);
    }

    const pes::PassAnswer answer = pes::PassForward(candidates, {}, in, carrier);
    if (answer.index >= 0) {
      Player* target = match->GetPlayer(answer.index);
      if (target) {
        e_FunctionType passType = e_FunctionType_ShortPass;
        if (answer.kind == pes::kPassLong) passType = e_FunctionType_HighPass;
        else if (answer.kind == pes::kPassThrough) passType = e_FunctionType_ShortPass;
        _AddPass(commandQueue, target, passType);
        passAnswered = true;
      }
    }
  }

  e_Velocity enumVelocity = e_Velocity_Idle;
  AI_GetBestDribbleMovement(match, player->GetID(), _mentalImage, rawInputDirection,
                            rawInputVelocityFloat, team->GetTeamData()->GetTactics());
}

void ElizaController::_AddPass(std::vector<PlayerCommand>& commandQueue, Player* target,
                               e_FunctionType passType) {
  PlayerCommand command;
  command.desiredFunctionType = passType;
  command.useDesiredMovement = false;
  command.useDesiredLookAt = false;
  command.touchInfo.targetPlayer = 0;
  command.touchInfo.forcedTargetPlayer = target;
  command.touchInfo.inputDirection = Vector3(0);
  command.touchInfo.inputPower = 0;
  command.touchInfo.autoDirectionBias = 1.0f;
  command.touchInfo.autoPowerBias = 1.0f;
  AI_GetPass(CastPlayer(), passType, command.touchInfo.inputDirection, command.touchInfo.inputPower,
             command.touchInfo.autoDirectionBias, command.touchInfo.autoPowerBias,
             command.touchInfo.desiredDirection, command.touchInfo.desiredPower,
             command.touchInfo.targetPlayer, command.touchInfo.forcedTargetPlayer);
  commandQueue.push_back(command);
}

void ElizaController::_AddPanicPass(std::vector<PlayerCommand>& commandQueue,
                                    const std::vector<PlayerImage>& opponentPlayerImages) {
  int yside = signSide(player->GetDirectionVec().coords[1]);  // > 0 ? 1 : -1;
  Vector3 sensibleAwayDir =
      ((player->GetDirectionVec() * Vector3(0.8f, 1.0f, 0.0f)).GetNormalized() +
       Vector3(-team->GetSide() * 0.7f, yside * 0.5f, 0))
          .GetNormalized(0) +
      Vector3(0, 0, 0.3f);
  sensibleAwayDir.Normalize(player->GetDirectionVec());

  // A blind hoof in one fixed direction was a gift to the nearest interceptor
  // at exactly the moments the carrier is most vulnerable. Probe the default
  // direction and its two neighbours; kick into the safest lane. If every
  // lane is hopeless, the desperate clearance stands.
  float laneOdds[3];
  Vector3 laneDirs[3];
  for (int probe = -1; probe <= 1; probe++) {
    laneDirs[probe + 1] = sensibleAwayDir.GetRotated2D(probe * 0.25f * pi);
    laneOdds[probe + 1] =
        _GetPassingOdds(player->GetPosition() + laneDirs[probe + 1] * 18.0f,
                        e_FunctionType_HighPass, opponentPlayerImages, 1.0f);
  }
  const Vector3 panicDir = laneDirs[GameplayTuning::GetSafestPanicLane(laneOdds)];

  PlayerCommand command;
  command.useDesiredMovement = false;
  command.useDesiredLookAt = false;
  // No intended recipient: this is a clearance, not a pass. MatchData keeps the
  // two apart so passing accuracy measures passing.
  command.touchInfo.isClearance = true;

  command.touchInfo.inputDirection = panicDir;
  command.touchInfo.desiredDirection = panicDir;
  command.touchInfo.autoDirectionBias = 0.0f;
  command.touchInfo.autoPowerBias = 0.0f;

  command.desiredFunctionType = e_FunctionType_HighPass;
  command.touchInfo.inputPower = 0.7f;
  command.touchInfo.desiredPower = 0.7f;
  commandQueue.push_back(command);

  command.desiredFunctionType = e_FunctionType_Shot;
  command.touchInfo.inputPower = 0.6f;
  command.touchInfo.desiredPower = 0.6f;
  commandQueue.push_back(command);

  command.desiredFunctionType = e_FunctionType_LongPass;
  command.touchInfo.inputPower = 0.8f;
  command.touchInfo.desiredPower = 0.8f;
  commandQueue.push_back(command);
}

float ElizaController::_GetPassingOdds(Player* targetPlayer, e_FunctionType passType,
                                       const std::vector<PlayerImage>& opponentPlayerImages,
                                       float ballVelocityMultiplier) {
  float initialTargetDistance = (targetPlayer->GetPosition() - player->GetPosition()).GetLength();
  // A high ball needs room to come down; over a short distance it is never the
  // right pass.
  if (passType == e_FunctionType_HighPass && initialTargetDistance < 10.0f)
    return 0.0f;

  // Evaluate the pass where it will actually be aimed: AI_GetPass leads the
  // receiver by the same amount, and rating a different point than the one the
  // ball is struck at is how a pass can score well and still arrive nowhere.
  Vector3 target = targetPlayer->GetPosition() +
                   targetPlayer->GetMovement() *
                       GameplayTuning::GetReceiverLeadTime_sec(initialTargetDistance);
  if (passType == e_FunctionType_LongPass)
    target += Vector3(-team->GetSide() * initialTargetDistance * 0.2f, 0, 0);

  return _GetPassingOdds(target, passType, opponentPlayerImages, ballVelocityMultiplier);
}

float ElizaController::_GetPassingOdds(const Vector3& target, e_FunctionType passType,
                                       const std::vector<PlayerImage>& opponentPlayerImages,
                                       float ballVelocityMultiplier) {
  float secondScale = 1.0f;  // how many seconds of range to measure danger in

  Vector3 origin = player->GetPosition() + player->GetMovement() * 0.12f;

  // draw imaginary line between this and target player
  Line line(origin, target);

  float danger = 0.0f;
  for (unsigned int opp = 0; opp < opponentPlayerImages.size(); opp++) {
    Vector3 oppPos = opponentPlayerImages.at(opp).position +
                     opponentPlayerImages.at(opp).movement * 0.2f;  // + time needed to brake
    float u;  // % of line opp is closest to (0 .. 1)
    float oppDistance;
    oppDistance = line.GetDistanceToPoint(oppPos, u);

    if (u >= 0.0f && u <= 1.0f + 0.2f) {  // opp is dangerous in the first place
      if ((passType == e_FunctionType_HighPass && (u < 0.2f || u > 0.65f)) ||
          passType != e_FunctionType_HighPass) {
        float clampedU = clamp(u, 0.0f, 1.0f);
        Vector3 intersect = origin * (1.0f - clampedU) +
                            target * clampedU;  // where opp is most likely to intercept ball

        float oppToIntersect_sec =
            (oppDistance + 1.0f) / sprintVelocity;  // add some distance to correct for acceleration

        // How long the ball is in the air before it reaches the point this
        // opponent would meet it at. `intersect` is already u of the way down
        // the lane, so its distance from the origin carries u once; the old
        // line multiplied by u a second time and priced a mid-lane interception
        // at u-squared of its real flight. That is exactly where an interceptor
        // stands, so every marked lane read as faster - and safer - than it was.
        Vector3 originToBallPos = (intersect - origin);
        float penaltyTime = (passType == e_FunctionType_HighPass && u > 0.5f)
                                ? 2.5f
                                : 0.0f;  // trapping high balls takes time
        float ballToIntersect_sec = 0.7f + originToBallPos.GetLength() * 0.03f + penaltyTime;
        ballToIntersect_sec *= 1.0f / ballVelocityMultiplier;

        danger += clamp(ballToIntersect_sec - oppToIntersect_sec + (secondScale * 0.5f), 0.0f,
                        secondScale);  // add some seconds because 0 seconds doesn't mean 0 danger
      }
    }
  }

  // Pressure at the receiving end, not just along the lane: a target with a
  // marker on his shoulder used to score the same as a free man.
  float nearestOpponentDistance = 1000.0f;
  for (unsigned int opp = 0; opp < opponentPlayerImages.size(); opp++) {
    Vector3 oppPos = opponentPlayerImages.at(opp).position +
                     opponentPlayerImages.at(opp).movement * 0.2f;
    nearestOpponentDistance = std::min(nearestOpponentDistance, (oppPos - target).GetLength());
  }

  if (passType == e_FunctionType_HighPass)
    danger += 0.4f;  // just don't prefer high passing when low passing is applicable as well
  danger = NormalizedClamp(
      danger, 0.0f, secondScale);  // 1 super dangerous dude is basically the same as 100% danger
  float odds = (1.0f - danger) *
               (1.0f - GameplayTuning::GetReceiverPressureDanger(nearestOpponentDistance));
  // Danger only covers being intercepted. A pass also has to be struck and
  // received, and both get harder the further it travels, so price the distance
  // itself rather than treating an empty forty-metre channel as a certainty.
  odds *= GameplayTuning::GetPassExecutionOdds((target - origin).Get2D().GetLength());

  return odds;
}

void ElizaController::_AddCelebration(std::vector<PlayerCommand>& commandQueue) {
  signed int xSide = (match->GetBall()->Predict(0).Get2D().coords[0] > 0) ? 1 : -1;
  signed int ySide = team->GetSide();
  if (team->GetLastTouchPlayer())
    ySide = (team->GetLastTouchPlayer()->GetPosition().coords[1] > 0) ? 1 : -1;
  // A dozen metres towards the crowd in the corner of the half he scored in, not the
  // corner flag itself: he used to be sent to the exact corner and then frozen there.
  float runX = player->GetPosition().coords[0], runY = player->GetPosition().coords[1];
  GoalCelebration::RunTarget(runX, runY, xSide, ySide, pitchHalfW, pitchHalfH, &runX, &runY);
  Vector3 celebrationPosition = Vector3(runX, runY, 0);

  // Flat by contract: Humanoid::SelectAnim asserts a movement direction has no
  // height. A scorer lifted off the turf by his own anim would otherwise hand it
  // a tilted direction.
  Vector3 desiredDirection =
      (celebrationPosition - player->GetPosition()).Get2D().GetNormalized();
  float desiredVelocityFloat =
      ClampVelocity((celebrationPosition - player->GetPosition()).GetLength() / 4.0f);
  Vector3 desiredLookAt = player->GetPosition() + player->GetDirectionVec() * 1000;

  int celebrationType = 1;
  if (match->GetLastGoalTeamID() != team->GetID()) {
    celebrationType = 2;
    desiredVelocityFloat = idleVelocity;
  } else {
    celebrationType = 1;
  }

  int madeGoal = 1;
  if (celebrationType == 1) {
    if (team->GetLastTouchPlayer() == player) {
      madeGoal = 2;
      desiredVelocityFloat = sprintVelocity;
    } else {
      madeGoal = 1;
      if (team->GetLastTouchPlayer() != 0) {
        if ((team->GetLastTouchPlayer()->GetPosition() - player->GetPosition()).GetLength() < 20) {
          celebrationPosition = team->GetLastTouchPlayer()->GetPosition();
          desiredDirection =
              ((team->GetLastTouchPlayer()->GetPosition() * 0.5 + celebrationPosition * 0.5) -
               player->GetPosition())
                  .Get2D()
                  .GetNormalized();
          desiredVelocityFloat = ClampVelocity(
              (team->GetLastTouchPlayer()->GetPosition() - player->GetPosition()).GetLength() /
              2.0);
        } else {
          desiredVelocityFloat = idleVelocity;
        }
        desiredLookAt = player->GetPosition() + team->GetLastTouchPlayer()->GetDirectionVec() * 100;
      }
    }
  }

  // celebration

  // The performance runs for as long as its clip does, rather than for a flat slice
  // of the stoppage: a 5.2 s celebration was being cut at 4 s, which dropped the
  // scorer out of his pose and into ordinary play with the camera still on him.
  // He performs when he gets there. Two seconds after the goal the performance used to
  // take over wherever he had reached, which is a man stopping dead mid-stride and
  // dancing on the spot - and a blocked scorer still celebrates, on the cap.
  const float toTarget = (celebrationPosition - player->GetPosition()).GetLength();
  const bool ready =
      match->GetLastGoalScorer() != player ||
      GoalCelebration::HasArrived(toTarget, match->GetGoalScoredTimer());
  if (ready && GoalCelebration::IsPerforming(match->GetGoalScoredTimer(),
                                             GoalSequence::kCelebration_ms)) {
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Special;
    command.useSpecialVar1 = true;
    command.specialVar1 = celebrationType;
    command.useSpecialVar2 = true;
    // A PES celebration is two clips: an opening, and a loop held until it is over.
    // Asking for the opening the whole way through played it and then dropped the
    // player back into whatever came next, which is why a celebration read as
    // unfinished (goalcelebration.hpp, tools/pes21_import/install_anims.py).
    // The clip lengths are GF's defaults: the per-goal choice of a filmed PES
    // celebration, which set them per scorer, was presentation and is gone.
    const int opening = madeGoal;
    const int loop = GoalCelebration::LoopVariable(opening);
    // Only ask for the loop when a loop was installed.
    const bool loopExists = match->GetAnimCollection() &&
                            match->GetAnimCollection()->HasSpecial(celebrationType, loop);
    command.specialVar2 = loopExists && GoalCelebration::Phase(
                                            match->GetGoalScoredTimer(),
                                            GoalCelebration::kIntroHold_ms) ==
                                            GoalCelebration::e_Loop
                              ? loop
                              : opening;
    command.useDesiredMovement = false;
    command.useDesiredLookAt = false;
    commandQueue.push_back(command);
  }

  // movement

  {
    PlayerCommand command;
    command.desiredFunctionType = e_FunctionType_Movement;
    command.useDesiredMovement = true;
    command.desiredDirection = desiredDirection;
    command.desiredVelocityFloat = desiredVelocityFloat;
    command.useDesiredLookAt = true;
    command.desiredLookAt = desiredLookAt;
    commandQueue.push_back(command);
  }
}

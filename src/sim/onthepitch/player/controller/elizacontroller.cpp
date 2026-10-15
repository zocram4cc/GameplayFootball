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
#include "onthepitch/pesdefenderdecisions.hpp"
#include "onthepitch/pesdodge.hpp"
#include "onthepitch/matchpressure.hpp"
#include "onthepitch/peskeeper.hpp"
#include "onthepitch/setpiecelaws.hpp"
#include "onthepitch/penaltyshootoutcontroller.hpp"
#include "onthepitch/teaminstructions.hpp"
#include "strategies/offtheball/default_def.hpp"
#include "strategies/offtheball/default_mid.hpp"
#include "strategies/offtheball/default_off.hpp"
#include "strategies/offtheball/goalie_default.hpp"
#include "strategies/strategy.hpp"
#include "onthepitch/goalcelebration.hpp"
#include "onthepitch/goalsequence.hpp"
#include "onthepitch/pescarrier.hpp"
#include "onthepitch/pesballphysics.hpp"

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
        // 0x1409e8880, docs/re/pes-re-keeper-port.md 2 and the corrections of
        // pes-re-goalkeeper.md 10.6.2). PES's point is where the keeper WALKS
        // while holding the ball, not the pass target - the pass target is the
        // chosen team-mate, which is what this uses it for: he walks toward
        // the free man and GF's pass goes to him.
        // ponytail: PES picks the mate as the first team-mate in his own
        // neighbour list with 10 m of space (MA+0x22e4c is the engine's
        // per-player sorted distance list); this engine's nearest-player rule
        // to the carry point stands in for that list.
        const pes::KeeperConstants& keeperConstants = match->GetPesKeeperConstants();
        // His own goal-area and box marks (PES F5/F6/F7). The keeper of the -x
        // goal attacks +x, so his own half is -x: the marks are magnitudes and
        // `attackDir` carries the sign.
        const int attackDir = -team->GetSide();
        const pes::KeeperFieldMarks marks{pitchHalfW,
                                          goalHalfWidth,
                                          pitchHalfW - SetPieceLaws::kGoalAreaDepth,
                                          SetPieceLaws::kGoalAreaHalfWidth,
                                          pitchHalfW - SetPieceLaws::kAreaDepth,
                                          SetPieceLaws::kAreaHalfWidth};
        const pes::DistributionPoint carry = pes::PesAfterCatchCarry(
            player->GetPosition().coords[0], player->GetPosition().coords[1], attackDir, marks,
            keeperConstants);
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
          const pes::DistributionPoint walk = pes::PesAfterCatchWalk(
              player->GetPosition().coords[0], player->GetPosition().coords[1], carry.x, carry.z,
              targetX, targetZ, attackDir, marks, keeperConstants);
          desiredTargetPosition = Vector3(walk.x, walk.z, 0.0f);
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

    // PES 2021's rush (docs/re/pes-re-goalkeeper.md 10.3, KP_PRESS decider
    // FUN_142115100/1421162c0): he goes for a loose ball when he wins the race
    // by the style factor - the opponent must need more than k times his own
    // frames, and once he is rushing the 0.71 s hysteresis keeps him going. A
    // ball more than 3.3 s away is never chased. This replaces the flat "within
    // 1000 ms and designated" rule, which had no idea who else was going.
    //
    // PES's style bits 19/20 are GF's Offensive/Defensive GK playing styles.
    // Where the style is neither, the no-style factors apply.
    const pes::KeeperConstants& keeperConstants = match->GetPesKeeperConstants();
    const PlayingStyles::Player gkStyle =
        CastPlayer()->GetPlayerData()->GetPlayingStyle();
    const bool offensiveStyle = gkStyle == PlayingStyles::Player::OffensiveGoalkeeper;
    const bool defensiveStyle = gkStyle == PlayingStyles::Player::DefensiveGoalkeeper;
    const float factor = pes::PesKeeperRushFactor(offensiveStyle, defensiveStyle, keeperConstants);
    const bool rushing =
        pes::PesKeeperRushRaceWins(CastPlayer()->GetTimeNeededToGetToBall_ms() * 0.001f,
                                   oppTimeNeededToGetToBall * 0.001f, keeperRushing, factor,
                                   keeperConstants);
    // The lateral limit on the point where he meets the ball: F5 for a
    // Defensive GK, F7 for an Offensive one, the midpoint for no style.
    const pes::KeeperFieldMarks marks{pitchHalfW,
                                      goalHalfWidth,
                                      pitchHalfW - SetPieceLaws::kGoalAreaDepth,
                                      SetPieceLaws::kGoalAreaHalfWidth,
                                      pitchHalfW - SetPieceLaws::kAreaDepth,
                                      SetPieceLaws::kAreaHalfWidth};
    const float lateralLimit_m =
        pes::PesKeeperRushLateralLimit_m(offensiveStyle, defensiveStyle, marks);
    keeperRushing = rushing;
    bool keeperChases = rushing;
    // The meeting point is the ball where his own reach says he gets it; the
    // lateral limit is on that point (10.3's limits on P(kf)).
    const Vector3 meetPoint =
        _mentalImage->GetBallPrediction(CastPlayer()->GetTimeNeededToGetToBall_ms()).Get2D();
    if (std::fabs(meetPoint.coords[1]) > lateralLimit_m) keeperChases = false;
    // And the area rule (10.3): a claim outside his own penalty area is an
    // outfield presser, not a keeper's rush, so he leaves his box only through
    // the outfield branch.
    const float claimDepth = pitchHalfW - std::fabs(meetPoint.coords[0]);
    if (claimDepth > SetPieceLaws::kAreaDepth) keeperChases = false;

    if (keeperChases && match->GetDesignatedPossessionPlayer() == player) {
      GetOnTheBallCommands(commandQueue, rawInputDirection, rawInputVelocityFloat);
    }
  }

  bool forceMagnet = false;


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

    // PES 2021's one ordered reaction table for the engaging defender
    // (docs/re/pes-re-defender-decisions.md 1): walked every tick, the first
    // predicate that returns true wins and replaces the movement command. GF's
    // own trap/interfere/sliding trio each decided for itself, with a duel
    // roll each; they are gone.
    _ReactionCommands(commandQueue);
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
      } else if (const TeamAIController::ConceptOrder* order =
                     team->GetController()->GetConceptOrder(CastPlayer())) {
        // A team concept's order (T+0xb774 + the T+0x26c point, transitions
        // 2.1/3.1): run to the concept's point.
        const Vector3 toTarget = order->target - CastPlayer()->GetPosition();
        inputDirection = toTarget.GetNormalized(inputDirection);
        inputVelocityFloat =
            clamp(toTarget.GetLength() * distanceToVelocityMultiplier, 0.0f, sprintVelocity);

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

    // PES's PASS_GET receiver runs ActionPassGet's approach (BallFollow): to
    // the ball path's nearest point, clamped onto the pitch. GF's magnet
    // approach is for everyone else.
    if (team->PassGetReceiver() == player && !hasPossession)
      _PassGetApproach(commandQueue);
    else
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

// --- PES 2021's engaging-defender decisions (docs/re/pes-re-defender-decisions.md) ---

namespace {

// The angle of a direction vector, in degrees. GF's pitch is (x length, y
// width); PES's is (x length, z width), so this engine's y is PES's z.
inline float HeadingDeg(const Vector3& direction) {
  return std::atan2(direction.coords[1], direction.coords[0]) * 180.0f / blunted::pi;
}

inline float BearingDeg(const Vector3& from, const Vector3& to) {
  return std::atan2(to.coords[1] - from.coords[1], to.coords[0] - from.coords[0]) * 180.0f /
         blunted::pi;
}

inline float AngleBetweenDeg(float a, float b) { return std::fabs(WrapDeg(a - b)); }

// Whether the team is playing one of its deepest mentality rungs, the only GF
// analogue the report leaves open for PES's single `T+0xa298` defensive mode
// byte (report 11 item 5: what that byte counts is unresolved).
// ponytail: one byte stands in for a whole rung, and the mode's +2 is spent on
// the two deepest of GF's five rungs rather than on one exact value.
bool DeepDefensiveMode(const TeamInstructions::State& instructions) {
  return instructions.mentality == TeamInstructions::e_Mentality_AllOutDefence ||
         instructions.mentality == TeamInstructions::e_Mentality_Defensive;
}

}  // namespace

Player* ElizaController::_Carrier() {
  // PES's FUN_140a50e00 (docs/re/pes-re-receiving-port.md): the ball's owner
  // when he is an opponent, nobody when my own team has it, and for a loose
  // ball the other team's pass target - each only while the ball is on him.
  const pes::DefenderDecisionConstants& k = match->GetPesDefenderDecisionConstants();
  Player* owner = match->GetBallRetainer();
  if (!owner) {
    Player* last = match->GetBallEvent(0).player;
    if (last && last->HasPossession()) owner = last;
  }
  Team* other = match->GetTeam(1 - team->GetID());
  Player* theirTarget = other->GetPassTarget();
  const pes::ChallengeOwner who = !owner ? pes::ChallengeOwner::kNobody
                                  : owner->GetTeamID() == team->GetID() ? pes::ChallengeOwner::kMyTeam
                                                                       : pes::ChallengeOwner::kOpponent;
  Player* candidate = nullptr;
  switch (pes::ChallengeCandidate(who, theirTarget && theirTarget->IsActive())) {
    case pes::ChallengeCandidate::kOwner: candidate = owner; break;
    case pes::ChallengeCandidate::kTheirPassTarget: candidate = theirTarget; break;
    case pes::ChallengeCandidate::kNone: return nullptr;
  }
  const Vector3 ball =
      _mentalImage->GetBallPrediction(static_cast<int>(k.challengeLook_s * 1000.0f)).Get2D();
  pes::ChallengeCheck in;
  in.candX = candidate->GetPosition().coords[0];
  in.candY = candidate->GetPosition().coords[1];
  in.candHeading_deg = HeadingDeg(candidate->GetMovement().Get2D().GetNormalized(candidate->GetDirectionVec()));
  in.candSpeed_mps = candidate->GetMovement().Get2D().GetLength();
  in.ballX = ball.coords[0];
  in.ballY = ball.coords[1];
  in.myX = CastPlayer()->GetPosition().coords[0];
  in.myY = CastPlayer()->GetPosition().coords[1];
  // GF has no PRESS movement state; the team's pressure man is its presser.
  in.pressing = team->GetController()->GetTeamPressurePlayer() == player;
  return pes::ChallengeTargetValid(k, in) ? candidate : nullptr;
}

int ElizaController::_TackleAggressionLevel() {
  // PES's A: ENFORCER (+1) and the team's defensive mode (+1). The team-sheet
  // flag that zeroes it (FUN_140a3b520 / FUN_1414dab90) has no GF counterpart.
  int A = 0;
  if (CastPlayer()->GetPlayerData()->GetPlayingStyle() == PlayingStyles::Player::TheDestroyer) ++A;
  if (DeepDefensiveMode(team->GetController()->GetInstructions())) ++A;
  return A;
}

pes::SlideAggressionInput ElizaController::_SlideAggression() {
  const TeamInstructions::State& instructions = team->GetController()->GetInstructions();
  pes::SlideAggressionInput in;
  in.enforcerStyle =
      CastPlayer()->GetPlayerData()->GetPlayingStyle() == PlayingStyles::Player::TheDestroyer;
  // PES's FUN_140a62180(ctx, side, 6) == 0: the Pressuring sheet toggle is
  // Aggressive rather than Conservative (pes-re-tactics.md 2).
  in.aggressiveTeamSheet = team->GetController()->GetTeamSheet().toggles[pes::kPressuring] == 0;
  in.deepDefensiveMode = DeepDefensiveMode(instructions);
  in.staminaUnder30 = CastPlayer()->GetStaminaPct() < 0.30f;
  return in;
}

void ElizaController::_PassGetApproach(PlayerCommandQueue& commandQueue) {
  const pes::ReceiveConstants& k = match->GetPesDefenderDecisionConstants().receive;
  // FUN_140a4d340 searches the prediction one PES frame at a time, 0..0xf9.
  constexpr int kPredictionFrames = 0xf9;
  const float frame_ms = pesball::kPesTick_s * 1000.0f;
  std::vector<pes::Point2> path;
  path.reserve(kPredictionFrames);
  for (int f = 0; f < kPredictionFrames; ++f) {
    const Vector3 b = _mentalImage->GetBallPrediction(static_cast<int>(f * frame_ms));
    path.push_back({b.coords[0], b.coords[1]});
  }
  const Vector3 me = CastPlayer()->GetPosition();
  const pes::Point2 p =
      pes::ReceivingPoint(k, path, {me.coords[0], me.coords[1]}, pitchHalfW, pitchHalfH);
  const Vector3 target(p.x, p.y, 0);
  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Movement;
  command.useDesiredMovement = true;
  command.desiredDirection = (target - me).Get2D().GetNormalized(CastPlayer()->GetDirectionVec());
  // ponytail: the route's speed choice (BallFollow slots 0x58/0x60, not
  // reduced) - GF's distance-to-velocity ramp stands in.
  command.desiredVelocityFloat = RangeVelocity(
      clamp((target - me).GetLength() * distanceToVelocityMultiplier, 0.0f, sprintVelocity));
  command.useDesiredLookAt = true;
  command.desiredLookAt = _mentalImage->GetBallPrediction(0).Get2D();
  commandQueue.push_back(command);
}

bool ElizaController::_AnyTeammateSliding() {
  std::vector<Player*> mates;
  match->GetTeam(team->GetID())->GetActivePlayers(mates);
  for (Player* mate : mates) {
    if (mate == player) continue;
    if (mate->GetCurrentFunctionType() == e_FunctionType_Sliding) return true;
  }
  return false;
}

bool ElizaController::_IsLastLine() {
  // PES reads the analysis table's line mark. The closest readable analogue is
  // "no team mate of mine is deeper than I am": nobody between me and my own
  // goal line, which is what a last man means on the pitch.
  const float ownX = -team->GetSide() * pitchHalfW;
  std::vector<Player*> mates;
  match->GetTeam(team->GetID())->GetActivePlayers(mates);
  for (Player* mate : mates) {
    if (mate == player) continue;
    const float mine = (player->GetPosition().coords[0] - ownX) * team->GetSide();
    const float his = (mate->GetPosition().coords[0] - ownX) * team->GetSide();
    if (his < mine - 0.01f) return false;
  }
  return true;
}

bool ElizaController::_HasCover(float towardsDeg) {
  // PES's FUN_140a4fea0(ctx, id, &pos, 10, 60): a team mate inside the cover
  // radius whose bearing is inside the cover wedge. `towardsDeg` is the bearing
  // of the thing covered - the carrier.
  const pes::DefenderDecisionConstants& k = match->GetPesDefenderDecisionConstants();
  std::vector<Player*> mates;
  match->GetTeam(team->GetID())->GetActivePlayers(mates);
  for (Player* mate : mates) {
    if (mate == player) continue;
    if ((mate->GetPosition() - player->GetPosition()).GetLength() > k.safetyCoverRadius_m) continue;
    if (AngleBetweenDeg(BearingDeg(player->GetPosition(), mate->GetPosition()), towardsDeg) <=
        k.safetyCoverAngle_deg)
      return true;
  }
  return false;
}

void ElizaController::_PushChallenge(PlayerCommandQueue& commandQueue, const Vector3& contact,
                                     e_FunctionType type) {
  PlayerCommand command;
  command.desiredFunctionType = type;
  command.useDesiredMovement = true;
  Vector3 flat = contact - CastPlayer()->GetPosition();
  if (type == e_FunctionType_Sliding) {
    // PES's slide capsule runs from where he stands towards the point, so the
    // body has to be aimed along the way there and not at the endpoint alone.
    command.desiredDirection = flat.Get2D().GetNormalized(CastPlayer()->GetDirectionVec());
  } else {
    command.desiredDirection = flat.GetNormalized(CastPlayer()->GetDirectionVec());
  }
  command.desiredVelocityFloat = sprintVelocity;
  command.useDesiredLookAt = true;
  command.desiredLookAt = CastPlayer()->GetPosition() + command.desiredDirection * 10.0f;
  commandQueue.push_back(command);
}

void ElizaController::_ReactionCommands(PlayerCommandQueue& commandQueue) {
  const pes::DefenderDecisionConstants& k = match->GetPesDefenderDecisionConstants();

  // The walk. PES rebuilds the per-player think context with the current frame
  // every tick, so this runs every tick; GF's RequestCommand is one tick per
  // player already.
  pes::DefenderReactionInput in;
  in.receive = _ReceiveCandidate();
  in.tackle = _TackleCandidate(k);
  in.slide = _SlideCandidate(k);
  in.block = _BlockCandidate(k);
  in.jostleForPass = _PassDuelCandidate(k);
  // Each dodge predicate (entries 4-7) carries its own kind
  // (FUN_14073bc70's cmd+0x1094). They are separate entries, so the chooser's
  // winner names which kind the command runs; each predicate stores its own.
  int dodgeKindBall = 0, dodgeKindNet = 0, dodgeKindPost = 0, dodgeKindHuman = 0;
  in.dodgeBall = _DodgeBallCandidate(&dodgeKindBall);
  in.dodgeNet = _DodgeNetCandidate(&dodgeKindNet);
  in.dodgePost = _DodgePostCandidate(&dodgeKindPost);
  in.dodgeHuman = _DodgeHumanCandidate(&dodgeKindHuman);
  // Where the ball will be when he gets there is where PES's solver puts the
  // contact point, and it is the direction any of these commands runs at.
  const Vector3 contact = _mentalImage->GetBallPrediction(200).Get2D();
  const pes::DefenderReaction chosen = pes::ChooseReaction(in);
  int dodgeKind = 0;
  switch (chosen) {
    case pes::DefenderReaction::kDodgeBall: dodgeKind = dodgeKindBall; break;
    case pes::DefenderReaction::kDodgeNet: dodgeKind = dodgeKindNet; break;
    case pes::DefenderReaction::kDodgePost: dodgeKind = dodgeKindPost; break;
    case pes::DefenderReaction::kDodgeHuman: dodgeKind = dodgeKindHuman; break;
    default: break;
  }
  switch (chosen) {
    case pes::DefenderReaction::kReceive:
      _TrapCommand(commandQueue);
      break;
    case pes::DefenderReaction::kTackle:
      CastPlayer()->SetPesChallenge(pes::ContactAction::kTackle);
      _PushChallenge(commandQueue, contact, e_FunctionType_Interfere);
      break;
    case pes::DefenderReaction::kSlide:
      _PushChallenge(commandQueue, contact, e_FunctionType_Sliding);
      break;
    case pes::DefenderReaction::kBlock:
    case pes::DefenderReaction::kJostleForPass:
      // Both play GF's interpose clips. PES issues a BLOCK anime (0xe) and a
      // JOSTLE anime (0x48) respectively; the player carries which one, so the
      // contact reaction runs PES's BLOCK / JOSTLE rules on him.
      CastPlayer()->SetPesChallenge(chosen == pes::DefenderReaction::kBlock
                                        ? pes::ContactAction::kBlock
                                        : pes::ContactAction::kJostle);
      _PushChallenge(commandQueue, contact, e_FunctionType_Interfere);
      break;
    case pes::DefenderReaction::kDodgeBall:
    case pes::DefenderReaction::kDodgeNet:
    case pes::DefenderReaction::kDodgePost:
    case pes::DefenderReaction::kDodgeHuman:
      _DodgeCommand(commandQueue, chosen, dodgeKind);
      break;
    case pes::DefenderReaction::kNone:
      break;
  }
}

bool ElizaController::_TackleCandidate(const pes::DefenderDecisionConstants& k) {
  Player* carrier = _Carrier();
  if (!carrier || carrier->GetTeamID() == team->GetID()) return false;

  // The anime layer has to be free of a committed challenge, and he may not
  // tackle in the first second after his own touch (PES's event-by-me gate).
  const e_FunctionType current = CastPlayer()->GetCurrentFunctionType();
  if (current == e_FunctionType_Sliding || current == e_FunctionType_Trip) return false;
  if (match->GetLastTouchPlayer() == CastPlayer() &&
      match->GetActualTime_ms() - CastPlayer()->GetLastTouchTime_ms() < 1000)
    return false;

  const Vector3 pos = CastPlayer()->GetPosition();
  const Vector3 carrierPos = carrier->GetPosition();
  const Vector3 ballAhead = _mentalImage->GetBallPrediction(200);
  const Vector3 ballNext = _mentalImage->GetBallPrediction(210);
  const float heading = HeadingDeg(CastPlayer()->GetDirectionVec());
  const float carrierHeading = HeadingDeg(carrier->GetDirectionVec());

  // Step 5's ball gate, on the strict branch. PES applies it to a defender
  // outside {FREE_RUN, WAIT_TIMER, DELAY, PRESS, SAND}; GF has no per-player
  // movement state, so every defender takes the strict branch (which is what
  // the live sample's only tackle was, report 12.1).
  if (!pes::StandingTackleBallGate(k, (ballAhead.Get2D() - pos).GetLength(),
                                   AngleBetweenDeg(heading, BearingDeg(pos, ballAhead.Get2D()))))
    return false;
  // The contact point's height. GF has no solver, so the ball's own height at
  // the look-ahead point stands in (report 12.3: it is a height, not a reach).
  if (ballAhead.coords[2] > k.tackleContactHeight_m) return false;

  const float carrierBearing = BearingDeg(carrierPos, pos);
  if (!pes::StandingTackleFromBehindAllowed(
          k, AngleBetweenDeg(carrierHeading, carrierBearing) > k.tackleBehindAngle_deg,
          AngleBetweenDeg(carrierBearing, carrierHeading),
          AngleBetweenDeg(carrierHeading, heading), _TackleAggressionLevel()))
    return false;
  if (!pes::StandingTackleEngageWindow(k, AngleBetweenDeg(heading, BearingDeg(pos, carrierPos)),
                                       (ballNext - carrierPos).GetLength()))
    return false;

  // The two safety terms. The carrier's running touch clip gives the sine;
  // without one PES's helper returns 1.0, which is above any threshold.
  float sf = 1.0f;
  if (carrier->TouchAnim() && carrier->GetTouchFrame() > 0)
    sf = pes::SafetyProgressSine(static_cast<float>(carrier->GetFrameNum()) /
                                 static_cast<float>(carrier->GetTouchFrame()));
  if (pes::StandingTackleSafetyVeto(k, sf, !_HasCover(BearingDeg(pos, carrierPos)), _IsLastLine(),
                                    static_cast<float>(match->GetActualTime_ms() -
                                                       carrier->GetLastTouchTime_ms()) /
                                        Match::kTick_ms))
    return false;

  // No double tackle: a team mate in a ball action and 1.5x closer to the point
  // (report 2, FUN_140970ad1).
  std::vector<Player*> mates;
  match->GetTeam(team->GetID())->GetActivePlayers(mates);
  const float mine = (ballAhead.Get2D() - pos).GetLength();
  for (Player* mate : mates) {
    if (mate == player) continue;
    if ((ballAhead.Get2D() - mate->GetPosition()).GetLength() * 1.5f > mine) continue;
    const e_FunctionType mateAction = mate->GetCurrentFunctionType();
    if (mateAction == e_FunctionType_Sliding || mateAction == e_FunctionType_Trap ||
        mateAction == e_FunctionType_Interfere || mateAction == e_FunctionType_Header)
      return false;
  }
  return true;
}

bool ElizaController::_SlideCandidate(const pes::DefenderDecisionConstants& k) {
  // CPU team only and open play only: GF is AI-vs-AI by construction, and the
  // whole block sits inside match->IsInPlay() && !match->IsInSetPiece().
  if (_AnyTeammateSliding()) return false;
  if (match->GetBallRetainer()) return false;

  // PES resolves the man he is going for before any geometry runs
  // (FUN_140a50e00), and every gate afterwards is against that player: the
  // slide is a challenge on an opponent, not a loose ball he happens to be
  // near. Without this a defender slides at any ball rolling past him with
  // nobody there.
  Player* target = _Carrier();
  if (!target || target->GetTeamID() == team->GetID()) return false;

  // The contact point, ten frames ahead, must stay below the height limit.
  const Vector3 ballAhead = _mentalImage->GetBallPrediction(100);
  if (ballAhead.coords[2] > k.slideBallHeight_m) return false;

  const bool deepMode = DeepDefensiveMode(team->GetController()->GetInstructions());
  int L = pes::SlideAggressionLevel(_SlideAggression());
  // Inside his own penalty area he may only slide at L = 3, and PES then
  // lowers L. The box is the keeper's box this engine uses everywhere.
  const float ownX = -team->GetSide() * pitchHalfW;
  const bool inOwnBox =
      std::fabs(CastPlayer()->GetPosition().coords[1]) < 20.05f &&
      (CastPlayer()->GetPosition().coords[0] - ownX) * team->GetSide() < 16.4f;
  L = pes::SlideAggressionOwnBox(k, inOwnBox, deepMode, L);
  if (L < 0) return false;

  // ponytail: PES also asks that he be his team's first man to the point, that
  // no opponent body sits in the capsule, and that the slide be lateral enough
  // to his heading. GF has no reach-and-timing solver and no capsule geometry,
  // so the reach below is the whole distance gate.
  const float reach = pes::SlideReach_m(k, L);
  if ((ballAhead.Get2D() - CastPlayer()->GetPosition()).GetLength() >= reach) return false;
  // The direction and engagement windows the raw applies after the reach
  // (FUN_140971ba0 @0x14097230c and @0x140972377): he may not slide sideways,
  // and the point he slides at has to be inside the window off his heading -
  // the same window the standing tackle uses. Without these a defender aimed
  // at a ball 45 deg to his side, or behind him, still slides at it.
  const float heading = HeadingDeg(CastPlayer()->GetDirectionVec());
  const float slideBearing = BearingDeg(CastPlayer()->GetPosition(), ballAhead.Get2D());
  if (!pes::SlideDirectionWindow(k, slideBearing, heading)) return false;
  if (!pes::SlideEngageWindow(k, slideBearing, heading)) return false;
  // Ponytail: the anime layer decides whether he is still recovering from a
  // slide; the engine's own current animation is the nearest readable gate.
  const e_FunctionType current = CastPlayer()->GetCurrentFunctionType();
  return current != e_FunctionType_Sliding && current != e_FunctionType_Trip;
}

bool ElizaController::_BlockCandidate(const pes::DefenderDecisionConstants& k) {
  // Entry 8's owner half: an opponent owner inside the block radius.
  if (match->GetBallRetainer() == 0) return false;
  Player* carrier = _Carrier();
  if (!carrier || carrier->GetTeamID() == team->GetID()) return false;
  if (match->GetBallRetainer() != carrier) return false;
  // PES's PRESS state: the MATCH_UP man while ForeCheckPress runs.
  const bool pressing = team->GetController()->IsPressing(CastPlayer());
  const float radius = pes::BlockRadius_m(k, pressing, false, false);
  if ((carrier->GetPosition() - CastPlayer()->GetPosition()).GetLength() > radius) return false;
  // A pass or a shot clip widens the radius to 7 m once it has passed its
  // fraction. GF's clip table carries no pass clip, so the widened radius is
  // read through the requirement that the ball is already in flight towards
  // him - the late window - rather than through a clip's own progress.
  return true;
}

bool ElizaController::_PassDuelCandidate(const pes::DefenderDecisionConstants& k) {
  // Entry 11's loose half: a kicked ball in flight, unowned, inside the block
  // radius of where it is going. It sits behind entry 8 in the table, so PES
  // reaches it only when the owner's own radius did not catch him.
  if (match->GetBallRetainer()) return false;
  Player* kicker = match->GetLastTouchPlayer();
  if (!kicker) return false;
  const float speed = match->GetBall()->GetMovement().GetLength() * 3.6f;
  const float window = pes::BlockLooseWindow_s(k, speed);
  const float age_s =
      static_cast<float>(match->GetActualTime_ms() - kicker->GetLastTouchTime_ms()) * 0.001f;
  if (age_s >= window) return false;

  const bool pressing = team->GetController()->IsPressing(CastPlayer());
  const float radius = pes::BlockRadius_m(k, pressing, false, false);
  const Vector3 ballPredict = _mentalImage->GetBallPrediction(200);
  return (ballPredict - CastPlayer()->GetPosition()).GetLength() <= radius;
}

// The ball-height band of the ball at a look-ahead time (FUN_1406e69f0), on
// the ball's own predicted position.
static int BallBandAt(Match* match, const pes::DodgeConstants& k, int ms) {
  const float height = match->GetBall()->Predict(ms).coords[2];
  return pes::BallBand(k, height, 1.0f);  // ponytail: body scale is GF's own
}

bool ElizaController::_DodgeBallCandidate(int* kind) {
  // FUN_140a07bf0 (eligibility) + FUN_14073bd80 (the selector). The ball must
  // be unowned or owned by an opponent, close, and the ball-dodge clip must
  // solve the geometry. ponytail: the full eligibility predicate reads PES's
  // play/receive action classes and the last-event frame, which GF does not
  // carry; the selector's own clip fit is the ported gate.
  if (match->GetBallRetainer() == CastPlayer()) return false;
  const Vector3 ballNear = _mentalImage->GetBallPrediction(0);
  const Vector3 ballFar = _mentalImage->GetBallPrediction(200);
  if ((ballNear.Get2D() - CastPlayer()->GetPosition()).GetLength() > 3.0f) return false;
  const pes::DodgeConstants& k = match->GetPesDodgeConstants();
  const int speedClass = pes::SpeedClass(CastPlayer()->GetMovement().GetLength() * 3.6f, 0.0f);
  std::vector<int> forced = {k.ballGrid[0][0], k.ballGrid[0][1], k.ballGrid[0][2]};
  const int bandNear = BallBandAt(match, k, 0);
  const int bandFar = BallBandAt(match, k, 200);
  (void)bandNear;
  (void)bandFar;
  auto clip = pes::BallSelector(match->GetPesAnimetable(), k, speedClass, forced);
  if (!clip) return false;
  *kind = static_cast<int>(pes::DodgeKind::kBall);
  return true;
}

bool ElizaController::_DodgeNetCandidate(int* kind) {
  // FUN_140a07e40 + FUN_14073c5d0. PES's net gate reads the player's own team's
  // post (the own goal line) and then requires the ball's predicted position
  // to fall inside the goal mouth there: past the post edge and between the two
  // posts. GF's stand-in is the same test on its own pitch marks - the own goal
  // line and the goal's half-width.
  //
  // ponytail: the net's full geometry (which goal-line positions admit it) is
  // PES's own post pair; the ported gate is the ball heading into the mouth.
  const Vector3 pos = CastPlayer()->GetPosition();
  // The team's own goal is at +side * pitchHalfW: the set-piece code's
  // defending branch (`team->GetSide() * pitchHalfW * 0.98`) is the same end.
  const float ownGoalX = team->GetSide() * pitchHalfW;
  // Near his own goal line, where PES's post-edge test admits the dodge.
  if (std::fabs(pos.coords[0] - ownGoalX) > 12.0f) return false;
  // The ball's predicted position has to be going into the mouth: past the
  // goal line and between the posts.
  const Vector3 ball = _mentalImage->GetBallPrediction(200).Get2D();
  const float into = (ball.coords[0] - ownGoalX) * -team->GetSide();
  if (into < 0.0f || std::fabs(ball.coords[1]) > goalHalfWidth) return false;
  const pes::DodgeConstants& k = match->GetPesDodgeConstants();
  const float speed = CastPlayer()->GetMovement().GetLength() * 3.6f;
  auto clip = pes::NetSelector(match->GetPesAnimetable(), k, speed > 22.0f);
  if (!clip) return false;
  *kind = static_cast<int>(pes::DodgeKind::kNet);
  return true;
}

bool ElizaController::_DodgePostCandidate(int* kind) {
  // FUN_140a08340 + FUN_14073c9c0. PES's post predicate reads the player's OWN
  // team's post (the one at the goal line he defends) and its eligibility gate
  // (FUN_140a084d0) checks that post sits at the field edge within 1.0 m in
  // both axes. GF carries no per-post object, so the port's anchor is the own
  // goal line's post: x = -side * pitchHalfW and the goal's half-width, both
  // gametypes.hpp's own marks. Which of the two posts it is comes from the
  // ball's side, as PES's +0x55c x test picks the one on the ball's half.
  //
  // ponytail: PES's own-post geometry (the 20-unit out step toward the touch
  // line, FUN_140a8f7c0) is the part not carried; the ported gate is the own
  // goal band and the selector's clip fit.
  const Vector3 pos = CastPlayer()->GetPosition();
  // The team's own goal is at +side * pitchHalfW (the set-piece code's
  // defending branch is the same end), and the goal line's outward normal is
  // that direction, so the selector's reference is 180 there.
  const float ownGoalX = team->GetSide() * pitchHalfW;
  // Only near his own goal line: the post's own neighbourhood.
  const float distToOwnGoal = std::fabs(pos.coords[0] - ownGoalX);
  if (distToOwnGoal > 12.0f) return false;
  const Vector3 ball = _mentalImage->GetBallPrediction(200).Get2D();
  const int postSide = ball.coords[1] >= 0.0f ? 1 : -1;
  const Vector3 post(ownGoalX, goalHalfWidth * postSide, 0.0f);
  // The two angles the exe's selector reads: the bearing from the post's
  // anchor to the player (raw, 0..360) and the approach angle relative to the
  // goal line's own facing. The goal line's normal points out of the pitch, so
  // the reference is 0 at one end and 180 at the other.
  float bearing = BearingDeg(post, Vector3(pos.coords[0], pos.coords[1], 0.0f));
  if (bearing < 0.0f) bearing += 360.0f;  // the exe's raw 0..360 bearing
  const float refAngle = ownGoalX >= 0.0f ? 180.0f : 0.0f;
  const float approach = WrapDeg(bearing - refAngle);
  const pes::DodgeConstants& k = match->GetPesDodgeConstants();
  const float speed = CastPlayer()->GetMovement().GetLength() * 3.6f;
  auto clip = pes::PostSelector(match->GetPesAnimetable(), k, bearing, refAngle, approach, speed);
  if (!clip) return false;
  *kind = static_cast<int>(pes::DodgeKind::kPost);
  return true;
}

bool ElizaController::_DodgeHumanCandidate(int* kind) {
  // FUN_140a08ea0 + FUN_14073c520. An opponent within 3 m whose dodge kind
  // resolves to a clip. The mirror side is the per-player draw (stream 2).
  const pes::DodgeConstants& k = match->GetPesDodgeConstants();
  Player* best = nullptr;
  float bestDistSq = k.humanDistSq;
  std::vector<Player*> opps;
  match->GetTeam(1 - team->GetID())->GetActivePlayers(opps);
  for (Player* opp : opps) {
    const Vector3 delta = opp->GetPosition() - CastPlayer()->GetPosition();
    const float d = delta.coords[0] * delta.coords[0] + delta.coords[1] * delta.coords[1];
    if (d < bestDistSq) bestDistSq = d, best = opp;
  }
  if (!best) return false;
  const int humanKind = _HumanDodgeKind(best);
  if (humanKind < 0 || humanKind > 7) return false;
  const int row = pes::KindRow(match->GetPesAnimetable(), match->GetPesMbinfo(), k, humanKind,
                               CastPlayer()->GetMovement().GetLength(), false, 0.0f);
  if (row < 0) return false;
  *kind = humanKind;
  return true;
}

int ElizaController::_HumanDodgeKind(Player* opp) {
  // FUN_14073a3d0. ponytail: the full kind inference reads PES's contact
  // record and the opponent's action classes, which GF does not carry per
  // part. The ported part is the geometry: an opponent closing from behind
  // (kinds 4/5) versus in front. The default kind is the jump family.
  const Vector3 toOpp = opp->GetPosition() - CastPlayer()->GetPosition();
  const float bearing = BearingDeg(CastPlayer()->GetPosition(), opp->GetPosition());
  const float heading = HeadingDeg(CastPlayer()->GetDirectionVec());
  const float off = std::fabs(AngleBetweenDeg(bearing, heading));
  const pes::DodgeConstants& k = match->GetPesDodgeConstants();
  if (off > k.behindAngle_deg) return 4;  // approaching from behind: slide_rapid_up
  if (off < k.bearingAngle_deg) return 1;  // in front: jump
  return 3;                                 // beside: slide
}

void ElizaController::_DodgeCommand(PlayerCommandQueue& commandQueue,
                                    pes::DefenderReaction reaction, int kind) {
  // FUN_14073bc70 + FUN_1406ecbb0: the Special command names the clip by
  // (family, motion). The player also carries the DODGE action so the contact
  // reaction runs PES's dodge rules on him (V5, the committed flag).
  CastPlayer()->SetPesChallenge(pes::ContactAction::kDodge);
  const pes::DodgeConstants& k = match->GetPesDodgeConstants();
  const pes::Animetable& t = match->GetPesAnimetable();
  int motionNo = -1;
  switch (reaction) {
    case pes::DefenderReaction::kDodgeBall: {
      const int speedClass = pes::SpeedClass(CastPlayer()->GetMovement().GetLength() * 3.6f, 0.0f);
      std::vector<int> forced = {k.ballGrid[0][0], k.ballGrid[0][1], k.ballGrid[0][2]};
      auto clip = pes::BallSelector(t, k, speedClass, forced);
      if (clip) motionNo = clip->motionNo;
      break;
    }
    case pes::DefenderReaction::kDodgeNet: {
      const float speed = CastPlayer()->GetMovement().GetLength() * 3.6f;
      auto clip = pes::NetSelector(t, k, speed > 22.0f);
      if (clip) motionNo = clip->motionNo;
      break;
    }
    case pes::DefenderReaction::kDodgePost: {
      // The same own-post geometry the predicate used (FUN_14073c9c0's
      // callsite, so the command's clip and the predicate's agree).
      const Vector3 pos = CastPlayer()->GetPosition();
      const float ownGoalX = team->GetSide() * pitchHalfW;
      const Vector3 ball = _mentalImage->GetBallPrediction(200).Get2D();
      const int postSide = ball.coords[1] >= 0.0f ? 1 : -1;
      const Vector3 post(ownGoalX, goalHalfWidth * postSide, 0.0f);
      float bearing = BearingDeg(post, Vector3(pos.coords[0], pos.coords[1], 0.0f));
      if (bearing < 0.0f) bearing += 360.0f;
      const float refAngle = ownGoalX >= 0.0f ? 180.0f : 0.0f;
      const float approach = WrapDeg(bearing - refAngle);
      const float speed = CastPlayer()->GetMovement().GetLength() * 3.6f;
      auto clip = pes::PostSelector(t, k, bearing, refAngle, approach, speed);
      if (clip) motionNo = clip->motionNo;
      break;
    }
    case pes::DefenderReaction::kDodgeHuman: {
      const int row = pes::KindRow(t, match->GetPesMbinfo(), k, kind,
                                   CastPlayer()->GetMovement().GetLength(), false, 0.0f);
      if (row >= 0 && t.Populated(row, pes::KindColumn(k, kind)))
        motionNo = t.Cell(row, pes::KindColumn(k, kind)).motionNo;
      break;
    }
    default:
      break;
  }
  if (motionNo < 0) return;  // the selector found no clip: PES declines too
  PlayerCommand command;
  command.desiredFunctionType = e_FunctionType_Special;
  command.useSpecialVar1 = true;
  command.specialVar1 = static_cast<int>(pes::PesClipFamily::kDodge);
  command.useSpecialVar2 = true;
  command.specialVar2 = motionNo;
  command.useDesiredMovement = true;
  command.desiredDirection = CastPlayer()->GetDirectionVec();
  command.desiredVelocityFloat = CastPlayer()->GetMovement().GetLength();
  commandQueue.push_back(command);
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
                                                       const Vector3& basePosition) {
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
  float flockToPossessionPlayerWeight = 0.45f * overallWeight;

  // Support Range 1..10 as the 0..1 share PES itself uses, (SR - 1) / 9
  // (tactics 4.1). ponytail: it scales GF's support web; PES's own support
  // distance formula belongs to the off-ball port.
  constexpr float kSupportRangeSteps = 9.0f;  // 0x14259d60c
  const float webScale = AITactics::GetSupportWebScale(
      (team->GetController()->GetTeamSheet().supportRange - 1) / kSupportRangeSteps);
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

  // actual base position
  {
    ForceSpot spot;
    spot.origin = basePosition;

    // lane version: flanks flow forward toward the ball's far lane. PES's run
    // and designated supporter (pesoffball.hpp) bypass this field entirely.
    {
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
  keeperRushing = false;
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
    // The receiver offside prediction (report 4.3's status 6): every input to
    // the carrier's offside line, and the receiver's own position, is read
    // int(gameSpeed x 0.3) = 16 frames ahead.
    constexpr float kReceiverFrames = 16.0f;
    const float receiverPrediction_s =
        kReceiverFrames * Match::kTick_ms * 0.001f / pes::kGfTicksPerPesTick;
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
      // PES 2021's receiver enumeration (docs/re/pes-re-carrier-ai.md 4.3):
      // a team-mate whose position now or his position the receiver
      // prediction ahead is beyond the carrier's offside line gets status 6
      // - offside - and is never a pass receiver.
      if (AI_ReceiverIsOffside(match, _mentalImage, team->GetID(), matePos,
                               mate->GetMovement(), ballPos, ballMove,
                               receiverPrediction_s))
        continue;
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

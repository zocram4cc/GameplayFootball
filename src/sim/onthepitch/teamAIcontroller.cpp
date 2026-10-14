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

#include "teamAIcontroller.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "../data/playerskills.hpp"
#include "../data/playingstyles.hpp"
#include "pessetpieceteam.hpp"
#include "setpiecelaws.hpp"
#include "simcontext.hpp"
#include "../misc/hungarian.h"
#include "AIsupport/AIfunctions.hpp"
#include "aimanager.hpp"
#include "aitactics.hpp"
#include "match.hpp"
#include "team.hpp"

bool ReverseSortTacticalOpponentInfo(const TacticalOpponentInfo& a, const TacticalOpponentInfo& b) {
  return a.dangerFactor > b.dangerFactor;
}

TeamAIController::TeamAIController(Team* team) : team(team) {
  match = team->GetMatch();
  taker = 0;

  // Whatever the game plan set before kick-off. These used to start at their
  // defaults every match, so the only way to reach them was from the touchline once
  // play had begun - no use at all for a side nobody is shouting at.
  instructions = TeamInstructions::Load(team->GetTeamData()->GetTactics().userProperties);

  depth = 0.45f;
  width = 0.95f;

  setPieceType = e_SetPiece_None;

  offsideTrapX = 0;

  defensiveShapeField = pes::ShapeFieldFromPitch();
  defensiveLineHeight = 0.0f;
  defensiveTeamLength = 0.0f;
  previousLineHeight = 0.0f;
  defensiveStyle = 0;
  defensiveAttackLevel = 2;
  defensiveLineSlider = 4.0f;
  defensiveCompactSlider = 4.0f;

  endApplyAttackingRun_ms = 0;
  attackingRunPlayer = 0;
  endApplyTeamPressure_ms = 0;
  teamPressurePlayer = 0;
  endApplyKeeperRush_ms = 0;
  forwardSupportPlayer = 0;

  zonePressureActive = false;
  zonePressureEndTime_ms = 0;
  nextZonePressureRefresh_ms = 0;
  prevTeamHasPossession = false;

  baseTeamTactics.Set("position_offense_depth_factor", 0.9f);
  baseTeamTactics.Set("position_defense_depth_factor", 0.75f);
  baseTeamTactics.Set("position_offense_width_factor", 0.9f);
  baseTeamTactics.Set("position_defense_width_factor", 0.8f);
  baseTeamTactics.Set("position_offense_ownhalf_factor", 0.52f);
  baseTeamTactics.Set("position_defense_ownhalf_factor", 0.54f);
  baseTeamTactics.Set("position_offense_midfieldfocus", 0.6f);
  baseTeamTactics.Set("position_defense_midfieldfocus", 0.5f);
  baseTeamTactics.Set("position_offense_midfieldfocus_strength", 0.35f);
  baseTeamTactics.Set("position_defense_midfieldfocus_strength", 0.35f);
  baseTeamTactics.Set("position_offense_sidefocus_strength",
                      0.1f);  // take possession factor more seriously (high) or ball position (low)
  baseTeamTactics.Set("position_defense_sidefocus_strength", 0.4f);
  baseTeamTactics.Set("position_offense_microfocus_strength", 0.7f);
  baseTeamTactics.Set("position_defense_microfocus_strength", 0.8f);

  // The non-positional sliders travel through the same pipeline. Their neutral
  // base is 0.5; the philosophy preset moves it, the user's slider offsets it.
  baseTeamTactics.Set("team_pressure", 0.5f);
  baseTeamTactics.Set("counter_attack", 0.5f);
  baseTeamTactics.Set("support_distance", 0.5f);
  baseTeamTactics.Set("dribble_offensiveness", 0.5f);
  baseTeamTactics.Set("dribble_centermagnet", 0.5f);
  // PES 2021's offside-trap instruction (report 14.4's `T+0xe4`): it arms the
  // trap only while a manager has asked for it, and both live teams ran with it
  // off. GF has no touchline button for it yet, so the base is PES's value and a
  // team's own tactics data can still set it.
  baseTeamTactics.Set("offside_trap", 0.0f);

  // how much [-1 * this value .. +1 * this value] offset we can add to the above tactics.
  teamTacticsModMultipliers.Set("position_offense_depth_factor", 0.1f);
  teamTacticsModMultipliers.Set("position_defense_depth_factor", 0.1f);
  teamTacticsModMultipliers.Set("position_offense_width_factor", 0.1f);
  teamTacticsModMultipliers.Set("position_defense_width_factor", 0.1f);
  teamTacticsModMultipliers.Set("position_offense_midfieldfocus", 0.3f);
  teamTacticsModMultipliers.Set("position_defense_midfieldfocus", 0.3f);
  teamTacticsModMultipliers.Set("position_offense_sidefocus_strength", 0.1f);
  teamTacticsModMultipliers.Set("position_defense_sidefocus_strength", 0.1f);
  teamTacticsModMultipliers.Set("position_offense_microfocus_strength", 0.15f);
  teamTacticsModMultipliers.Set("position_defense_microfocus_strength", 0.15f);
  // These four used to be missing, so their user sliders were multiplied by 0.
  teamTacticsModMultipliers.Set("position_offense_ownhalf_factor", 0.1f);
  teamTacticsModMultipliers.Set("position_defense_ownhalf_factor", 0.1f);
  teamTacticsModMultipliers.Set("position_offense_midfieldfocus_strength", 0.15f);
  teamTacticsModMultipliers.Set("position_defense_midfieldfocus_strength", 0.15f);
  // At 0.5 the offset formula (user - 0.5) * 2 * multiplier makes the user's
  // slider land exactly as-is on the neutral 0.5 base, which is how these five
  // were read before they travelled through the pipeline at all.
  teamTacticsModMultipliers.Set("team_pressure", 0.5f);
  teamTacticsModMultipliers.Set("counter_attack", 0.5f);
  teamTacticsModMultipliers.Set("support_distance", 0.5f);
  teamTacticsModMultipliers.Set("dribble_offensiveness", 0.5f);
  teamTacticsModMultipliers.Set("dribble_centermagnet", 0.5f);
  // The offside-trap instruction is a flag, not a slider: its whole [0, 1] range
  // travels through so a team's tactics data can set it either way.
  teamTacticsModMultipliers.Set("offside_trap", 1.0f);

  offensivenessBias = 0.5f;
  philosophy = TeamPhilosophy::e_Philosophy_Balanced;
  // The shape the team is actually lined up in, read off the lineup. This used
  // to come from a "formation" string in the tactics properties, but tactics_xml
  // is parsed with atof, so that property can only ever hold a number - every
  // team came out a 4-4-2 however it was really set up, and a side loaded as a
  // 5-4-1 got reshaped by an AI that thought it was a flat four.
  formationShape = TeamFormationShape();
  Log(e_Notice, "TeamAIController", "TeamAIController",
      "team " + int_to_str(team->GetID()) + " lined up as " +
          Formations::ShapeName(formationShape));
  mentality = MatchMentality::e_Mentality_Normal;

  UpdateTactics();
}

TeamAIController::~TeamAIController() {
  if (Verbose())
    printf("exiting teamcontroller.. ");
  if (Verbose())
    printf("done\n");
}

Player* SelectAttackingRunPlayer(Team* team) {
  Player* possessionPlayer = team->GetDesignatedTeamPossessionPlayer();

  Vector3 offenseFocusPos =
      possessionPlayer->GetPosition() + Vector3(-team->GetSide() * 26.0f, 0, 0);

  // The nearest AI teammate to the focus, except that a hole player or a
  // full-back finisher volunteers from further away and an anchor man never
  // leaves his post (PES Playing Styles).
  Player* attackingRunPlayer = nullptr;
  float bestCost = 10000.0f;
  for (const std::unique_ptr<Player>& candidate : team->GetAllPlayers()) {
    if (!candidate->IsActive() || candidate.get() == possessionPlayer)
      continue;
    const float affinity = PlayingStyles::GetAttackingRunAffinity(
        candidate->GetPlayerData()->GetPlayingStyle(), candidate->GetPlayerData()->GetComStyles());
    if (affinity <= 0.0f)
      continue;
    const float cost =
        (candidate->GetPosition() - offenseFocusPos).GetLength() * (1.5f - affinity);
    if (cost < bestCost) {
      bestCost = cost;
      attackingRunPlayer = candidate.get();
    }
  }
  return attackingRunPlayer;
}

void TeamAIController::Process() {
  if (match->GetActualTime_ms() % 1000 == 0)
    UpdateTactics();

  CalculateSituation();

  float startDistance =
      30.0f + 20.0f * offensivenessBias;  // distance from goal where we start holding the opponents
                                          // (but are likely to fallback somewhat)
  float forceDistance = 6.0f;  // minimum distance from goal - try to 'hold' the opp there

  float deepestDanger = (pitchHalfW - startDistance) * team->GetSide();

  // ball as max
  float adaptedBallX = match->GetBall()->Predict(0).coords[0];
  // when far away from our goal (startDistance), drop back more easily. closer to forceDistance,
  // don't buckle.
  adaptedBallX *= team->GetSide();  // > 0 == on our half (easier to work with)
  float offsetX =
      20.0f +
      10.0f * (1.0f - offensivenessBias);  // when ball is this distance away from startDistance, we
                                           // start falling back more towards forceDistance.
  float startToForcedBias = NormalizedClamp(
      adaptedBallX, pitchHalfW - startDistance - offsetX,
      pitchHalfW -
          forceDistance);  // where, between startDistance and forceDistance, is the ball? 0 .. 1
  adaptedBallX +=
      offsetX * (1.0f - startToForcedBias);  // the fall back intensity gradually diminishes when
                                             // getting closer to forceDistance
  adaptedBallX *= team->GetSide();           // back to absolute space
  if (adaptedBallX * team->GetSide() > deepestDanger * team->GetSide())
    deepestDanger = adaptedBallX;

  // ballfuture as max
  if (match->GetBall()->Predict(700).coords[0] * team->GetSide() > deepestDanger * team->GetSide())
    deepestDanger = match->GetBall()->Predict(700).coords[0];

  // opp as max
  Player* opp = match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer();
  float cautionDistance = 4.0f * team->GetSide();
  if ((opp->GetPosition().coords[0] + opp->GetMovement().coords[0] * 0.15f + cautionDistance) *
          team->GetSide() >
      deepestDanger * team->GetSide())
    deepestDanger =
        opp->GetPosition().coords[0] + opp->GetMovement().coords[0] * 0.1f + cautionDistance;

  // slacking teammate as max
  float lineX = AI_GetOffsideLine(match, match->GetMentalImage(0), abs(team->GetID()));
  float allowSlackDistance =
      4.0f;  // despite teammates slacking behind line this much, just hold the line
  if (lineX * team->GetSide() - allowSlackDistance > deepestDanger * team->GetSide()) {
    // printf("previous: %f\n", deepestDanger);
    deepestDanger = lineX - allowSlackDistance * team->GetSide();
    // printf("now:      %f\n", deepestDanger);
  }

  /*
  if (team->GetID() == 0) SetRedDebugPilon(Vector3(deepestDanger, 0, 0));
  if (team->GetID() == 1) SetYellowDebugPilon(Vector3(deepestDanger, 0, 0));
  */

  offsideTrapX = deepestDanger;
  // A side parking the bus never holds a line beyond its own box.
  offsideTrapX = TeamPhilosophy::AdaptOffsideTrapX(philosophy, offsideTrapX, team->GetSide());
  // disable
  // offsideTrapX = pitchHalfW * team->GetSide();

  CalculateDefensiveShape();

  // calculate who's dangerous

  std::vector<Player*> players;
  match->GetActiveTeamPlayers(abs(team->GetID() - 1), players);

  Vector3 mostDangerousPos = Vector3((pitchHalfW - 2.0) * team->GetSide(), 0, 0);
  mostDangerousPos = mostDangerousPos * 0.8f + match->GetBall()->Predict(100).Get2D() * 0.2f;

  tacticalOpponentInfo.clear();

  for (auto* player : players) {
    TacticalOpponentInfo info;
    info.player = player;

    info.dangerFactor =
        1.0 -
        NormalizedClamp((player->GetPosition() - mostDangerousPos).GetLength(), 0, pitchHalfW * 2);

    // player on ball is most dangerous
    info.dangerFactor *= 0.95f;
    if (player == match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer())
      info.dangerFactor += 0.05f;

    tacticalOpponentInfo.push_back(info);
  }

  // sort list
  std::sort(tacticalOpponentInfo.begin(), tacticalOpponentInfo.end(),
            ReverseSortTacticalOpponentInfo);

  // team pressure

  /* DISABLED, interferes with other defense AI code for now
    if (team->GetHumanGamerCount() == 0) {
      if (match->GetBestPossessionTeamID() != team->GetID()) {

        bool opponentFreeRun = false;
        Player *opp = match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer();
        Vector3 dangerPos = Vector3(pitchHalfW * team->GetSide(), opp->GetPosition().coords[1] *
    0.5, 0); float oppDangerDistance = (opp->GetPosition() - dangerPos).GetLength(); Vector3
    adaptedGoalPos = Vector3(pitchHalfW * team->GetSide(), 0, 0) * 0.5 + opp->GetPosition() * 0.5;
        float oppGoalDistance = (opp->GetPosition() - adaptedGoalPos).GetLength();
        Player *p = AI_GetClosestPlayer(team, adaptedGoalPos);
        float usGoalDistance = (p->GetPosition() - adaptedGoalPos).GetLength();
        if (usGoalDistance > oppGoalDistance - 3.2) opponentFreeRun = true;

        bool closeEnemy = false;
        if (oppDangerDistance < 20) closeEnemy = true;

        if (opponentFreeRun || closeEnemy) {
          ApplyTeamPressure();
        }
      }
    }
  */

  // PES-style zone pressure: after a suitable turnover, the nearest defender
  // engages normally and one additional defender closes down for a short,
  // tactic-dependent period.
  if (prevTeamHasPossession && !teamHasPossession) {
    Player* primaryDefender = team->GetDesignatedTeamPossessionPlayer();
    const Vector3 ballPos = match->GetBall()->Predict(0).Get2D();
    const float primaryDistance =
        primaryDefender ? (primaryDefender->GetPosition() - ballPos).GetLength() : 1000.0f;
    const float pressureSetting =
        liveTeamTactics.GetReal("team_pressure", 0.5f);
    const float territory =
        AITactics::GetAttackingTerritory(ballPos.coords[0], team->GetSide(), pitchHalfW);
    // Gegenpressing counter-presses on the loss itself; ApplyTeamPressure
    // then also keeps hunting longer via GetTeamPressureDurationBonus_ms.
    if (AITactics::ShouldStartZonePressureOnLoss(
            TeamPhilosophy::PressesOnPossessionLoss(philosophy), pressureSetting, territory,
            primaryDistance))
      ApplyZonePressure();
  }

  if (zonePressureActive) {
    if (teamHasPossession || match->GetActualTime_ms() > zonePressureEndTime_ms) {
      zonePressureActive = false;
      endApplyTeamPressure_ms = match->GetActualTime_ms();
      teamPressurePlayer = 0;
    } else if (match->GetActualTime_ms() >= nextZonePressureRefresh_ms) {
      ApplyTeamPressure();
      nextZonePressureRefresh_ms = match->GetActualTime_ms() + 250;
    }
  }
  // Counterattack: the moment the ball is won with the opponent committed
  // upfield, spring a runner at once instead of waiting for the periodic check.
  if (!prevTeamHasPossession && teamHasPossession) {
    const Vector3 ballPos = match->GetBall()->Predict(0).Get2D();
    const float territory =
        AITactics::GetAttackingTerritory(ballPos.coords[0], team->GetSide(), pitchHalfW);

    int opponentsInOwnHalf = 0;
    std::vector<Player*> opponents;
    match->GetActiveTeamPlayers(abs(team->GetID() - 1), opponents);
    for (Player* opponent : opponents) {
      if (opponent->GetPosition().coords[0] * team->GetSide() > 0.0f)
        opponentsInOwnHalf++;
    }

    const float counterSetting =
        liveTeamTactics.GetReal("counter_attack", 0.5f);
    if (AITactics::ShouldLaunchCounter(counterSetting, opponentsInOwnHalf, territory,
                                       random(0.0f, 1.0f))) {
      ApplyAttackingRun();
      endApplyAttackingRun_ms =
          match->GetActualTime_ms() + AITactics::GetCounterWindow_ms(counterSetting);
      if (Verbose())
        printf("!!! counterattack sprung: %i opponents caught upfield !!!\n", opponentsInOwnHalf);
    }
  }

  prevTeamHasPossession = teamHasPossession;

  // trigger attacking runs

  if (match->GetActualTime_ms() % 500 == 0 &&
      endApplyAttackingRun_ms <= match->GetActualTime_ms()) {
    if (match->GetBestPossessionTeamID() == team->GetID()) {
      const float counterAttack =
          liveTeamTactics.GetReal("counter_attack", 0.5f);
      const float neededRating = AITactics::GetAttackingRunThreshold(counterAttack);

      // from a certain distance, running is not very useful (can't pass that far)
      Player* runner = SelectAttackingRunPlayer(team);
      if (runner) {
        // -team->GetSide() is the direction this team attacks (see
        // SelectAttackingRunPlayer's own -side * 26 m target). The probe was
        // sampling ten metres toward the team's OWN goal, so the defenders it
        // counted were the ones behind the runner and the ones he was about
        // to run into were invisible: the density term rewarded running at a
        // crowded area and punished an open one, exactly backwards.
        Player* carrier = team->GetDesignatedTeamPossessionPlayer();
        const float distance =
            carrier ? (runner->GetPosition() - carrier->GetPosition()).GetLength() : 40.0f;
        float distanceRating = std::pow(1.0f - NormalizedClamp(distance, 0, 40), 0.5f);

        // more likely to run when there's less defenders in front
        std::vector<Player*> opponents;
        Vector3 spot = runner->GetPosition() * Vector3(1.0f, 0.8f, 0.0f) +
                       Vector3(-team->GetSide() * 10.0f, 0, 0);
        AI_GetClosestPlayers(team->GetMatch()->GetTeam(abs(team->GetID() - 1)), spot,
                             opponents, 4);
        float oppDensityRating = 1.0f;
        for (unsigned int i = 0; i < opponents.size(); i++) {
          float oppDistance = (opponents.at(i)->GetPosition() - spot).GetLength();
          float oppDistanceRatingInv =
              std::pow(curve(1.0f - NormalizedClamp(oppDistance, 0, 15), 1.0f), 0.5f);
          oppDensityRating -= oppDistanceRatingInv * 0.3f;  // subtractive!
        }

        float runConditionsRating = distanceRating * oppDensityRating;

        if (runConditionsRating >= neededRating) {
          ApplyAttackingRun();
          if (Verbose())
            printf("!!! tactics induced run !!!\n");
        }
      }
    }
  }

  if (match->GetActualTime_ms() % 1500 == 0) {
    const float counterAttack =
        liveTeamTactics.GetReal("counter_attack", 0.5f);
    const float supportLead = 0.5f + AITactics::ClampSetting(counterAttack) * 2.0f;
    forwardSupportPlayer = AI_GetClosestPlayer(
        team,
        team->GetDesignatedTeamPossessionPlayer()->GetPosition() * Vector3(1.0f, 1.0f, 0.0f) +
            Vector3(-team->GetSide() * supportLead, 0, 0), team->GetDesignatedTeamPossessionPlayer());
  }
}

float mixup(float base, const std::string& varname, e_PlayerRole role, float counterAttack) {
  float value = -100.0f;

  if (role == e_PlayerRole_CB) {
    if (varname == "position_offense_width_factor")
      value = 0.2f;  // wider defense
  }

  if (role == e_PlayerRole_LB || role == e_PlayerRole_RB) {
    if (varname == "position_defense_ownhalf_factor")
      value = -0.075f;  // go forward
    if (varname == "position_offense_width_factor")
      value = 0.2f;  // wider defense
    if (varname == "position_offense_ownhalf_factor")
      value = -0.1f;  // go forward
  }

  if (role == e_PlayerRole_LM || role == e_PlayerRole_RM || role == e_PlayerRole_LW ||
      role == e_PlayerRole_RW) {
    // wingers stay high up to offer counter-attack support
    if (varname == "position_defense_ownhalf_factor")
      value = -(0.02f + 0.06f * counterAttack);
    if (varname == "position_offense_ownhalf_factor")
      value = -0.1f;  // go forward
  }

  if (role == e_PlayerRole_AM) {
    // attackers stay high up to offer counter-attack support
    if (varname == "position_defense_depth_factor")
      value = 0.05f + 0.15f * counterAttack;
  }

  if (role == e_PlayerRole_CF || role == e_PlayerRole_SS) {
    // strikers stay high up to offer counter-attack support
    if (varname == "position_defense_depth_factor")
      value = 0.05f + 0.15f * counterAttack;
  }

  if (value > -100.0f) {
    // bias version
    // float rolebias = 0.5f;
    // return clamp(value * rolebias + base * (1.0f - rolebias), 0.0f, 1.0f);

    // offset version
    return clamp(base + value, 0.0f, 1.0f);

  } else {
    return base;
  }
}

// PES's slot-lines (report 7's `ctx+0x128[slot]`): role class 3-6 is the
// midfield line, 7-9 the forward line, everything else the back line. GF's own
// roles fall into the same three groups.
namespace {

int PesLineIndexForRole(e_PlayerRole role) {
  switch (role) {
    case e_PlayerRole_DM:
    case e_PlayerRole_CM:
    case e_PlayerRole_LM:
    case e_PlayerRole_RM:
    case e_PlayerRole_AM:
      return 1;
    case e_PlayerRole_CF:
    case e_PlayerRole_SS:
    case e_PlayerRole_LW:
    case e_PlayerRole_RW:
      return 2;
    default:
      return 0;  // GK, CB, LB, RB
  }
}

}  // namespace

void TeamAIController::CalculateDefensiveShape() {
  // FUN_1405f9eb0 with FUN_1405f7a00's cushion and FUN_1405ef910's step-up
  // (report 6, 14.1), then FUN_1405efbd0's midfield catch-up (report 14.1). No
  // smoothing: the target is rebuilt from this tick's ball.
  const pes::DefensiveShapeConstants& k = match->GetPesDefensiveShapeConstants();
  const pes::ShapeField& field = defensiveShapeField;
  // Own frame: +x toward the opponentsents' goal, own goal at -halfLength.
  const float dir = static_cast<float>(-team->GetSide());
  const Vector3 ball = match->GetBall()->Predict(0);
  const Vector3 ballVel = match->GetBall()->GetMovement();

  pes::LineFacts facts;
  facts.ballDepth_m = ball.coords[0] * dir;
  const Vector3 backToOwnGoal(-1.0f, 0.0f, 0.0f);
  const Vector3 ballVel2D(ballVel.coords[0] * dir, ballVel.coords[1], 0.0f);
  const float ballSpeed = ballVel2D.GetLength();
  facts.ballSpeed_mps = ballSpeed;
  facts.ballHeadingFromGoal_deg =
      ballSpeed > 0.01f ? std::acos(std::clamp(backToOwnGoal.GetDotProduct(ballVel2D) / ballSpeed, -1.0f, 1.0f)) *
                             180.0f / pi
                        : 180.0f;

  Player* carrier = team->GetDesignatedTeamPossessionPlayer();
  const bool hasCarrier = carrier != nullptr;
  // The cushion and the step-up both key off the opponent on the ball; when our
  // own man carries it, PES's cushion collapses entirely.
  const bool carrierIsOpponent = !teamHasPossession;
  facts.carrierDepth_m = hasCarrier ? carrier->GetPosition().coords[0] * dir : 0.0f;
  if (hasCarrier) {
    const Vector3 facing = carrier->GetDirectionVec();
    const Vector3 facing2D(facing.coords[0] * dir, facing.coords[1], 0.0f);
    const float facingLength = facing2D.GetLength();
    facts.carrierFacingFromGoal_deg =
        facingLength > 0.01f ? std::acos(std::clamp(backToOwnGoal.GetDotProduct(facing2D) / facingLength,
                                                    -1.0f, 1.0f)) *
                                  180.0f / pi
                             : 0.0f;
  }

  // ponytail: PES's attack level `T+0xb8a4` has no GF counterpart; PES's live
  // teams both sat on level 2, which is what the line ramp uses.
  const int attackLevel = 2;
  // ponytail: PES's Defensive Style (team-sheet option byte 4) has no GF
  // counterpart either; PES's live team 0 ran style 0, the forecheck branch.
  const int style = 0;
  // PES's Defensive Line runs 1..10 with 10 the deepest; GF's defensive depth
  // factor is a 0..1 slider over the same direction, so it maps straight on.
  const float lineSlider =
      1.0f + 9.0f * AITactics::ClampSetting(GetLiveTacticReal("position_defense_depth_factor", 0.75f));
  // Compactness is how tightly the block holds its length and width together.
  // ponytail: GF has no compactness tactic, so its defensive width factor is
  // read inversely (wider == looser). Add a real slider when the tactic pipeline
  // grows one.
  const float compactSlider =
      11.0f - 10.0f * AITactics::ClampSetting(GetLiveTacticReal("position_defense_width_factor", 0.8f));

  defensiveStyle = style;
  defensiveAttackLevel = attackLevel;
  defensiveLineSlider = lineSlider;
  defensiveCompactSlider = compactSlider;
  // PES's Pressuring is team-sheet option byte 6; GF's nearest instruction is
  // Aggressive Defence, which pushes the defender onto the ball. ponytail: add a
  // real Pressuring option when the touchline instructions grow one.
  defensivePressing = TeamInstructions::Has(instructions, TeamInstructions::e_Instruction_AggressiveDefence);

  float ownFrameLine = pes::DefensiveLineHeight(
      k, field, facts, lineSlider, style, attackLevel, match->IsInPlay(),
      pes::TargetLineCushionFactor(k, facts, hasCarrier, carrierIsOpponent, previousLineHeight,
                                   defensiveLineHeight));
  if (pes::DefensiveLineStepUp(k, field, facts, ownFrameLine, hasCarrier))
    ownFrameLine = facts.ballDepth_m - k.stepUpDistance_m;
  ownFrameLine = std::max(ownFrameLine, -field.ownGoalArea_m);

  // FUN_1405efbd0's catch-up: the back line steps a quarter of the gap to the
  // midfield band upfield when the midfield has been caught upfield of it.
  const float gap = k.midGap_m;
  std::vector<float> midScores;
  std::vector<Player*> active;
  team->GetActivePlayers(active);
  for (Player* p : active) {
    if (PesLineIndexForRole(p->GetFormationEntry().role) != 1) continue;
    const float x = p->GetPosition().coords[0] * dir;
    const float midLower = ownFrameLine + gap;
    midScores.push_back(x <= midLower + 2.0f ? 100.0f
                                             : 100.0f * (x - midLower - 8.0f) / -6.0f);
  }
  std::sort(midScores.begin(), midScores.end(), std::greater<float>());
  float score = 0.0f;
  for (size_t i = 0; i < midScores.size() && i < 3; ++i) score += midScores[i];
  ownFrameLine += pes::ShapeRamp(score, k.midLineScoreAdjustMax, 1.0f, 200.0f, 0.0f) *
                  std::fabs(gap) / k.midCatchUpDivisor;

  previousLineHeight = ownFrameLine;
  defensiveLineHeight = ownFrameLine * dir;
  defensiveTeamLength = pes::TeamLength_m(k, style, attackLevel, compactSlider);
}

bool TeamAIController::IsOffsideTrapArmed() const {
  // PES arms the trap only on its team flag `T+0xe4` (report 14.4); GF has no
  // touchline button for it, so the tactic stands in and defaults to PES's live
  // value for both teams, off.
  return GetLiveTacticReal("offside_trap", 0.0f) > 0.5f;
}

Vector3 TeamAIController::GetAdaptedFormationPosition(Player* player,
                                                      bool useDynamicFormationPosition) {
  bool toggle_yFocus = true;
  bool toggle_microFocus = true;
  bool toggle_midfieldFocus = true;

  e_PlayerRole role;
  if (useDynamicFormationPosition)
    role = player->GetDynamicFormationEntry().role;
  else
    role = player->GetFormationEntry().role;

  const float counterAttack = AITactics::ClampSetting(
      liveTeamTactics.GetReal("counter_attack", 0.5f));
  Vector3 focalPoint = match->GetDesignatedPossessionPlayer()->GetPosition();
  float urgencyBias =
      1.0f - NormalizedClamp((focalPoint - player->GetPosition()).GetLength(), 2.0f, 30.0f);
  float ballX = match->GetBall()->GetAveragePosition(3500 * (1.0f - urgencyBias * 0.7f)).coords[0];
  float ballY = match->GetBall()->GetAveragePosition(4000 * (1.0f - urgencyBias * 0.5f)).coords[1];

  float offense_depthFactor = mixup(liveTeamTactics.GetReal("position_offense_depth_factor"),
                                    "position_offense_depth_factor", role, counterAttack);
  float defense_depthFactor = mixup(liveTeamTactics.GetReal("position_defense_depth_factor"),
                                    "position_defense_depth_factor", role, counterAttack);
  float offense_widthFactor = mixup(liveTeamTactics.GetReal("position_offense_width_factor"),
                                    "position_offense_width_factor", role, counterAttack);
  float defense_widthFactor = mixup(liveTeamTactics.GetReal("position_defense_width_factor"),
                                    "position_defense_width_factor", role, counterAttack);
  float offense_ownHalfFactor = mixup(liveTeamTactics.GetReal("position_offense_ownhalf_factor"),
                                      "position_offense_ownhalf_factor", role, counterAttack);
  float defense_ownHalfFactor = mixup(liveTeamTactics.GetReal("position_defense_ownhalf_factor"),
                                      "position_defense_ownhalf_factor", role, counterAttack);
  float offense_midfieldFocus = mixup(liveTeamTactics.GetReal("position_offense_midfieldfocus"),
                                      "position_offense_midfieldfocus", role, counterAttack);
  float defense_midfieldFocus = mixup(liveTeamTactics.GetReal("position_defense_midfieldfocus"),
                                      "position_defense_midfieldfocus", role, counterAttack);
  float offense_midfieldFocusStrength =
      mixup(liveTeamTactics.GetReal("position_offense_midfieldfocus_strength"),
            "position_offense_midfieldfocus_strength", role, counterAttack);
  float defense_midfieldFocusStrength =
      mixup(liveTeamTactics.GetReal("position_defense_midfieldfocus_strength"),
            "position_defense_midfieldfocus_strength", role, counterAttack);
  float offense_sideFocusStrength =
      mixup(liveTeamTactics.GetReal("position_offense_sidefocus_strength"),
            "position_offense_sidefocus_strength", role, counterAttack);
  float defense_sideFocusStrength =
      mixup(liveTeamTactics.GetReal("position_defense_sidefocus_strength"),
            "position_defense_sidefocus_strength", role, counterAttack);
  float offense_microFocusStrength =
      mixup(liveTeamTactics.GetReal("position_offense_microfocus_strength"),
            "position_offense_microfocus_strength", role, counterAttack);
  float defense_microFocusStrength =
      mixup(liveTeamTactics.GetReal("position_defense_microfocus_strength"),
            "position_defense_microfocus_strength", role, counterAttack);

  offense_sideFocusStrength += (-0.5 + AI_GetMindSet(role)) * 0.2f;
  defense_sideFocusStrength += (0.5 - AI_GetMindSet(role)) * 0.2f;
  offense_sideFocusStrength =
      clamp(offense_sideFocusStrength + -0.3f + (offensivenessBias) * 0.3f, 0.0f, 1.0f);
  defense_sideFocusStrength =
      clamp(defense_sideFocusStrength + -0.3f + (1.0f - offensivenessBias) * 0.3f, 0.0f, 1.0f);
  if (player->GetDebug())
    printf("offense: %f, defense: %f\n", offense_sideFocusStrength, defense_sideFocusStrength);

  float possessionAmountBias = NormalizedClamp(fadingTeamPossessionAmount - 0.5f, 0.3f, 0.7f);
  // also take the ball's position as part of the possessionBias equation
  float ballBias = NormalizedClamp(((ballX / pitchHalfW) * -team->GetSide()), -0.7f,
                                   0.7f);  // 0 == own half, 1 == opponent half
  // if possessionBias is unclear (near 0.5), take ballBias more seriously as indicator of
  // possession.
  float ballBiasBias = 1.0f - fabs(possessionAmountBias * 2.0f - 1.0f);  // biasception
  ballBiasBias *= 0.6f;  // don't take ballBias too serious - we need defenders to somewhat keep
                         // watch when possession team is unclear, even when the ball is forward
  float possessionBias = possessionAmountBias * (1.0f - ballBiasBias) + ballBias * ballBiasBias;

  possessionBias = clamp(possessionBias + (offensivenessBias - 0.5f) * 0.3f, 0.0f, 1.0f);

  // offense can be a bit more relaxed
  focalPoint = (match->GetBall()->GetAveragePosition(3000).Get2D() * 1.0f + focalPoint * 0.0f) *
                   possessionBias +
               (match->GetBall()->GetAveragePosition(2000).Get2D() * 0.5f + focalPoint * 0.5f) *
                   (1.0f - possessionBias);

  float adaptedDepth = depth * (offense_depthFactor * possessionBias +
                                defense_depthFactor * (1.0f - possessionBias));
  float adaptedWidth = width * (offense_widthFactor * possessionBias +
                                defense_widthFactor * (1.0f - possessionBias));

  float offsetX = pitchHalfW * team->GetSide() *
                  ((offense_ownHalfFactor * 2.0f - 1.0f) * possessionBias +
                   (defense_ownHalfFactor * 2.0f - 1.0f) * (1.0f - possessionBias));

  float sideFocusStrength = (offense_sideFocusStrength * possessionBias +
                             defense_sideFocusStrength * (1.0f - possessionBias));

  float sideFocus = possessionBias * 2.0f - 1.0f;  // -1 == own side, 1 == opp side

  float sideX = 0.2f * sideFocus * -team->GetSide() * pitchHalfW +
                0.8f * -match->GetAveragePossessionSide(6000) * pitchHalfW;
  // exaggerate: sideX = clamp(sideX * 2.0f, -pitchHalfW, pitchHalfW);
  // if (player->GetDebug()) printf("sideX: %f\n", sideX);
  float centerX = clamp((ballX * (1.0f - sideFocusStrength) + sideX * sideFocusStrength) + offsetX,
                        -pitchHalfW, pitchHalfW);

  // center of width
  float centerY = clamp(ballY, -pitchHalfH, pitchHalfH);

  // leave space for actual depth (50% depth on both sides of center)
  float adaptCenterToFitDepthBias = 0.95f;  // the lower this value, the more players will move with
                                            // the centerX, and be clamped to the edges of the pitch
  float adaptCenterToFitWidthBias = 0.9f;   // the lower this value, the more players will move with
                                            // the centerY, and be clamped to the edges of the pitch
  centerX *= ((1.0f - adaptedDepth) / 1.0f) * adaptCenterToFitDepthBias +
             (1.0f - adaptCenterToFitDepthBias);
  centerY *= ((1.0f - adaptedWidth) / 1.0f) * adaptCenterToFitWidthBias +
             (1.0f - adaptCenterToFitWidthBias);

  float backXBound, frontXBound, lowYBound, highYBound;
  if (!teamHasPossession) {
    // PES 2021's open-play defensive shape (docs/re/pes-re-defensive-shape.md 6,
    // 7, 14.1): the whole team shape is three bands off one line height, so a
    // defender's depth and width terms both come from the line he plays in. PES
    // rebuilds both shapes every tick and the out-of-possession one is what a
    // defending team plays in.
    const int lineIndex = PesLineIndexForRole(role);
    const float line = defensiveLineHeight;
    const float len = defensiveTeamLength;
    // The back line sits on the line height itself (report 14.7: a back-line
    // slot's target is the line height, 82% within 5 cm live); the midfield a
    // gap above it, the forwards at the far end of the team length.
    float back = lineIndex == 0 ? line
                : lineIndex == 1
                    ? line + match->GetPesDefensiveShapeConstants().midGap_m
                    : std::max(line + len - 5.0f,
                               line + match->GetPesDefensiveShapeConstants().midGap_m);
    float front = lineIndex == 0 ? back + 3.0f
                 : lineIndex == 1 ? back + 2.0f
                                  : std::max(line + len, back + 2.0f);
    back = clamp(back, -pitchHalfW, pitchHalfW);
    front = clamp(front, -pitchHalfW, pitchHalfW);
    backXBound = std::min(back, front);
    frontXBound = std::max(back, front);

    // Each line's width comes from its own head count (15/19/23 m for 3/4/5
    // players), and the block is shifted toward the ball's touchline in
    // proportion to how wide the ball is played.
    const pes::DefensiveShapeConstants& shapeConstants =
        match->GetPesDefensiveShapeConstants();
    std::vector<Player*> teammates;
    team->GetActivePlayers(teammates);
    int headCount = 0;
    for (Player* mate : teammates)
      if (mate->GetFormationEntry().role != e_PlayerRole_GK &&
          PesLineIndexForRole(mate->GetFormationEntry().role) == lineIndex)
        ++headCount;
    const Vector3 currentBall = match->GetBall()->Predict(0);
    const float dir = static_cast<float>(-team->GetSide());
    const float half = pes::LineHalfWidth_m(
        shapeConstants, defensiveShapeField, headCount, defensiveCompactSlider,
        defensiveAttackLevel, true, currentBall.coords[1], currentBall.coords[0] * dir,
        line * dir);
    const float lateral = std::clamp((std::fabs(currentBall.coords[1]) -
                                      shapeConstants.lateralRampNear_m) /
                                         (shapeConstants.lateralRampFar_m -
                                          shapeConstants.lateralRampNear_m),
                                     0.0f, 1.0f);
    const float coefficient =
        lineIndex == 0 && headCount >= 3 ? 0.7f : 0.6f;  // report 7's per-line c table
    const float reach = pitchHalfH - 0.5f - half;
    const float shift = reach <= 0.0f
                            ? 0.0f
                            : std::clamp((currentBall.coords[1] > 0.0f ? 1.0f : -1.0f) * pitchHalfH *
                                             lateral * coefficient,
                                         -reach, reach);
    lowYBound = shift - half;
    highYBound = shift + half;
  } else {
    backXBound = centerX - adaptedDepth * pitchHalfW * -team->GetSide();
    frontXBound = centerX + adaptedDepth * pitchHalfW * -team->GetSide();
    lowYBound = centerY - adaptedWidth * pitchHalfH;
    highYBound = centerY + adaptedWidth * pitchHalfH;
    // maybe setting offside trap this way doesn't work too well: after all,
    // defensive positioning stuff is done after this, thereby diminishing trap
    // sometimes. on the other hand, maybe this is a good basic trap setup, and we
    // could apply the trap with the applyoffsidetrap function after the
    // defensive positioning as well. (done from the def/mid strategies code)
    if (backXBound * team->GetSide() > GetOffsideTrapX() * team->GetSide())
      backXBound = GetOffsideTrapX();
  }

  float xFocus = 0.0f;
  float xFocusStrength = 0.0f;
  float yFocus = ballY * 1.0f;
  float yFocusStrength =
      0.5f * possessionBias + 0.2f * (1.0f - possessionBias);  // todo: make dynamic/configurable

  float defendFactor = (1.0f - AI_GetMindSet(role)) * (1.0f - possessionBias);
  Vector3 microFocus = focalPoint;  // * Vector3(1.0f, 0.8f + (1.0f - defendFactor) * 0.2f, 0.0f);
                                    // // defenders stay in the middle more
  Vector3 defensiveFocusPos = Vector3(clamp(microFocus.coords[0] + team->GetSide() * 2.0f,
                                            -pitchHalfW, backXBound * team->GetSide()),
                                      microFocus.coords[1] * 0.9f, 0);
  microFocus =
      (Vector3(clamp(microFocus.coords[0] - team->GetSide() * 1.0f, -pitchHalfW, pitchHalfW),
               microFocus.coords[1] * 0.9f, 0)) *
          possessionBias +
      (defensiveFocusPos) * (1.0f - possessionBias);
  float microFocusStrength = (offense_microFocusStrength * possessionBias +
                              defense_microFocusStrength * (1.0f - possessionBias));

  // on own half in possession: little microfocus strength. on own half not in possession: lots of
  // microfocus. other half: the other way around.
  float microFocusSideBias = NormalizedClamp((ballX / pitchHalfW) * -team->GetSide(), -0.7f,
                                             0.7f);  // ball on own half == lower values
  microFocusSideBias = microFocusSideBias * 0.7f + 0.3f;
  float autoMicroFocusStrength =
      (std::pow(microFocusSideBias, 0.8f) * possessionBias +
       (std::pow(1.0f - microFocusSideBias, 0.6f)) * (1.0f - possessionBias));
  microFocusStrength = microFocusStrength * (0.2f + 0.8f * autoMicroFocusStrength);
  // if (team->GetID() == 0) printf("microfocusstrength: %f\n", microFocusStrength);

  // midfield focus. higher == more on opponent's half
  float manualMidfieldFocus =
      (offense_midfieldFocus * possessionBias + defense_midfieldFocus * (1.0f - possessionBias));
  float autoMidfieldFocus = NormalizedClamp((ballX / pitchHalfW) * -team->GetSide(), -0.8f,
                                            0.8f);  // midfield follows ball
  // autoMidfieldFocus *= 0.5f + 0.5f * possessionBias; // auto defensive stance on no possession
  // (deemed too defensive)
  float midfieldFocus = manualMidfieldFocus * 0.7f + autoMidfieldFocus * 0.3f;
  // if (player->GetDebug()) printf("midfieldfocus: %f\n", midfieldFocus);
  float midfieldFocusStrength = (offense_midfieldFocusStrength * possessionBias +
                                 defense_midfieldFocusStrength * (1.0f - possessionBias));
  if (!toggle_yFocus)
    yFocusStrength = 0.0f;
  if (!toggle_microFocus)
    microFocusStrength = 0.0f;
  if (!toggle_midfieldFocus)
    midfieldFocusStrength = 0.0f;

  Vector3 desiredPos = AI_GetAdaptedFormationPosition(
      team->GetMatch(), player, backXBound, frontXBound, lowYBound, highYBound, xFocus,
      xFocusStrength, yFocus, yFocusStrength, microFocus, microFocusStrength, midfieldFocus,
      midfieldFocusStrength, useDynamicFormationPosition);

  // The playing style bends the formation spot: a hole player pushes on while
  // his team attacks, an anchor man sits, a roaming flank tucks inside, and a
  // goal poacher ignores his depth altogether to lurk on the opponent's
  // offside line (roadmap 4D, docs/PES21_IMPORT.md).
  const PlayingStyles::Player style = player->GetPlayerData()->GetPlayingStyle();
  // Track Back: a midfielder or forward with the card drops deeper when his
  // team loses the ball.
  desiredPos.coords[0] +=
      -team->GetSide() * (PlayingStyles::GetDepthOffset(style, teamHasPossession) +
                          PlayerSkills::GetTrackBackDepth(player->GetPlayerData()->GetSkills(),
                                                          teamHasPossession));
  desiredPos.coords[1] += signSide(desiredPos.coords[1]) *
                          PlayingStyles::GetWidthOffset(style, teamHasPossession);
  if (style == PlayingStyles::Player::GoalPoacher && teamHasPossession &&
      AI_GetMindSet(role) > 0.5f) {
    const float oppOffsideLineX =
        AI_GetOffsideLine(match, match->GetMentalImage(0), abs(team->GetID() - 1));
    desiredPos.coords[0] = PlayingStyles::GetPoacherTargetX(style, desiredPos.coords[0],
                                                            oppOffsideLineX, team->GetSide());
  }
  desiredPos.coords[0] = clamp(desiredPos.coords[0], -pitchHalfW, pitchHalfW);
  desiredPos.coords[1] = clamp(desiredPos.coords[1], -pitchHalfH, pitchHalfH);

  return desiredPos;
}

void TeamAIController::CalculateDynamicRoles() {
  std::vector<Player*> players;
  team->GetActivePlayers(players);

  auto iter = players.begin();
  while (iter != players.end()) {
    if ((*iter)->GetFormationEntry().role == e_PlayerRole_GK) {
      players.erase(iter);
      break;
    }
  }

  unsigned int playerNum = players.size();

  // collect adapted formation positions
  std::vector<Vector3> adaptedFormationPositions;
  for (auto* player : players) {
    adaptedFormationPositions.push_back(GetAdaptedFormationPosition(player, false));
  }

  // first make a sorted list on all possible distances between players and formation targets
  std::vector<int> distances;
  for (unsigned int x = 0; x < playerNum; x++) {
    for (unsigned int y = 0; y < playerNum; y++) {
      const Vector3& playerPos = players.at(x)->GetPosition() + players.at(x)->GetMovement() * 0.5;
      const Vector3& formationPos = adaptedFormationPositions.at(y);
      float distance = (playerPos - formationPos).GetLength();
      distances.push_back(int(std::round(distance * 10)));
    }
  }

  std::sort(distances.begin(), distances.end());

  for (unsigned int i = playerNum; i < distances.size(); i += 5) {
    // libhungarian by Cyrill Stachniss, 2004

    hungarian_problem_t p;

    std::vector<int> r(playerNum * playerNum);

    for (unsigned int x = 0; x < playerNum; x++) {
      for (unsigned int y = 0; y < playerNum; y++) {
        const Vector3& playerPos =
            players.at(x)->GetPosition() + players.at(x)->GetMovement() * 0.5;
        const Vector3& formationPos = adaptedFormationPositions.at(y);
        float cost = (playerPos - formationPos).GetLength();
        int intCost = int(std::round(cost * 10));
        if (intCost >= distances.at(i))
          intCost = 50000;
        r[x + y * playerNum] = intCost;
      }
    }

    int** m = array_to_matrix(r.data(), playerNum, playerNum);

    /* initialize the hungarian_problem using the cost matrix*/
    int matrix_size = hungarian_init(&p, m, playerNum, playerNum, HUNGARIAN_MODE_MINIMIZE_COST);

    // fprintf(stderr, "assignment matrix has %d rows and %d columns.\n\n", matrix_size,
    // matrix_size);

    /* some output */
    // fprintf(stderr, "cost-matrix:");
    // hungarian_print_costmatrix(&p);

    /* solve the assignement problem */
    int totalCost = hungarian_solve(&p);

    /* some output */
    // fprintf(stderr, "assignment:");
    // hungarian_print_assignment(&p);

    bool ready = false;
    if (totalCost != -1 && (totalCost < 50000 || i >= distances.size() - 5)) {
      // printf("total cost: %i\n", totalCost);
      //  assign dynamic role with best cost
      for (unsigned int x = 0; x < playerNum; x++) {
        for (unsigned int y = 0; y < playerNum; y++) {
          if ((&p)->assignment[y][x] == 1) {
            FormationEntry formationEntry = players.at(y)->GetFormationEntry();
            players.at(x)->SetDynamicFormationEntry(formationEntry);
          }
        }
      }

      ready = true;
    }

    /* free used memory */
    hungarian_free(&p);
    for (unsigned int row = 0; row < playerNum; row++) {
      free(m[row]);
    }
    free(m);

    if (ready)
      break;
  }
}

void TeamAIController::CalculateManMarking() {
  // FUN_140534a20's assignment (report 3, 14.2), run every frame: PES re-decides
  // the marking each tick, GF re-decided it every 400 ms and capped it at three
  // opponents. Opponents are ordered most-advanced first, projected a sixth of a
  // second ahead; the passes are ported as report 14.2 reduces them - pass 0
  // (who may mark), pass 1 (the first defender on the carrier), pass 2b (who
  // needs a marker at all), pass 6 (continuity) and pass 7 (nearest free
  // defender, stealing from a further one). Passes 3, 4 and 5 (the sheet-driven
  // plan marks and the stopped corner/FK pairing) have no GF counterpart and are
  // left to the set-piece code; PES's at-most-three-opponents cap goes with them.
  std::vector<Player*> own;
  team->GetActivePlayers(own);

  // PES clears every mark target before the passes run.
  for (Player* p : own) p->SetManMarkingID(-1);

  if (!match->IsInPlay() || match->IsInSetPiece()) {
    for (size_t slot = 0; slot < markPendingOpponent.size(); ++slot)
      markPendingOpponent[slot] = -1;
    return;
  }

  const float dir = static_cast<float>(-team->GetSide());

  // The opponents, most advanced first: a man running goal-side sorts in front of
  // where he stands.
  std::vector<Player*> opponents;
  match->GetActiveTeamPlayers(abs(team->GetID() - 1), opponents);
  std::vector<std::pair<float, Player*>> order;
  order.reserve(opponents.size());
  for (Player* o : opponents)
    order.emplace_back(
        pes::OpponentOrderKey(dir, o->GetPosition().coords[0] * dir,
                              o->GetMovement().coords[0] * dir),
        o);
  std::sort(order.begin(), order.end(),
            [](const std::pair<float, Player*>& a, const std::pair<float, Player*>& b) {
              return a.first < b.first;
            });

  // Pass 0: who may take a mark. The keeper never marks and the man carrying
  // the ball is busy with it.
  Player* carrier = match->GetTeam(abs(team->GetID() - 1))->GetDesignatedTeamPossessionPlayer();
  std::vector<Player*> available;
  for (Player* p : own)
    if (p->GetFormationEntry().role != e_PlayerRole_GK && p != carrier) available.push_back(p);

  // Who marked whom last frame, for continuity.
  std::vector<int> previous(markPendingOpponent.size(), -1);
  for (Player* p : own)
    if (p->GetManMarkingID() >= 0 && static_cast<size_t>(p->GetManMarkingID()) < previous.size())
      previous[p->GetManMarkingID()] = p->GetID();

  // This frame's candidate pairing per own player, before the hand-off delay
  // commits it.
  std::vector<int> candidate(own.size(), -1);
  auto candidateSlot = [&own](Player* p) {
    for (size_t i = 0; i < own.size(); ++i)
      if (own[i] == p) return static_cast<int>(i);
    return -1;
  };

  // Pass 1: the first defender on the carrier. The chaser is the man nearest the
  // ball; PES keeps him while he is within 8 m of the carrier, 10 m once he
  // already marks him, which for a ball-side search is the nearest man.
  if (carrier) {
    const Vector3 ballPos = match->GetBall()->Predict(0);
    Player* chaser = nullptr;
    float bestDistance = 1000.0f;
    for (Player* p : available) {
      const float distance = (p->GetPosition() - ballPos).GetLength();
      if (distance < bestDistance) bestDistance = distance, chaser = p;
    }
    if (chaser) {
      candidate[candidateSlot(chaser)] = carrier->GetID();
      available.erase(std::remove(available.begin(), available.end(), chaser), available.end());
    }
  }

  const float stealRadius = defensiveAttackLevel < 3
                                ? match->GetPesDefensiveShapeConstants().stealRadiusLowLevel_m
                                : match->GetPesDefensiveShapeConstants().stealRadius_m;
  for (const std::pair<float, Player*>& entry : order) {
    Player* opp = entry.second;
    if (opp == carrier) continue;
    Player* chosen = nullptr;
    // Pass 6: continuity. His own marker keeps him unless another free defender
    // is already closer to him than the marker is.
    const int lastId = static_cast<size_t>(opp->GetID()) < previous.size()
                           ? previous[opp->GetID()]
                           : -1;
    Player* lastMarker = lastId >= 0 ? match->GetPlayer(lastId) : nullptr;
    if (lastMarker && std::find(available.begin(), available.end(), lastMarker) != available.end()) {
      chosen = lastMarker;
      for (Player* p : available) {
        if (p == lastMarker) continue;
        if ((p->GetPosition() - opp->GetPosition()).GetLength() <
            (lastMarker->GetPosition() - opp->GetPosition()).GetLength()) {
          chosen = nullptr;
          break;
        }
      }
    }
    // Pass 7: the nearest free defender, up to the steal radius.
    if (!chosen) {
      float best = stealRadius;
      for (Player* p : available) {
        const float distance = (p->GetPosition() - opp->GetPosition()).GetLength();
        if (distance < best) best = distance, chosen = p;
      }
    }
    if (chosen) {
      candidate[candidateSlot(chosen)] = opp->GetID();
      available.erase(std::remove(available.begin(), available.end(), chosen), available.end());
    }
  }

  // The hand-off hysteresis (report 3.3): a changed target is committed only once
  // the same new one has been pending for the organiser's delay.
  const unsigned long now = match->GetActualTime_ms();
  const pes::DefensiveShapeConstants& shape = match->GetPesDefensiveShapeConstants();
  const unsigned long delay_ms =
      static_cast<unsigned long>(pes::MarkHandOffFrames(shape, 0)) * 1000 /
      static_cast<unsigned long>(shape.cushionFps);
  for (size_t i = 0; i < own.size(); ++i) {
    const int playerId = own[i]->GetID();
    if (markPendingOpponent.size() <= static_cast<size_t>(playerId)) {
      markPendingOpponent.resize(playerId + 1, -1);
      markPendingSinceTick.resize(playerId + 1, 0);
    }
    const int want = candidate[i];
    if (want == own[i]->GetManMarkingID()) continue;  // unchanged: nothing pending
    if (markPendingOpponent[playerId] != want) {
      markPendingOpponent[playerId] = want;
      markPendingSinceTick[playerId] = now;
    } else if (now - markPendingSinceTick[playerId] >= delay_ms) {
      own[i]->SetManMarkingID(want);
      markPendingOpponent[playerId] = -1;
    }
  }
}

Vector3 TeamAIController::GetMarkSpot(Player* defender, Player* opp) {
  // FUN_140623750 (report 14.3): the goal-side point 6 - 4*ramp(depth) metres
  // from the opponent's predicted position, 2 m further when our team is
  // pressing.
  const pes::DefensiveShapeConstants& k = match->GetPesDefensiveShapeConstants();
  const PlayerImage oppImage = match->GetMentalImage(0)->GetPlayerImage(opp->GetID());
  const Vector3 oppPos = oppImage.position + oppImage.movement * 0.2f;
  const float dir = static_cast<float>(-team->GetSide());
  const float distance =
      pes::MarkDistance_m(k, defensiveShapeField, oppPos.coords[0] * dir, defensivePressing);
  return pes::MarkSpot(oppPos, distance, Vector3(pitchHalfW * team->GetSide(), 0.0f, 0.0f));
}

void TeamAIController::ApplyOffsideTrap(Vector3& position) const {
  // binary 'hard clamp' method
  // if (position.coords[0] * team->GetSide() > offsideTrapX * team->GetSide()) position.coords[0] =
  // offsideTrapX;

  // smooth version

  // area, centered around offsideTrapX, that will be compressed.
  // so the area from [offsideTrapX - areaLength .. offsideTrapX + areaLength] will be compressed
  // into [offsideTrapX .. offsideTrapX + areaLength]
  float areaHalfLength = 2.0f;  // 4.0f;
  float absPosX = position.coords[0] * team->GetSide();
  float absOffsideTrapX = offsideTrapX * team->GetSide();

  if (absPosX > absOffsideTrapX - areaHalfLength) {
    float areaFront = absOffsideTrapX - areaHalfLength;
    float posFromAreaFront = absPosX - areaFront;
    float posFactor = posFromAreaFront / (areaHalfLength * 2.0f);
    posFactor = clamp(posFactor, 0.0f, 1.0f);  // 0.0f == most forward, 1.0f == deepest players
    // posFactor = pow(posFactor, 0.5f); // don't impact players on the non-offside side of the
    // offsideline as much as the other way around

    float absResultPosX =
        areaFront +
        areaHalfLength *
            posFactor;  // this compresses the 2 * areaHalfLength into 1 * areaHalfLength

    position.coords[0] = absResultPosX * team->GetSide();
  }
}

void TeamAIController::PrepareSetPiece(e_SetPiece setPiece, int takerTeamID) {
  setPieceType = setPiece;

  if (takerTeamID == -1)
    assert(setPieceType == e_SetPiece_None);
  if (setPieceType == e_SetPiece_None)
    return;

  Match* match = team->GetMatch();
  const Vector3 ballPos = match->GetBall()->Predict(0).Get2D();
  const pes::SetPieceTeamConstants& spk = match->GetPesSetPieceTeamConstants();
  // The counts that live in instruction immediates rather than in .rdata.
  const pes::SetPieceTeamCounts& spc = pes::SetPieceTeamCounts();

  const bool isTakerTeam = takerTeamID == team->GetID() ? true : false;
  Team* takerTeam = isTakerTeam ? team : match->GetTeam(abs(team->GetID() - 1));
  // The side whose box a restart may require everyone out of, and the direction
  // that team attacks in - PES's `dir` (choreography §2).
  const int restartingSide = takerTeam->GetSide();
  const int dir = -restartingSide;
  // PES's attack level (choreography 11.6) is the team's mentality rung.
  const int attackLevel = pes::AttackLevel((int)GetInstructions().mentality);

  std::vector<Player*> players;
  team->GetActivePlayers(players);
  // Where everyone stood when play stopped. Every placement move starts there.
  std::map<Player*, Vector3> started;
  for (auto* player : players) started[player] = player->GetPosition();


  // PES's placement is a jog (choreography 11.3): a player ends up as far as
  // 16 km/h covers inside the kind's time budget, from where he stood at the
  // stop - never a teleport across the pitch.
  auto place = [this, &spk, &started, ballPos](Player* player, const Vector3& mark,
                                               float budget_s) {
    const Vector3 from = started[player];
    const Vector3 delta = mark - from;
    player->ResetPosition(from + delta * pes::JogFraction(budget_s, delta.GetLength(), spk),
                          ballPos);
  };

  // Law 16, PES's way (choreography 11.3, raw 0x1409297df): at a goal kick, and
  // at a free kick in the restarting team's own box, everyone except the kicker
  // and that side's two back roles is pushed out of the box being kicked from.
  // PES applies this to team-mates too, which is what keeps a striker from
  // standing in the six-yard box waiting for his own goal kick.
  auto pushOutOfBox = [this, ballPos, restartingSide](Player* player, const Vector3& mark,
                                                     bool keepHim) {
    Vector3 at = mark;
    if (SetPieceLaws::IntrudesOnPenaltyArea(at.coords[0], at.coords[1], restartingSide,
                                            pitchHalfW)) {
      float outX = at.coords[0], outZ = at.coords[1];
      SetPieceLaws::ClearingTarget(at.coords[0], at.coords[1], restartingSide, pitchHalfW, &outX,
                                  &outZ);
      at = Vector3(outX, outZ, 0);
    }
    if (!keepHim) return;  // the kicker and the two back roles stay put
    player->ResetPosition(at, ballPos);
  };

  // Keepers are not in the placement list, but they are placed too, and PES
  // gives the restarting side's own keeper an unlimited budget (choreography
  // 11.3) while the other side's is a back-class one.
  std::vector<Player*> keepers;
  team->GetActivePlayers(keepers);
  for (auto* player : keepers) {
    if (player->GetFormationEntry().role != e_PlayerRole_GK) continue;
    const float budget = pes::PlacementBudget_s(
        spk, (int)setPiece, isTakerTeam, e_PlayerRole_GK, false, false, false, isTakerTeam, false,
        false);
    place(player, Vector3(pitchHalfW * team->GetSide() * 0.98, 0, 0), budget);
  }
  players.clear();
  team->GetActivePlayers(players);
  auto iter = players.begin();
  while (iter != players.end()) {
    if ((*iter)->GetFormationEntry().role == e_PlayerRole_GK) {
      iter = players.erase(iter);
    } else {
      iter++;
    }
  }
  started.clear();
  for (auto* player : players) started[player] = player->GetPosition();

  if (isTakerTeam)
    team->SetFadingTeamPossessionAmount(1.5f);
  else
    team->SetFadingTeamPossessionAmount(0.5f);

  // Load custom set-piece formation parameters
  SetPieceParams spParams = SetPieceConfig::Load(team->GetTeamData()->GetDatabaseID(), setPiece);
  float depth = spParams.formation_depth;
  float width = spParams.formation_width;

  // PES's per-kind budget (choreography 11.3, FUN_140927ab0).
  auto budgetFor = [&](Player* player) {
    const e_PlayerRole role = player->GetFormationEntry().role;
    return pes::PlacementBudget_s(spk, (int)setPiece, isTakerTeam, role, false, false, false,
                                  role == e_PlayerRole_GK, false, /*isMarking=*/false);
  };

  switch (setPiece) {
    case e_SetPiece_KickOff:
      for (auto* player : players) {
        Vector3 basePos =
            player->GetFormationEntry().position *
            Vector3(-team->GetSide() * pitchHalfW * 0.6, -team->GetSide() * pitchHalfH * 0.6, 0);
        basePos.coords[1] +=
            random(-2.0f, 2.0f);  // to stop people from bumping into each other and such
        basePos.coords[0] *= 0.5;
        basePos.coords[0] += (pitchHalfW * 0.2) * team->GetSide();
        if (basePos.coords[0] * team->GetSide() < 0.5)
          basePos.coords[0] = 0.5 * team->GetSide();  // not allowed to stand on opp side
        if (basePos.GetLength() < 9.4) {              // not allowed to stand in center spot
          basePos.Normalize(Vector3(team->GetSide(), 0, 0));
          basePos *= 9.4;
        }
        place(player, basePos, budgetFor(player));
      }
      break;

    case e_SetPiece_GoalKick:
      for (auto* player : players) {
        float backXBound, frontXBound, lowYBound, highYBound;
        if (isTakerTeam) {
          backXBound = team->GetSide() * pitchHalfW * 0.5;
          frontXBound = -team->GetSide() * pitchHalfW * 0.2;
        } else {
          backXBound = team->GetSide() * pitchHalfW * 0.4;
          frontXBound = -team->GetSide() * pitchHalfW * 0.1;
        }
        lowYBound = -pitchHalfH * 0.7;
        highYBound = pitchHalfH * 0.7;
        Vector3 basePos =
            AI_GetAdaptedFormationPosition(match, player, backXBound, frontXBound, lowYBound,
                                           highYBound, 0, 0, 0, 0, 0, 0, 0, 0, false);
        place(player, basePos, budgetFor(player));
      }
      break;

    case e_SetPiece_Corner: {
      // The defending side's post men (choreography 11.5): the near-post man is
      // the corner-side role 2-4 player with the lowest height + jump + heading,
      // and a far-post man joins him only when the defending team's attack level
      // is 0. PES reads the three as b[0x31] + b[0x2c] + b[0x1d].
      auto aerial = [](Player* player) {
        return player->GetPlayerData()->GetHeight() + player->GetStat("physical_jump") +
               player->GetStat("technical_header");
      };
      Player* nearPost = 0;
      Player* farPost = 0;
      if (!isTakerTeam) {
        int bestIndex = -1;
        float bestScore = 1e9f;
        for (unsigned int i = 0; i < players.size(); i++) {
          Player* player = players.at(i);
          const e_PlayerRole role = player->GetFormationEntry().role;
          const bool sameSide =
              ballPos.coords[1] * player->GetFormationEntry().position.coords[1] > 0.0f;
          const bool narrow = role == e_PlayerRole_LB || role == e_PlayerRole_RB ||
                              role == e_PlayerRole_DM || role == e_PlayerRole_CM;
          const bool wide = narrow || role == e_PlayerRole_LM || role == e_PlayerRole_RM ||
                            role == e_PlayerRole_AM;
          if (!sameSide ? !wide : !narrow) continue;
          if (aerial(player) < bestScore) {
            bestScore = aerial(player);
            nearPost = player;
            bestIndex = static_cast<int>(i);
          }
        }
        (void)bestIndex;
        if (attackLevel < 1) {
          float bestFar = 1e9f;
          for (unsigned int i = 0; i < players.size(); i++) {
            Player* player = players.at(i);
            if (player == nearPost || player->GetFormationEntry().role == e_PlayerRole_GK) continue;
            if (aerial(player) < bestFar) {
              bestFar = aerial(player);
              farPost = player;
            }
          }
        }
      }
      // The attacking side's men in the box: 4 + min(attack level, 2)
      // (FUN_1404f6680). Who they are is PES's own ranking, which is the same
      // aerial score with the defenders demoted.
      std::vector<Player*> boxAttackers;
      if (isTakerTeam) {
        std::vector<Player*> outfield = players;
        std::sort(outfield.begin(), outfield.end(), [&aerial](Player* a, Player* b) {
          return aerial(a) > aerial(b);
        });
        const int count = std::min<int>(pes::BoxAttackerCount(attackLevel, spc),
                                        static_cast<int>(outfield.size()));
        boxAttackers.assign(outfield.begin(), outfield.begin() + count);
      }
      // The two post spots: 0.5 m off the goal line, the near post 0.31 m inside
      // it and the far post 0.7 m wide of it (live, choreography 11.5).
      const float goalSign = ballPos.coords[0] >= 0.0f ? 1.0f : -1.0f;
      const float zSign = ballPos.coords[1] >= 0.0f ? 1.0f : -1.0f;
      const Vector3 nearMark(goalSign * (pitchHalfW - 0.5f), zSign * 3.35f, 0);
      const Vector3 farMark(goalSign * (pitchHalfW - 0.5f),
                            zSign * -(goalInnerHalfWidth - 0.7f), 0);

      for (unsigned int i = 0; i < players.size(); i++) {
        if (players.at(i) == nearPost) {
          place(players.at(i), nearMark, spk.budgetPostMan_s);
          continue;
        }
        if (players.at(i) == farPost) {
          place(players.at(i), farMark, spk.budgetPostMan_s);
          continue;
        }
        if (std::find(boxAttackers.begin(), boxAttackers.end(), players.at(i)) !=
            boxAttackers.end()) {
          const int rank = static_cast<int>(
              std::find(boxAttackers.begin(), boxAttackers.end(), players.at(i)) -
              boxAttackers.begin());
          // ponytail: PES's authored positionCK_N_V spots are not shipped, so GF
          // lays its own box men out inside the goal area: three across, two
          // deep. The count is PES's; the geometry is this engine's.
          place(players.at(i),
                Vector3(goalSign * (pitchHalfW - 7.0f - 6.0f * static_cast<float>(rank / 3)),
                        (static_cast<float>(rank % 3) - 1.0f) * 4.5f, 0),
                budgetFor(players.at(i)));
          continue;
        }
        float backXBound, frontXBound, lowYBound, highYBound, xFocus, xFocusStrength, yFocus,
            yFocusStrength, midfieldFocus, midfieldFocusStrength;
        if (isTakerTeam) {
          backXBound = -team->GetSide() * pitchHalfW * 0.2;
          frontXBound = -team->GetSide() * pitchHalfW * 0.96;
          xFocus = frontXBound * 0.85;
          xFocusStrength = 0.7f;
          yFocus = ballPos.coords[1] * 0.1f;
          yFocusStrength = 0.7f;
          midfieldFocus = 0.9f;
          midfieldFocusStrength = 0.5f;
        } else {
          backXBound = team->GetSide() * pitchHalfW * 0.98;
          frontXBound = team->GetSide() * pitchHalfW * 0.5;
          xFocus = backXBound * 0.94;
          xFocusStrength = 0.8f;
          yFocus = ballPos.coords[1] * 0.1f;
          yFocusStrength = 0.9f;
          midfieldFocus = 0.1f;
          midfieldFocusStrength = 0.7f;
        }
        lowYBound = -pitchHalfH * 0.6 * width;
        highYBound = pitchHalfH * 0.6 * width;
        Vector3 basePos = AI_GetAdaptedFormationPosition(
            match, players.at(i), backXBound, frontXBound, lowYBound, highYBound, xFocus,
            xFocusStrength, yFocus, yFocusStrength,
            Vector3(ballPos.coords[0] * 0.95f, ballPos.coords[1] * 0.1f, 0), 0.9, midfieldFocus,
            midfieldFocusStrength, false);
        place(players.at(i), basePos, budgetFor(players.at(i)));
      }
      break;
    }

    case e_SetPiece_ThrowIn:
      for (unsigned int i = 0; i < players.size(); i++) {
        float backXBound, frontXBound, lowYBound, highYBound, xFocus, xFocusStrength, yFocus,
            yFocusStrength;
        Vector3 ballPos = match->GetBall()->Predict(0).Get2D();
        if (isTakerTeam) {
          backXBound = clamp(ballPos.coords[0] + 30 * team->GetSide(), -pitchHalfW, pitchHalfW);
          frontXBound = clamp(ballPos.coords[0] + 20 * -team->GetSide(), -pitchHalfW, pitchHalfW);
          xFocus = clamp(ballPos.coords[0] + 4 * -team->GetSide(), -pitchHalfW, pitchHalfW);
          xFocusStrength = 0.4;
          yFocus = ballPos.coords[1] * 0.996;
          yFocusStrength = 0.6;
        } else {
          backXBound = clamp(ballPos.coords[0] + 30 * team->GetSide(), -pitchHalfW, pitchHalfW);
          frontXBound = clamp(ballPos.coords[0] + 15 * -team->GetSide(), -pitchHalfW, pitchHalfW);
          xFocus = clamp(ballPos.coords[0] + 16 * team->GetSide(), -pitchHalfW, pitchHalfW);
          xFocusStrength = 0.2;
          yFocus = ballPos.coords[1] * 0.95;
          yFocusStrength = 0.5;
        }
        lowYBound = -pitchHalfH * 0.75f + ballPos.coords[1] * 0.25f;
        highYBound = pitchHalfH * 0.75f + ballPos.coords[1] * 0.25f;
        Vector3 basePos = AI_GetAdaptedFormationPosition(
            match, players.at(i), backXBound, frontXBound, lowYBound, highYBound, xFocus,
            xFocusStrength, yFocus, yFocusStrength, ballPos, 0.7, 0, 0, false);
        place(players.at(i), basePos, budgetFor(players.at(i)));
      }
      break;

    case e_SetPiece_FreeKick:
      for (unsigned int i = 0; i < players.size(); i++) {
        float backXBound, frontXBound, lowYBound, highYBound, xFocus, xFocusStrength, yFocus,
            yFocusStrength;
        Vector3 ballPos = match->GetBall()->Predict(0).Get2D();
        if (isTakerTeam) {
          float xOffset =
              clamp((ballPos.coords[0] * -team->GetSide()) / pitchHalfW, -1.0, 1.0) * 0.5 +
              0.5;  // 0 == close to our goal, 1 == far from our goal
          backXBound = clamp(team->GetSide() * pitchHalfW * (depth - xOffset * depth), -pitchHalfW,
                             pitchHalfW);
          frontXBound = clamp(team->GetSide() * pitchHalfW * (-0.43f * depth - xOffset * depth),
                              -pitchHalfW, pitchHalfW);
          // -0.43 pushes attackers forward past centre into the opponent half for deep free kicks
          // printf("fb: %f %f\n", backXBound, frontXBound);
          xFocus = clamp(ballPos.coords[0] + 10 * -team->GetSide(), -pitchHalfW, pitchHalfW);
          xFocusStrength = 0.5 + xOffset * 0.2;
          yFocus = ballPos.coords[1] * 0.4;
          yFocusStrength = 0.6 + xOffset * 0.2;
        } else {
          float xOffset =
              clamp((ballPos.coords[0] * -team->GetSide()) / pitchHalfW, -1.0, 1.0) * 0.5 +
              0.5;  // 0 == close to our goal, 1 == far from our goal
          // xOffset *= 40.0;
          backXBound =
              clamp(team->GetSide() * pitchHalfW * (1.0 - xOffset * 0.6), -pitchHalfW, pitchHalfW);
          frontXBound =
              clamp(team->GetSide() * pitchHalfW * (0.5 - xOffset * 0.8), -pitchHalfW, pitchHalfW);
          xFocus = clamp(ballPos.coords[0] + 20 * team->GetSide(), -pitchHalfW, pitchHalfW);
          xFocusStrength = 0.6 - xOffset * 0.4;
          yFocus = ballPos.coords[1] * 0.2;
          yFocusStrength = 0.8 - xOffset * 0.4;
        }
        lowYBound = -pitchHalfH * 0.7 * width;
        highYBound = pitchHalfH * 0.7 * width;
        Vector3 basePos = AI_GetAdaptedFormationPosition(
            match, players.at(i), backXBound, frontXBound, lowYBound, highYBound, xFocus,
            xFocusStrength, yFocus, yFocusStrength, ballPos, 0.4, 0, 0, false);

        // keep distance
        if (!isTakerTeam) {
          if ((basePos - match->GetBall()->Predict(0).Get2D()).GetLength() < 9.15) {
            basePos = match->GetBall()->Predict(0).Get2D() +
                      (basePos - match->GetBall()->Predict(0).Get2D()).GetNormalized() * 9.15;
          }
        }

        place(players.at(i), basePos, budgetFor(players.at(i)));
      }

      // The wall (choreography 5.3 and 11.4, kicker report 3.1): as many men as
      // the size table wants, standing 9.15 m from the ball toward the aim point
      // and half a metre apart along the wall line.
      if (!isTakerTeam) {
        const float goalX = dir * pitchHalfW;
        const float goalDx = goalX - ballPos.coords[0], goalDz = -ballPos.coords[1];
        const float goalD = std::sqrt(goalDx * goalDx + goalDz * goalDz);
        const float ballAngle = std::atan2(std::fabs(ballPos.coords[1]), std::fabs(goalDx)) *
                                180.0f / static_cast<float>(M_PI);
        const int wallSize = pes::WallSize(goalD, ballAngle, ballPos.coords[0],
                                          ballPos.coords[1], dir, pitchHalfW,
                                          pitchHalfW - SetPieceLaws::kAreaDepth,
                                          SetPieceLaws::kAreaHalfWidth, spk, spc);
        // The aim point: the goal centre for a one-man wall, else 1.3 m outside
        // the near post on the ball's side (0x14259bdd8).
        const int ballSide = ballPos.coords[1] >= 0.0f ? 1 : -1;
        Vector3 aim(goalX, 0, 0);
        if (wallSize > 1) aim = Vector3(goalX, ballSide * (goalInnerHalfWidth + spk.wallNearPost_m), 0);
        Vector3 toAim = aim - ballPos;
        toAim.Normalize(0);
        // The wall's yaw is perpendicular to the ball's bearing, and the members
        // are spread 0.5 m apart about the centre with the first on it.
        Vector3 along(-toAim.coords[1], toAim.coords[0], 0);
        std::vector<Player*> closest;
        AI_GetClosestPlayers(team, ballPos, closest, wallSize);
        for (unsigned int i = 0; i < closest.size(); i++) {
          const float offset = (static_cast<float>(i) - (closest.size() - 1) * 0.5f) * spk.wallSpacing_m;
          place(closest.at(i),
                ballPos + toAim * spk.wallDistance_m + along * offset,
                // a wall man has no budget at all (PES passes FLT_MAX)
                -1.0f);
        }
      }

      break;

    case e_SetPiece_Penalty:
      for (unsigned int i = 0; i < players.size(); i++) {
        float backXBound, frontXBound, lowYBound, highYBound, xFocus, xFocusStrength, yFocus,
            yFocusStrength;
        Vector3 ballPos = match->GetBall()->Predict(0).Get2D();
        if (isTakerTeam) {
          backXBound = clamp(ballPos.coords[0] + 50 * team->GetSide(), -pitchHalfW, pitchHalfW);
          frontXBound = clamp(ballPos.coords[0] + 10 * -team->GetSide(), -pitchHalfW, pitchHalfW);
          xFocus = ballPos.coords[0];
          xFocusStrength = 0.6;
          yFocus = 0.0;
          yFocusStrength = 0.8;
        } else {
          backXBound = clamp(ballPos.coords[0] + 11 * team->GetSide(), -pitchHalfW, pitchHalfW);
          frontXBound = clamp(ballPos.coords[0] + 20 * -team->GetSide(), -pitchHalfW, pitchHalfW);
          xFocus = ballPos.coords[0];
          xFocusStrength = 1.0;
          yFocus = 0.0;
          yFocusStrength = 1.0;
        }
        lowYBound = -pitchHalfH * 0.8;
        highYBound = pitchHalfH * 0.8;
        Vector3 basePos = AI_GetAdaptedFormationPosition(
            match, players.at(i), backXBound, frontXBound, lowYBound, highYBound, xFocus,
            xFocusStrength, yFocus, yFocusStrength, 0, 0, 0, 0, false);

        // outside the box
        signed int penaltySide = (match->GetBall()->Predict(0).coords[0] < 0) ? -1 : 1;
        if (basePos.coords[0] * penaltySide > pitchHalfW - 16.5 - 0.5)
          basePos.coords[0] = (pitchHalfW - 16.5 - 0.5) * penaltySide;

        // outside penalty arc as well
        if ((basePos - match->GetBall()->Predict(0).Get2D()).GetLength() < 9.15 + 0.5) {
          basePos = match->GetBall()->Predict(0).Get2D() +
                    (basePos - match->GetBall()->Predict(0).Get2D()).GetNormalized() * (9.15 + 0.5);
        }

        place(players.at(i), basePos, budgetFor(players.at(i)));
      }
      break;

    default:
      for (unsigned int i = 0; i < players.size(); i++) {
        Vector3 basePos =
            players.at(i)->GetFormationEntry().position *
            Vector3(-team->GetSide() * pitchHalfW * 0.7, -team->GetSide() * pitchHalfH * 0.7, 0);

        place(players.at(i), basePos, budgetFor(players.at(i)));
      }
      break;
  }

  if (isTakerTeam) {
    // Who takes it (kicker report 1): the team sheet's taker where GF has one,
    // otherwise the best set-piece suitability. GF carries no sheet of
    // designated takers (PES T+0x8e08) and no per-player set-piece ratings
    // (playerdata+0x96), so the suitability below is this engine's nearest
    // standing for it.
    // ponytail: PES rates six kinds per player. GF's nearest stats stand in -
    // long free kick -> curl, short -> short pass, the second kicker -> tight
    // possession, both corners -> curl, penalty -> set piece - so a player who
    // is a good long-range curler is also GF's best corner taker. The ceiling
    // is that GF cannot tell a left-footed corner taker from a right-footed one.
    auto suitability = [this](Player* player, e_SetPiece kind) {
      if (kind == e_SetPiece_FreeKick) return player->GetStat("technical_curl");
      if (kind == e_SetPiece_Penalty) return player->GetStat("technical_setpiece");
      if (kind == e_SetPiece_Corner) return player->GetStat("technical_curl");
      return player->GetStat("technical_setpiece");
    };
    std::vector<Player*> onPitch;
    team->GetActivePlayers(onPitch);
    std::vector<Player*>::const_iterator keep = onPitch.begin();
    while (keep != onPitch.end()) {
      if ((*keep)->GetFormationEntry().role == e_PlayerRole_GK)
        keep = onPitch.erase(keep);
      else
        keep++;
    }
    // A corner never goes to a striker (kicker report 1.1: role != 9), and a
    // penalty never to a keeper, which the list above already excludes.
    if (setPiece == e_SetPiece_Corner) {
      std::vector<Player*>::const_iterator drop = onPitch.begin();
      while (drop != onPitch.end()) {
        if ((*drop)->GetFormationEntry().role == e_PlayerRole_CF)
          drop = onPitch.erase(drop);
        else
          drop++;
      }
    }

    if (setPiece == e_SetPiece_FreeKick) {
      // PES's waterfall, minus its designated branch: the keeper near his own
      // goal, the nearest defender in our own half, the nearest outfield player
      // from far out, and the rating fallback last.
      std::vector<pes::SetPieceCandidate> candidates;
      for (unsigned int i = 0; i < onPitch.size(); i++) {
        pes::SetPieceCandidate c;
        c.index = static_cast<int>(i);
        c.role = onPitch.at(i)->GetFormationEntry().role;
        c.isKeeper = c.role == e_PlayerRole_GK;
        c.suitability = suitability(onPitch.at(i), setPiece);
        c.distanceToBall_m = (onPitch.at(i)->GetPosition() - ballPos).GetLength();
        candidates.push_back(c);
      }
      // PES's role filter for the far-out step is "role < 7", which is every
      // role except the three forwards.
      const int pick = pes::FreeKickTaker(candidates, ballPos.coords[0], ballPos.coords[1], dir,
                                          dir * pitchHalfW, -dir * pitchHalfW,
                                          pitchHalfW - SetPieceLaws::kAreaDepth,
                                          SetPieceLaws::kAreaHalfWidth, spk);
      if (pick >= 0 && pick < static_cast<int>(onPitch.size()))
        taker = onPitch.at(pick);
      else {
        float best = -1.0f;
        taker = onPitch.empty() ? 0 : onPitch.front();
        for (unsigned int i = 0; i < onPitch.size(); i++) {
          const float rating = suitability(onPitch.at(i), setPiece);
          if (rating > best) {
            best = rating;
            taker = onPitch.at(i);
          }
        }
      }
    } else if (setPiece == e_SetPiece_GoalKick) {
      // PES gives a goal kick to the keeper outright (choreography §2, the slot
      // whose role is 0); the fallback below is only for a team with no keeper
      // registered in goal.
      std::vector<Player*> keepers2;
      team->GetActivePlayers(keepers2);
      for (unsigned int i = 0; i < keepers2.size(); i++) {
        if (keepers2.at(i)->GetFormationEntry().role == e_PlayerRole_GK) {
          taker = keepers2.at(i);
          break;
        }
      }
    } else {
      // A corner, a penalty or anything else without a sheet: the best
      // suitability, first on ties (FUN_140a621f0's shape).
      float best = -1.0f;
      taker = onPitch.empty() ? 0 : onPitch.front();
      for (unsigned int i = 0; i < onPitch.size(); i++) {
        const float rating = suitability(onPitch.at(i), setPiece);
        if (rating > best) {
          best = rating;
          taker = onPitch.at(i);
        }
      }
    }

    if (taker == 0) taker = AI_GetClosestPlayer(team, ballPos);
    // A keeper who ends up with a restart is placed where that restart wants
    // him, not where the outfield placement put him.
    if (taker && taker->GetFormationEntry().role == e_PlayerRole_GK &&
        (setPiece == e_SetPiece_GoalKick || setPiece == e_SetPiece_FreeKick)) {
      taker->ResetPosition(
          Vector3(ballPos.coords[0] - team->GetSide() * 1.5f, ballPos.coords[1], 0), ballPos);
    }

    if (setPiece == e_SetPiece_ThrowIn || setPiece == e_SetPiece_KickOff) {
      taker->ResetPosition(ballPos + ballPos.GetNormalized(Vector3(0, -team->GetSide(), 0)) * 0.3f,
                           ballPos);
    } else if (setPiece == e_SetPiece_FreeKick) {
      taker->ResetPosition(ballPos + Vector3(team->GetSide(), 0, 0) * 2.3f, ballPos);
    } else {
      taker->ResetPosition(ballPos + ballPos.GetNormalized(Vector3(0, -team->GetSide(), 0)) * 2.3f,
                           ballPos);
    }
    if (setPiece == e_SetPiece_ThrowIn) {
      taker->SelectRetainAnim();
    }
    if (setPiece == e_SetPiece_Penalty) {
      taker->ResetPosition(ballPos + Vector3(team->GetSide(), 0, 0) * 3.0, ballPos);
    }

  } else
    taker = 0;

  // Law 16, PES's way (choreography 11.3): at a goal kick, and at a free kick
  // in the restarting team's own box, everyone but the kicker and that side's
  // two back roles ends up outside the box being kicked from. This satisfies
  // the referee's own clearing gate on both teams at once, which is what PES
  // does - and what GF used to leave to a walk.
  for (auto* player : players) {
    const e_PlayerRole role = player->GetFormationEntry().role;
    // PES's roles 0 and 1 are the keeper and the centre back; GF splits the
    // full backs off the centre backs, and PES's role 2 (the full back) is
    // pushed out, so only GF's GK and CB may stay in the box.
    const bool keepHim = player == taker ||
                         (isTakerTeam && (role == e_PlayerRole_GK || role == e_PlayerRole_CB));
    pushOutOfBox(player, player->GetPosition(), keepHim);
  }

  // keep distance from ball
  float minDistance = 2.0;
  if (!isTakerTeam)
    minDistance = 5.0;
  for (auto* player : players) {
    if (player != taker) {
      Vector3 toBall = player->GetPosition() - ballPos;
      float ballDistance = (toBall).GetLength();
      if (ballDistance < minDistance)
        player->ResetPosition(player->GetPosition() + toBall.GetNormalized(0) * minDistance, ballPos);
    }
  }
}

void TeamAIController::ApplyAttackingRun(Player* manualPlayer) {
  const float counterAttack =
      liveTeamTactics.GetReal("counter_attack", 0.5f);
  endApplyAttackingRun_ms =
      match->GetActualTime_ms() + AITactics::GetAttackingRunDuration_ms(counterAttack);

  attackingRunPlayer = manualPlayer ? manualPlayer : SelectAttackingRunPlayer(team);
}

void TeamAIController::ApplyTeamPressure() {
  // Gegenpressing keeps hunting the ball for several seconds longer.
  endApplyTeamPressure_ms =
      match->GetActualTime_ms() + 500 + TeamPhilosophy::GetTeamPressureDurationBonus_ms(philosophy);

  Player* opp = match->GetTeam(abs(team->GetID() - 1))->GetBestPossessionPlayer();
  if (!opp) {
    teamPressurePlayer = 0;
    return;
  }
  Vector3 opponentPos = opp->GetPosition() + opp->GetMovement() * 0.24f;

  // Team pressure is assistance: keep the primary defender in place and send
  // the nearest available AI-controlled outfield teammate.
  Player* primaryDefender = team->GetDesignatedTeamPossessionPlayer();
  teamPressurePlayer = 0;
  std::vector<Player*> candidates;
  const Vector3 pressureTarget = opponentPos + Vector3(team->GetSide() * 1.0f, 0, 0);
  AI_GetClosestPlayers(team, pressureTarget, candidates, playerNum);
  float bestPressureRating = 1000.0f;
  for (Player* candidate : candidates) {
    if (candidate != primaryDefender && candidate != team->GetGoalie()) {
      const float distance = (candidate->GetPosition() - pressureTarget).GetLength();
      const float rolePenalty = AITactics::GetSecondaryPressureRolePenalty(
          AI_GetMindSet(candidate->GetDynamicFormationEntry().role));
      // A destroyer hunts the ball; a number ten leaves that to others.
      const float pressureRating =
          distance + rolePenalty +
          PlayingStyles::GetPressingDistanceBias(candidate->GetPlayerData()->GetPlayingStyle());
      if (pressureRating < bestPressureRating) {
        bestPressureRating = pressureRating;
        teamPressurePlayer = candidate;
      }
    }
  }

  // Do not alter the player's persistent man-marking assignment here. The
  // pressure override is intentionally temporary and is already identified by
  // teamPressurePlayer/endApplyTeamPressure_ms in ElizaController.
}

void TeamAIController::ApplyZonePressure() {
  const float pressureSetting =
      liveTeamTactics.GetReal("team_pressure", 0.5f);
  zonePressureActive = true;
  zonePressureEndTime_ms =
      match->GetActualTime_ms() + AITactics::GetZonePressureDuration_ms(pressureSetting);
  ApplyTeamPressure();
  nextZonePressureRefresh_ms = match->GetActualTime_ms() + 250;
}

void TeamAIController::ApplyKeeperRush() {
  endApplyKeeperRush_ms = match->GetActualTime_ms() + 300;
}

void TeamAIController::CalculateSituation() {
  teamHasPossession = team->HasPossession();
  teamHasUniquePossession = team->HasUniquePossession();
  oppTeamHasPossession = match->GetTeam(abs(team->GetID() - 1))->HasPossession();
  oppTeamHasUniquePossession = match->GetTeam(abs(team->GetID() - 1))->HasUniquePossession();
  teamHasBestPossession = match->GetBestPossessionTeamID() == team->GetID();
  teamPossessionAmount = team->GetTeamPossessionAmount();
  fadingTeamPossessionAmount = team->GetFadingTeamPossessionAmount();
  timeNeededToGetToBall = team->GetTimeNeededToGetToBall_ms();
  oppTimeNeededToGetToBall = match->GetTeam(abs(team->GetID() - 1))->GetTimeNeededToGetToBall_ms();
}

void TeamAIController::ApplyFormation(Formations::e_Formation newFormation) {
  ApplyFormationShape(Formations::GetShape(newFormation));
}

Formations::Shape TeamAIController::TeamFormationShape() const {
  std::vector<e_PlayerRole> roles;
  TeamData* teamData = team->GetTeamData();
  for (int i = 0; i < playerNum; i++)
    roles.push_back(teamData->GetFormationEntry(i).role);
  return Formations::ShapeFromRoles(roles);
}

void TeamAIController::ApplyFormationShape(const Formations::Shape& shape) {
  const std::vector<Formations::Slot> layout = Formations::GetLayoutForShape(shape);
  TeamData* teamData = team->GetTeamData();

  for (int i = 0; i < static_cast<int>(layout.size()) && i < playerNum; i++) {
    FormationEntry entry = teamData->GetFormationEntry(i);
    entry.role = layout.at(i).role;
    entry.databasePosition = layout.at(i).position;
    // Same blend the database loader applies, so switching shape mid-match ends
    // up with exactly the positions a fresh load of that shape would give.
    entry.position = entry.databasePosition * 0.6f + GetDefaultRolePosition(entry.role) * 0.4f;
    teamData->SetFormationEntry(i, entry);
  }

  formationShape = Formations::IsValidShape(shape) ? shape : Formations::MakeShape(4, 4, 2);
}

MatchMentality::FormationShape TeamAIController::GetDesperationShape() const {
  const int goalDifference = match->GetMatchData()->GetGoalCount(team->GetID()) -
                             match->GetMatchData()->GetGoalCount(abs(team->GetID() - 1));
  return MatchMentality::GetDesperationShape(goalDifference);
}

void TeamAIController::UpdateTactics() {
  const TeamTactics& teamTactics = team->GetTeamData()->GetTactics();
  TeamTactics& teamTacticsWritable = team->GetTeamData()->GetTacticsWritable();

  const Properties& userTacticsModifiers = teamTactics.userProperties;

  int goals = match->GetMatchData()->GetGoalCount(team->GetID());
  int oppGoals = match->GetMatchData()->GetGoalCount(abs(team->GetID() - 1));

  liveTeamTactics = baseTeamTactics;

  // The team's philosophy bundles the sliders into a style of play; the user's
  // own tactic modifiers are then applied on top of that as offsets. A human
  // manager's choice is respected as-is; for a CPU-managed team the AI manager
  // adapts the style to the situation.
  const TeamPhilosophy::e_Philosophy preferredPhilosophy =
      TeamPhilosophy::Parse(teamTactics.userProperties.Get(
          "philosophy", teamTactics.factoryProperties.Get("philosophy", "balanced")));
  // Likewise the shape to hold: the lineup, not an unreadable property.
  const Formations::Shape preferredShape = TeamFormationShape();

  // The set philosophy and shape stand unless the CPU manager runs this team
  // (MatchSetup::aiManagerRuns).
  if (!match->AIManagerRunsTeam(team->GetID())) {
    philosophy = preferredPhilosophy;
    // A human manager's shape is his own business, however strange it looks.
    if (Formations::ShapeName(preferredShape) != Formations::ShapeName(formationShape))
      ApplyFormationShape(preferredShape);
  } else {
    AIManager::MatchSituation situation;
    situation.goalDifference = goals - oppGoals;
    situation.matchTime_ms = match->GetMatchTime_ms();
    situation.possessionShare =
        1.0f - fabs(match->GetMatchData()->GetPossessionFactor_60seconds() - team->GetID());
    philosophy = AIManager::ChoosePhilosophy(preferredPhilosophy, situation);

    // The CPU manager reshapes the team to chase a game or see one out.
    const Formations::Shape wantedShape = Formations::GetShape(
        AIManager::ChooseFormation(Formations::FromShape(preferredShape), situation));
    if (Formations::ShapeName(wantedShape) != Formations::ShapeName(formationShape))
      ApplyFormationShape(wantedShape);
  }
  TeamPhilosophy::ApplyPreset(philosophy, liveTeamTactics);
  const Properties philosophyTactics = liveTeamTactics;

  // when trailing, we need goals. when leading, defend lead
  float goalFactor = clamp(0.5 + (oppGoals - goals) * 0.25f, 0.0f, 1.0f);
  // time still to play matters - get more desperate towards the end
  float timeFactor = 0.5f + 0.5f * clamp(match->GetMatchTime_ms() / 6300000.0f, 0.0f, 1.0f);
  // printf("timefactor: %f\n", timeFactor);

  float offenseBias = clamp(0.5f + (goalFactor - 0.5f) * (timeFactor * 1.0f), 0.0f, 1.0f);
  // todo: add skills/opp skills and difficulty as factors
  // todo: add off/def slider/tactics
  // if (team->GetHumanGamerCount() == 0) offenseBias = offenseBias * 0.8f + 0.2f; // let's assume
  // human gamers want more offensive play
  //                                     offenseBias = offenseBias * 0.8f + 0.2f; // let's assume AI
  //                                     gamers want more defensive play

  float possessionFactor = match->GetMatchData()->GetPossessionFactor_60seconds();
  float recentPossessionBias = 1.0f - fabs(possessionFactor - team->GetID());
  // if (team->GetID() == 0) printf("team 0 recentPossessionBias: %f (possessionFactor: %f)\n",
  // recentPossessionBias, possessionFactor); if (team->GetID() == 1) printf("team 1
  // recentPossessionBias: %f (possessionFactor: %f)\n", recentPossessionBias, possessionFactor);

  offensivenessBias = offenseBias * 0.5f + recentPossessionBias * 0.5f;
  // if (team->GetID() == 0) printf("T1 offensivenessBias: %f\n", offensivenessBias);
  // if (team->GetID() == 1) printf("T2 offensivenessBias: %f\n", offensivenessBias);

  // autoBias = clamp(fabs(offenseBias - 0.5f) * 2.0f, 0.0f, 1.0f); // don't use too much autobias
  // when things are calm (= offenseBias being around 0.5)

  const map_Properties* userMods = userTacticsModifiers.GetProperties();
  map_Properties::const_iterator iter = userMods->begin();
  while (iter != userMods->end()) {
    float multiplier = teamTacticsModMultipliers.GetReal(iter->first.c_str(), 0.0f);

    float userOffset = atof(iter->second.c_str());
    float offset = userOffset;

    // feed back to teamdata (todo: hasn't this system been effectively disabled? if so, delete it
    // to avoid confusion)
    // teamTacticsWritable.liveProperties.Set(iter->first.c_str(), offset);

    offset = (offset - 0.5f) * 2.0f * multiplier;

    float baseValue = philosophyTactics.GetReal(iter->first.c_str(), -1.0f);
    // printf("value: %s\n", iter->first.c_str());
    if (baseValue >= 0.0f) {  // not all user mods from teamdata are used in this class (for
                              // example, individual settings like dribble stuff), ignore them
      liveTeamTactics.Set(iter->first.c_str(), clamp(baseValue + offset, 0.0f, 1.0f));
    }
    iter++;
  }

  // The manager's own instructions from the touchline, on top of everything the
  // philosophy and his tactic sliders produced.
  TeamInstructions::Apply(instructions, liveTeamTactics);

  // Late-game mentality shift, summed onto whatever the manager's own tactics
  // produced as momentum - it never takes them over.
  mentality = MatchMentality::Decide(goals - oppGoals, match->GetMatchTime_ms());
  if (mentality != MatchMentality::e_Mentality_Normal) {
    offensivenessBias = MatchMentality::ApplyMomentum(offensivenessBias, mentality, 1.0f);
    liveTeamTactics.Set(
        "position_offense_depth_factor",
        MatchMentality::ApplyMomentum(
            liveTeamTactics.GetReal("position_offense_depth_factor", 0.9f), mentality, 1.0f));
    // The defensive line follows the same swing, but more cautiously.
    liveTeamTactics.Set(
        "position_defense_depth_factor",
        MatchMentality::ApplyMomentum(
            liveTeamTactics.GetReal("position_defense_depth_factor", 0.75f), mentality, 0.5f));
  }
}

SetPieceParams TeamAIController::GetSetPieceParams(e_SetPiece setPiece) {
  return SetPieceConfig::Load(team->GetTeamData()->GetDatabaseID(), setPiece);
}

void TeamAIController::Reset() {
  match->GetMatchData()->RecordPassRestart();
  taker = 0;

  setPieceType = e_SetPiece_None;

  offsideTrapX = 0;

  endApplyAttackingRun_ms = 0;
  attackingRunPlayer = 0;
  endApplyTeamPressure_ms = 0;
  teamPressurePlayer = 0;
  endApplyKeeperRush_ms = 0;

  zonePressureActive = false;
  zonePressureEndTime_ms = 0;
  nextZonePressureRefresh_ms = 0;
  prevTeamHasPossession = false;

  teamHasPossession = false;
  teamHasUniquePossession = false;
  oppTeamHasPossession = false;
  oppTeamHasUniquePossession = false;
  teamHasBestPossession = false;
  teamPossessionAmount = 1.0f;
  fadingTeamPossessionAmount = 1.0f;
  timeNeededToGetToBall = 100;
  oppTimeNeededToGetToBall = 100;
}

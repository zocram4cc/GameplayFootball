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

#ifndef _HPP_TEAM_AICONTROLLER
#define _HPP_TEAM_AICONTROLLER

#include "../data/setpiececonfig.hpp"
#include "../gamedefines.hpp"
#include "base/properties.hpp"
#include "../data/formations.hpp"
#include "matchmentality.hpp"
#include "teaminstructions.hpp"
#include "teamphilosophy.hpp"

class Match;
class Team;

struct TacticalOpponentInfo {
  Player* player;
  float dangerFactor;
};

bool SortTacticalOpponentInfo(const TacticalOpponentInfo& a, const TacticalOpponentInfo& b);

class TeamAIController {
public:
  TeamAIController(Team* team);
  virtual ~TeamAIController();

  void Process();

  Vector3 GetAdaptedFormationPosition(Player* player, bool useDynamicFormationPosition = true);
  void CalculateDynamicRoles();
  float CalculateMarkingQuality(Player* player, Player* opp);
  void CalculateManMarking();
  void ApplyOffsideTrap(Vector3& position) const;
  float GetOffsideTrapX() const { return offsideTrapX; }
  void PrepareSetPiece(e_SetPiece setPiece, int takerTeamID = -1);
  SetPieceParams GetSetPieceParams(e_SetPiece setPiece);
  Player* GetPieceTaker() { return taker; }
  // Overrides the taker the set-piece preparation picked by proximity. The
  // shootout needs its own rotation to take the kicks.
  void SetPieceTaker(Player* player) { taker = player; }
  e_SetPiece GetSetPieceType() { return setPieceType; }
  void ApplyAttackingRun(Player* manualPlayer = 0);
  void ApplyTeamPressure();
  void ApplyZonePressure();
  void ApplyKeeperRush();
  void RequestPass(Player* target);
  Player* ConsumePassRequest(Player* passer);
  void CalculateSituation();

  void UpdateTactics();

  const Formations::Shape& GetFormationShape() const { return formationShape; }
  // Writes a shape's roles and positions into the team's formation slots. Any
  // band split totalling ten is accepted, however unorthodox.
  void ApplyFormationShape(const Formations::Shape& shape);
  void ApplyFormation(Formations::e_Formation newFormation);

  // Instructions the manager has set from the touchline.
  TeamInstructions::State& GetInstructions() { return instructions; }
  const TeamInstructions::State& GetInstructions() const { return instructions; }

  TeamPhilosophy::e_Philosophy GetPhilosophy() const { return philosophy; }
  MatchMentality::e_Mentality GetMentality() const { return mentality; }
  // Outfield shape the team wants while chasing the game.
  MatchMentality::FormationShape GetDesperationShape() const;
  float GetStaminaDrainMultiplier() const {
    return TeamPhilosophy::GetStaminaDrainMultiplier(philosophy);
  }

  unsigned long GetEndApplyAttackingRun_ms() { return endApplyAttackingRun_ms; }
  Player* GetAttackingRunPlayer() { return attackingRunPlayer; }

  unsigned long GetEndApplyTeamPressure_ms() { return endApplyTeamPressure_ms; }
  Player* GetTeamPressurePlayer() { return teamPressurePlayer; }

  bool IsZonePressureActive() const { return zonePressureActive; }
  unsigned long GetZonePressureEndTime_ms() const { return zonePressureEndTime_ms; }

  Player* GetForwardSupportPlayer() { return forwardSupportPlayer; }

  unsigned long GetEndApplyKeeperRush_ms() { return endApplyKeeperRush_ms; }

  const std::vector<TacticalOpponentInfo>& GetTacticalOpponentInfo() {
    return tacticalOpponentInfo;
  }

  void Reset();

protected:
  Match* match;
  Team* team;
  Player* taker;
  e_SetPiece setPieceType;

  Properties baseTeamTactics;
  Properties teamTacticsModMultipliers;
  Properties liveTeamTactics;

  Formations::Shape formationShape;
  TeamInstructions::State instructions;
  TeamPhilosophy::e_Philosophy philosophy;
  MatchMentality::e_Mentality mentality;

  float offensivenessBias;

  bool teamHasPossession;
  bool teamHasUniquePossession;
  bool oppTeamHasPossession;
  bool oppTeamHasUniquePossession;
  bool teamHasBestPossession;
  float teamPossessionAmount;
  float fadingTeamPossessionAmount;
  int timeNeededToGetToBall;
  int oppTimeNeededToGetToBall;

  float depth;
  float width;

  float offsideTrapX;

  unsigned long endApplyAttackingRun_ms;
  Player* attackingRunPlayer;
  unsigned long endApplyTeamPressure_ms;
  Player* teamPressurePlayer;
  unsigned long endApplyKeeperRush_ms;

  Player* passRequestTarget;
  unsigned long passRequestExpires_ms;
  void ClearPassRequest();

  bool zonePressureActive;
  unsigned long zonePressureEndTime_ms;
  unsigned long nextZonePressureRefresh_ms;
  bool prevTeamHasPossession;

  Player* forwardSupportPlayer;  // sort of like the attacking run player, but more for a forward
                                 // offset for a player close to the action, to support the player
                                 // in possession

  std::vector<TacticalOpponentInfo> tacticalOpponentInfo;
};

#endif

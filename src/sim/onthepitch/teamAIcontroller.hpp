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

#include <array>
#include <map>
#include <string>
#include <vector>

#include "../data/formations.hpp"
#include "../data/setpiececonfig.hpp"
#include "../gamedefines.hpp"
#include "base/properties.hpp"
#include "pesrandom.hpp"
#include "pesstrategy.hpp"
#include "pesdefshape.hpp"
#include "teaminstructions.hpp"
#include "pesteamconcepts.hpp"
#include "pespress.hpp"

class Match;
class Team;

struct TacticalOpponentInfo {
  Player* player;
  float dangerFactor;
};

bool SortTacticalOpponentInfo(const TacticalOpponentInfo& a, const TacticalOpponentInfo& b);
namespace pes {
// Per-team presser state the press picker (pespress.hpp) reads and the tick
// writes. One owner: TeamAIController. Men are side-local indices 0..10 in
// each side's block (the picker's `g = side*11 + i`). Only the fields the
// picker reads are here; the rest of PES's T+0xa2a0 record lands with the
// writer.
struct TeamPressState {
  // Role-table entry per man (PES T+0xa2a0): kPresserRole marks the presser.
  int role[22] = {};
  // Per-man recent-action stamps (PES T+0xa2cc + k*0x34, FUN_140a40200).
  std::array<PressStampRing, 22> actionRing{};
  // The four press slots, as man indices; -1 when empty (PES T+0xa548).
  int pressSlot[4] = {-1, -1, -1, -1};

  // FUN_140a42940 (action part): store the man's action and stamp it at time `t`.
  // ponytail: the 0x40-byte per-man record it also clears (PES T+0x28c) has no GF
  // reader yet; add it when the writer's tail reads that record.
  void SetAction(int g, int action, uint32_t t) {
    role[g] = action;
    PressStampPush(actionRing[g], action, t);
  }
};
}  // namespace pes

class TeamAIController {
public:
  TeamAIController(Team* team);
  virtual ~TeamAIController();

  void Process();

  Vector3 GetAdaptedFormationPosition(Player* player, bool useDynamicFormationPosition = true);
  void CalculateDynamicRoles();
  void CalculateManMarking();
  // FUN_140623750's mark spot (report 14.3): where a marker stands, goal-side of
  // his opponent.
  Vector3 GetMarkSpot(Player* defender, Player* opp);
  // PES 2021's open-play defensive line (docs/re/pes-re-defensive-shape.md 6,
  // 14.1), rebuilt every tick in Process(): the back line's height in world x,
  // and the metres of length the team covers from it.
  float GetDefensiveLineHeight() const { return defensiveLineHeight; }
  float GetDefensiveTeamLength() const { return defensiveTeamLength; }
  // The presser state the press picker writes and OffBallPort reads.
  pes::TeamPressState& GetPressState() { return pressState; }
  const pes::TeamPressState& GetPressState() const { return pressState; }
  // PES arms the offside trap only on a team instruction (its `T+0xe4` flag,
  // report 14.4). GF has no such instruction: this reads the tactic that stands
  // in for it and defaults to off, PES's live value for both teams.
  bool IsOffsideTrapArmed() const;
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
  void CalculateSituation();

  // PES 2021's open-play defensive line, rebuilt every tick (report 6, 14.1).
  void CalculateDefensiveShape();

  void UpdateTactics();

  const Formations::Shape& GetFormationShape() const { return formationShape; }
  // Writes a shape's roles and positions into the team's formation slots. Any
  // band split totalling ten is accepted, however unorthodox.
  void ApplyFormationShape(const Formations::Shape& shape);
  // The shape the team is lined up in, counted off its own roles.
  Formations::Shape TeamFormationShape() const;
  void ApplyFormation(Formations::e_Formation newFormation);

  // Instructions the manager has set from the touchline.
  TeamInstructions::State& GetInstructions() { return instructions; }
  const TeamInstructions::State& GetInstructions() const { return instructions; }

  // The live value of a tactic slider after the whole pipeline - base
  // defaults and user modifiers.
  // Every gameplay read of a tactic belongs here; the raw team data bypasses
  // everything the manager is doing.
  float GetLiveTacticReal(const char* name, float defaultValue) const {
    return liveTeamTactics.GetReal(name, defaultValue);
  }
  // The whole live tactic set, for state read-back (remote control panel,
  // debug overlays). Values only mutate on this thread's UpdateTactics.
  const Properties& GetLiveTactics() const { return liveTeamTactics; }

  // PES's team sheet (docs/re/pes-re-tactics.md 1): the sliders and toggles
  // the AI reads, from the team's tactics with the install's sheet defaults.
  const pes::TeamSheet& GetTeamSheet() const { return teamSheet; }
  // A team concept's order for one man (T+0xb748/b774/26c, transitions 2-3):
  // the concept id and the point it sends him to. Null when he has none.
  struct ConceptOrder {
    int conceptId;
    Vector3 target;
  };
  const ConceptOrder* GetConceptOrder(const Player* player) const;
  // PES's PRESS state, the block-radius input (defender decisions): while the
  // team's ForeCheckPress runs, its MATCH_UP man presses.
  bool IsPressing(const Player* player) const;
  // PES's strategy word T+0x134 (docs/re/pes-re-match-management.md 1).
  int GetStrategy() const { return strategy.strategy; }
  // Every team tick, dead balls included: the decider moves the strategy while
  // the ball is dead and the attack level follows it once play resumes.
  void UpdateStrategy();

  unsigned long GetEndApplyAttackingRun_ms() { return endApplyAttackingRun_ms; }
  Player* GetAttackingRunPlayer() { return attackingRunPlayer; }

  Player* GetForwardSupportPlayer() { return forwardSupportPlayer; }

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
  pes::TeamSheet teamSheet;
  // PES's concept selector state (FUN_140579840): running flag and start time
  // per id, the men each concept has ordered, and its own record.
  std::array<bool, pes::kConceptCount> conceptRunning{};
  std::array<unsigned long, pes::kConceptCount> conceptStart_ms{};
  std::map<const Player*, ConceptOrder> conceptOrders;
  Player* chaseChaser = nullptr;
  Player* chaseSecond = nullptr;
  Player* chaseCarrier = nullptr;
  Player* counterRunner = nullptr;
  std::array<Player*, 3> counterSupporters{};
  // MI+0x11a0 (frames since the possessing team changed) and FUN_140a74ec0
  // (in-play time since a kick-off restart), in GF time.
  unsigned long possessionChange_ms = 0;
  bool lastRestartWasKickOff = false;
  unsigned long inPlaySinceRestart_ms = 0;
  unsigned long lastProcess_ms = 0;
  // FUN_140687820's measured back/midfield lines (T+0x89a0/a4, world x) and the
  // bands they are compared with (T+0x875c/8770, own frame), this tick.
  float measuredBackLine = 0, measuredMidLine = 0, backBand = 0, midBand = 0;
  // PES 2021's CPU strategy decider (pesstrategy.hpp): its state and its own
  // generator.
  pes::StrategyState strategy;
  std::optional<pes::Random> strategyRng;

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

  // PES 2021's open-play defensive shape (docs/re/pes-re-defensive-shape.md 6, 7,
  // 14.1): the pitch facts the chain reads, the line height it produces this
  // tick and the length the team covers, all in world terms.
  pes::ShapeField defensiveShapeField;
  float defensiveLineHeight;
  float defensiveTeamLength;
  pes::TeamPressState pressState;
  // The PES slider readings the shape uses this tick (report 14.6), mapped from
  // GF's live tactics; the band code reads them so the line and its width come
  // from one place.
  int defensiveStyle;
  int defensiveAttackLevel;
  float defensiveLineSlider;
  float defensiveCompactSlider;
  // PES's Pressuring option (team-sheet option byte 6, report 14.3): the marker
  // keeps 2 m more room.
  bool defensivePressing;
  // Last tick's own-frame line height, PES's f7a00 input (`prevFinal`).
  float previousLineHeight;
  // PES's per-slot mark state (report 3.3): the opponent a defender is switching
  // to and the tick that switch started pending, so a hand-off commits only
  // after the organiser's delay.
  std::vector<int> markPendingOpponent;
  std::vector<unsigned long> markPendingSinceTick;

  unsigned long endApplyAttackingRun_ms;
  Player* attackingRunPlayer;
  bool prevTeamHasPossession;

  void RunConcepts();
  void StopConcept(int id);
  bool StartConcept(int id);
  bool ConceptEnded(int id);
  void ExecuteConcept(int id);
  pes::TransitionChaseInput ChaseInput(Player* carrier, std::vector<Player*>* men);
  pes::SideCounterInput CounterInput(std::vector<Player*>* men);
  pes::DefenceConceptInput DefenceInput();
  float MeasuredLine(int line, float ref) const;
  Player* forwardSupportPlayer;  // sort of like the attacking run player, but more for a forward
                                 // offset for a player close to the action, to support the player
                                 // in possession

  std::vector<TacticalOpponentInfo> tacticalOpponentInfo;
};

#endif

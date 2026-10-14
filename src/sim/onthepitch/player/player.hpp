// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_PLAYER
#define _HPP_PLAYER

#include "humanoid/humanoid.hpp"
#include "playerbase.hpp"
#include "../../data/formstate.hpp"
#include "../pescontact.hpp"
#include "../pesrandom.hpp"

#include <memory>

class Match;
class Team;
class ElizaController;

struct TacticalPlayerSituation {
  float forwardSpaceRating;
  float toGoalSpaceRating;
  float spaceRating;
  float forwardRating;
};

class Player : public PlayerBase {
public:
  Player(Team* team, PlayerData* playerData);
  virtual ~Player();

  Humanoid* CastHumanoid();
  ElizaController* CastController();

  // PES's per-match modifiers (FormState): the condition arrow this player was
  // dealt at kick-off, and his familiarity with the formation slot he is
  // DEPLOYED at - the slot, not where he is standing (docs/21Research.md,
  // Arrays 2 and 3). Both multiply into GetStat and clamp at the ceiling.
  void SetConditionArrow(FormState::Arrow arrow) { conditionArrow = arrow; }
  FormState::Arrow GetConditionArrow() const { return conditionArrow; }
  FormState::Slot GetDeployedSlot() const;
  FormState::Familiarity GetDeployedFamiliarity() const;

  int GetTeamID() const;
  Team* GetTeam();

  // get ready for some action
  virtual void Activate(boost::intrusive_ptr<Node> humanoidSourceNode,
                        std::shared_ptr<AnimCollection> animCollection);
  // go back to bench/take a shower
  virtual void Deactivate();

  bool TouchPending() { return CastHumanoid()->TouchPending(); }
  bool TouchAnim() { return CastHumanoid()->TouchAnim(); }
  Vector3 GetTouchPos() { return CastHumanoid()->GetTouchPos(); }
  int GetTouchFrame() { return CastHumanoid()->GetTouchFrame(); }
  int GetCurrentFrame() { return CastHumanoid()->GetCurrentFrame(); }

  void SelectRetainAnim() { CastHumanoid()->SelectRetainAnim(); }

  inline e_FunctionType GetCurrentFunctionType() {
    return CastHumanoid()->GetCurrentFunctionType();
  }
  FormationEntry GetFormationEntry();
  inline void SetDynamicFormationEntry(FormationEntry entry) { dynamicFormationEntry = entry; }
  inline FormationEntry GetDynamicFormationEntry() { return dynamicFormationEntry; }
  inline void SetManMarkingID(int id) { manMarkingID = id; }
  inline int GetManMarkingID() { return manMarkingID; }

  bool HasPossession() const;
  bool HasBestPossession() const;
  bool HasUniquePossession() const;
  inline int GetPossessionDuration_ms() const { return possessionDuration_ms; }
  inline int GetTimeNeededToGetToBall_ms() const { return timeNeededToGetToBall_ms; }
  inline int GetTimeNeededToGetToBall_optimistic_ms() const {
    return timeNeededToGetToBall_optimistic_ms;
  }
  inline int GetTimeNeededToGetToBall_previous_ms() const {
    return timeNeededToGetToBall_previous_ms;
  }
  void SetDesiredTimeToBall_ms(int ms) { desiredTimeToBall_ms = ms; }
  int GetDesiredTimeToBall_ms() const {
    return std::clamp(desiredTimeToBall_ms, static_cast<int>(timeNeededToGetToBall_ms), 1000000);
  }
  bool AllowLastDitch(bool includingPossessionAmount = true) const;

  void TriggerControlledBallCollision() { triggerControlledBallCollision = true; }
  bool IsControlledBallCollisionTriggered() { return triggerControlledBallCollision; }
  void ResetControlledBallCollisionTrigger() { triggerControlledBallCollision = false; }

  float GetAverageVelocity(float timePeriod_sec);  // is reset on ResetSituation() calls

  void UpdatePossessionStats(bool onInterval = true);

  float GetClosestOpponentDistance() const;

  // PES's per-player math::Random (player+0x32d0; docs/re/pes-re-rng-slots.md
  // 6, 9.2): every per-player draw (kick error, success rolls, picks) is its
  // slot 2. Built on first use from the match's generator constants.
  pes::Random& GetPesRandom();

  const TacticalPlayerSituation& GetTacticalSituation() { return tacticalSituation; }

  virtual void Process();

  void GiveYellowCard(unsigned long giveTime_ms) {
    cards++;
    cardEffectiveTime_ms = giveTime_ms;
  }
  void GiveRedCard(unsigned long giveTime_ms) {
    cards += 3;
    cardEffectiveTime_ms = giveTime_ms;
  }
  int GetCards() const { return cards; }

  // One save decision per incoming shot, rolled from the keeper's reaction stat
  // and shared by the dive animation and the body-collision pass, so a keeper
  // who has been beaten is beaten everywhere.
  bool KeeperAttemptsSave();
  // Only a genuine shot can beat him; he still collects crosses and slow balls.
  bool IsBeatenKeeper();

  // Wet/worn pitch slips (proposal 4A): pace lost for a moment after going down.
  float GetSlipVelocityMultiplier() const;
  bool IsSlipping() const;
  virtual float GetSlipFactor() const { return GetSlipVelocityMultiplier(); }

  void SendOff();

  virtual float GetStat(const char* name) const;

  virtual void ResetSituation(const Vector3& focusPos);

  // Which PES challenge an Interfere clip stands for (pes-re-defender-
  // decisions.md 1): TACKLE, BLOCK or JOSTLE; the contact reaction reads it.
  void SetPesChallenge(pes::ContactAction action) { pesChallenge = action; }
  pes::ContactAction GetPesChallenge() const { return pesChallenge; }

protected:
  pes::ContactAction pesChallenge = pes::ContactAction::kTackle;
  FormState::Arrow conditionArrow = FormState::Arrow::Normal;
  void _CalculateTacticalSituation();

  Team* team;

  signed int manMarkingID;

  FormationEntry dynamicFormationEntry;

  bool hasPossession;
  bool hasBestPossession;
  bool hasUniquePossession;
  int possessionDuration_ms;

  // Slip bookkeeping.
  unsigned long lastSlipTime_ms = 0;
  // (Removed: the old per-shot save roll latched here. KeeperAttemptsSave is
  // now the geometry of the save, evaluated per call, so there is no roll to
  // latch.)
  // The beaten-keeper census DOES latch, for a different reason: the save
  // geometry is consulted several times a tick (the dive decision and the
  // body-collision pass both ask), so counting every call would count ticks,
  // not shots. Cleared the moment nothing is coming at him.
  bool keeperBeatenRecorded = false;
  Vector3 previousDirectionVec;
  unsigned int timeNeededToGetToBall_ms;
  unsigned int timeNeededToGetToBall_optimistic_ms;
  unsigned int timeNeededToGetToBall_previous_ms;

  bool triggerControlledBallCollision;

  TacticalPlayerSituation tacticalSituation;

  int desiredTimeToBall_ms;
  Vector3 idealMovement;

  int cards;  // 1 == 1 yellow; 2 == 2 yellow; 3 == 1 red; 4 == 1 yellow, 1 red

  unsigned long cardEffectiveTime_ms;

  std::unique_ptr<pes::Random> pesRandom;
};

#endif

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_PLAYERBASE
#define _HPP_PLAYERBASE

#include "../../data/playerdata.hpp"
#include "controller/icontroller.hpp"
#include "humanoid/humanoidbase.hpp"
#include "scene/scene3d/node.hpp"

// PES 2021's stamina model: the constants the inline getters below call
#include "../pesstamina.hpp"

class Match;

class PlayerBase {
public:
  PlayerBase(Match* match, PlayerData* playerData);
  virtual ~PlayerBase();

  int GetID() const { return id; }
  PlayerData* GetPlayerData() { return playerData; }

  bool IsActive() { return isActive; }

  // get ready for some action
  virtual void Activate(boost::intrusive_ptr<Node> humanoidSourceNode,
                        std::shared_ptr<AnimCollection> animCollection) = 0;
  // go back to bench/take a shower
  virtual void Deactivate();

  void ResetPosition(const Vector3& newPos, const Vector3& focusPos) {
    humanoid->ResetPosition(newPos, focusPos);
  }
  void OffsetPosition(const Vector3& offset) { humanoid->OffsetPosition(offset); }

  inline int GetFrameNum() { return humanoid->GetFrameNum(); }
  inline int GetFrameCount() { return humanoid->GetFrameCount(); }

  inline Vector3 GetPosition() const { return humanoid->GetPosition(); }
  // defense-in-depth: the graphics put once caught a substitute mid-Activate
  // (isActive already true, humanoid not yet built) and crashed here; the
  // substitution lock now prevents that, but never dereference a null humanoid
  inline Vector3 GetGeomPosition() const {
    return humanoid ? humanoid->GetGeomPosition() : Vector3(0);
  }
  inline Vector3 GetDirectionVec() const { return humanoid->GetDirectionVec(); }
  inline Vector3 GetBodyDirectionVec() const { return humanoid->GetBodyDirectionVec(); }
  inline Vector3 GetMovement() const { return humanoid->GetMovement(); }
  inline radian GetAngle() const { return humanoid->GetAngle(); }
  inline radian GetRelBodyAngle() const { return humanoid->GetRelBodyAngle(); }
  inline e_Velocity GetEnumVelocity() const { return humanoid->GetEnumVelocity(); }
  inline float GetFloatVelocity() const { return EnumToFloatVelocity(humanoid->GetEnumVelocity()); }
  inline e_FunctionType GetCurrentFunctionType() const {
    return humanoid->GetCurrentFunctionType();
  }
  inline e_FunctionType GetPreviousFunctionType() const {
    return humanoid->GetPreviousFunctionType();
  }

  void TripMe(const Vector3& tripVector, int tripType) { humanoid->TripMe(tripVector, tripType); }

  void RequestCommand(PlayerCommandQueue& commandQueue);
  IController* GetController();

  void SetDebug(bool state);
  bool GetDebug() const;

  boost::intrusive_ptr<Node> GetHumanoidNode() { return humanoid->GetHumanoidNode(); }

  float GetDecayingPositionOffsetLength() { return humanoid->GetDecayingPositionOffsetLength(); }

  virtual void Process();
  virtual void PreparePutBuffers(unsigned long snapshotTime_ms);
  virtual void FetchPutBuffers(unsigned long putTime_ms);
  void Put();

  virtual float GetStat(const char* name) const;
  float GetVelocityMultiplier() const;
  // 1 unless the player is recovering from a slip; overridden by Player.
  virtual float GetSlipFactor() const { return 1.0f; }
  float GetMaxVelocity() const;

  const Anim* GetCurrentAnim() { return humanoid->GetCurrentAnim(); }
  const Anim* GetPreviousAnim() { return humanoid->GetPreviousAnim(); }

  void SetLastTouchTime_ms(unsigned long touchTime_ms) { this->lastTouchTime_ms = touchTime_ms; }
  unsigned long GetLastTouchTime_ms() { return lastTouchTime_ms; }
  void SetLastTouchType(e_TouchType touchType) { this->lastTouchType = touchType; }
  e_TouchType GetLastTouchType() { return lastTouchType; }
  float GetLastTouchBias(int decay_ms, unsigned long time_ms = 0);

  const NodeMap& GetNodeMap() { return humanoid->GetNodeMap(); }

  // The Match's stamina constants, copied at kick-off so the inline getters
  // read the model's numbers and not a default-built struct.
  void SetStaminaConstants(const pes::StaminaConstants& constants) { matchConstants = constants; }

  // PES 2021's stamina (docs/re/pes-re-stamina-drain.md): a tank that drains
  // while the ball is in play, a short-term dash reserve, and the fatigue
  // factor they produce on the ten stats PES writes it on. The old GF
  // distance-based `fatigueFactorInv` is replaced by these.
  const pes::StaminaState& GetStamina() const { return stamina; }
  pes::StaminaState& GetStamina() { return stamina; }
  // The A5 factor: 0.8 + 0.2 * clamp((pct - 10) / 60, 0, 1).
  float GetFatigueFactor() const { return pes::FatigueFactor(matchConstants, stamina); }
  // The breath penalty, integer points subtracted from the breath-sensitive
  // stats (report 4).
  int GetBreathPenalty() const { return pes::BreathPenalty(matchConstants, stamina); }
  // The tank's remaining fraction: 1 rested, 0 empty. The AI reads it the way
  // it read the old `fatigueFactorInv`.
  float GetStaminaPct() const {
    return stamina.max == 0 ? 1.0f
                            : clamp(static_cast<float>(stamina.cur) / stamina.max, 0.0f, 1.0f);
  }

  float GetConfidenceFactor() const { return confidenceFactor; }

  // Injury level: PES's 0 clear / 2 knock / 3 injured, accumulated from
  // contacts, scaled onto the 0..1 range the AI manager's threshold reads.
  float GetInjuryLevel() const {
    return clamp(static_cast<float>(stamina.injuryLevel) / 3.0f, 0.0f, 1.0f);
  }

  float GetAverageStat() { return averageStat; }

  virtual void ResetSituation(const Vector3& focusPos);

protected:
  Match* match;

  PlayerData* playerData;
  const int id;

  bool debug;

  HumanoidBase* humanoid;
  IController* controller;

  bool isActive;

  unsigned long lastTouchTime_ms;
  e_TouchType lastTouchType;

  float confidenceFactor;
  // PES's stamina constants, copied from the MatchSetup once so the inline
  // getters above do not need a pointer back to the Match.
  pes::StaminaConstants matchConstants;
  pes::StaminaState stamina;

  float averageStat;

  std::vector<Vector3> positionHistoryPerSecond;  // resets too (on ResetSituation() calls)
};

#endif

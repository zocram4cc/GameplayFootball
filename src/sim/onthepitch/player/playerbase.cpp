// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "simcontext.hpp"
#include "../../utils.hpp"
#include "../match.hpp"
#include "base/geometry/triangle.hpp"
#include "controller/elizacontroller.hpp"
#include "controller/strategies/strategy.hpp"
#include "player.hpp"

PlayerBase::PlayerBase(Match* match, PlayerData* playerData)
    : match(match),
      playerData(playerData),
      id(match->AllocatePlayerID()),
      humanoid(nullptr),
      controller(nullptr),
      isActive(false) {
  debug = false;
  lastTouchTime_ms = 0;
  lastTouchType = e_TouchType_None;
  fatigueFactorInv = 1.0;
  confidenceFactor = 1.0;
  injuryLevel = 0.0f;

  averageStat = playerData->GetAverageStat();

  // if (Verbose()) printf("player '%s' has an average stat of %f\n",
  // playerData->GetLastName().c_str(), averageStat);
  Log(e_Notice, "PlayerBase", "PlayerBase",
      "player '" + playerData->GetLastName() + "' has an average stat of " +
          real_to_str(averageStat));
}

PlayerBase::~PlayerBase() {
  if (Verbose())
    printf("exiting playerbase.. ");
  if (isActive)
    Deactivate();
  if (Verbose())
    printf("deleting humanoid.. ");
  if (humanoid)
    delete humanoid;
  if (Verbose())
    printf("done\n");
}

void PlayerBase::Deactivate() {
  ResetSituation(GetPosition());

  isActive = false;

  delete controller;
  controller = nullptr;
}

IController* PlayerBase::GetController() {
  return controller;
}

void PlayerBase::RequestCommand(PlayerCommandQueue& commandQueue) {
  controller->RequestCommand(commandQueue);
}

void PlayerBase::SetDebug(bool state) {
  debug = state;
}

bool PlayerBase::GetDebug() const {
  if (IsReleaseVersion())
    return false;
  else
    return debug;
}

void PlayerBase::Process() {
  if (isActive) {
    controller->Process();
    humanoid->Process();
  }
  // if (debug) printf("::%f velo\n", GetMovement().GetLength());
}

void PlayerBase::PreparePutBuffers(unsigned long snapshotTime_ms) {
  humanoid->PreparePutBuffers(snapshotTime_ms);
}

void PlayerBase::FetchPutBuffers(unsigned long putTime_ms) {
  humanoid->FetchPutBuffers(putTime_ms);
}

void PlayerBase::Put() {
  humanoid->Put();
}

float PlayerBase::GetStat(const char* name) const {
  return playerData->GetStat(name);
}

float PlayerBase::GetMaxVelocity() const {
  // fatigue reduces sprint speed: fully rested = 100%, exhausted = 60%
  const float fatigueFactor = 0.6f + 0.4f * fatigueFactorInv;
  return sprintVelocity * GetVelocityMultiplier() * fatigueFactor * GetSlipFactor();
}

float PlayerBase::GetVelocityMultiplier() const {
  // see humanoid_utils' physics function
  return 0.9f + GetStat("physical_velocity") * 0.1f;
}

float PlayerBase::GetLastTouchBias(int decay_ms, unsigned long time_ms) {
  unsigned long adaptedTime_ms = time_ms;
  if (time_ms == 0)
    adaptedTime_ms = match->GetActualTime_ms();
  if (decay_ms > 0)
    return 1.0f - clamp((adaptedTime_ms - GetLastTouchTime_ms()) / (float)decay_ms, 0.0f, 1.0f);
  return 0.0f;
}

void PlayerBase::ResetSituation(const Vector3& focusPos) {
  positionHistoryPerSecond.clear();
  lastTouchTime_ms = 0;
  lastTouchType = e_TouchType_None;
  injuryLevel = 0.0f;
  if (IsActive())
    humanoid->ResetSituation(focusPos);
  if (GetController())
    GetController()->Reset();
}

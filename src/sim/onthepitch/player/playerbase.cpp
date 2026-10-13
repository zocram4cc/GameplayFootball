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
  confidenceFactor = 1.0;
  // The stamina state is filled at kick-off (Match deals the constants and
  // the tank); until then it is empty and the inline getters read 0.
  stamina = pes::StaminaState{};

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
  // Fatigue reduces sprint speed: PES's A5 factor is 1.0 at a full tank and
  // 0.8 when the tank empties (docs/re/pes-re-stamina-drain.md 4), so the
  // rested-to-spent range is 100%..80% of the player's own top speed.
  return sprintVelocity * GetVelocityMultiplier() * GetFatigueFactor() *
         GetSlipFactor();
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
  if (IsActive())
    humanoid->ResetSituation(focusPos);
  if (GetController())
    GetController()->Reset();
}

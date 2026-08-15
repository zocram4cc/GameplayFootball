#include "offsiderule.hpp"

namespace OffsideRule {

bool CanCreateOffside(e_SetPiece restart) {
  return restart != e_SetPiece_ThrowIn && restart != e_SetPiece_GoalKick &&
         restart != e_SetPiece_Corner;
}

bool ShouldSnapshot(bool inPlay, bool restartQueued, e_SetPiece queuedRestart) {
  if (!inPlay)
    return false;
  if (restartQueued && !CanCreateOffside(queuedRestart))
    return false;
  return true;
}

bool IsDeliberatePlay(e_TouchType touchType) {
  return touchType == e_TouchType_Intentional_Kicked;
}

bool TouchResetsPhase(bool opponentOfFlagged, e_TouchType touchType) {
  if (!opponentOfFlagged)
    return true;
  return IsDeliberatePlay(touchType);
}

blunted::Vector3 RestartPosition(const blunted::Vector3& positionAtPass,
                                 const blunted::Vector3& positionAtInvolvement) {
  (void)positionAtPass;  // recorded to judge the offence, not to restart from
  return positionAtInvolvement;
}

}  // namespace OffsideRule

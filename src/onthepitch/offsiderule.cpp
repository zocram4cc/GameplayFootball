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

}  // namespace OffsideRule

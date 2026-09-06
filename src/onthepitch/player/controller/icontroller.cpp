// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "icontroller.hpp"

#include "../../gameplaytuning.hpp"
#include "../../match.hpp"
#include "../playerbase.hpp"

void IController::SetPlayer(PlayerBase* player) {
  this->player = player;
}

int IController::GetReactionTime_ms() {
  return GameplayTuning::GetReactionTime_ms(player->GetStat("physical_reaction"));
}

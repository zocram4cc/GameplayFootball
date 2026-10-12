#include "foulsequence.hpp"

namespace FoulSequence {

unsigned long CutsceneLength_ms(int foulType) {
  // A booking or a sending off is held longer than a word with the offender.
  return (foulType == 2 || foulType == 3) ? kCardCutscene_ms : kWarningCutscene_ms;
}

unsigned long ReplayFiresAt_ms(unsigned long foulTime_ms, int foulType) {
  return foulTime_ms + CutsceneLength_ms(foulType);
}

unsigned long RestartPrepareAt_ms(unsigned long foulTime_ms, int foulType) {
  return ReplayFiresAt_ms(foulTime_ms, foulType) + kRestartPrepareAfterReplay_ms;
}

unsigned long RestartTakeAt_ms(unsigned long foulTime_ms, int foulType) {
  return RestartPrepareAt_ms(foulTime_ms, foulType) + kTakeAfterPrepare_ms;
}

unsigned long ReplayStartOffset_ms(unsigned long cutsceneElapsed_ms) {
  return cutsceneElapsed_ms + kReplayLeadIn_ms;
}

bool ScheduleIsConsistent() {
  for (int foulType = 0; foulType <= 3; ++foulType) {
    const unsigned long foul = 0;
    if (ReplayFiresAt_ms(foul, foulType) < foul + CutsceneLength_ms(foulType))
      return false;
    // The restart clears the situation, so it must not land before the replay.
    if (RestartPrepareAt_ms(foul, foulType) <= ReplayFiresAt_ms(foul, foulType))
      return false;
    if (RestartTakeAt_ms(foul, foulType) <= RestartPrepareAt_ms(foul, foulType))
      return false;
    if (ReplayStartOffset_ms(CutsceneLength_ms(foulType)) <= CutsceneLength_ms(foulType))
      return false;
  }
  return true;
}

}  // namespace FoulSequence

#include "goalsequence.hpp"

namespace GoalSequence {

unsigned long ReplayFiresAt_ms(unsigned long goalTime_ms, unsigned long cutsceneEnd_ms) {
  const unsigned long celebrationEnd = goalTime_ms + kCelebration_ms;
  return cutsceneEnd_ms > celebrationEnd ? cutsceneEnd_ms : celebrationEnd;
}

unsigned long RestartPrepareAt_ms(unsigned long goalTime_ms) {
  return ReplayFiresAt_ms(goalTime_ms) + kRestartPrepareAfterReplay_ms;
}

unsigned long RestartKickOffAt_ms(unsigned long goalTime_ms) {
  return RestartPrepareAt_ms(goalTime_ms) + kKickOffAfterPrepare_ms;
}

unsigned long ReplayStartOffset_ms(unsigned long celebrationElapsed_ms) {
  const unsigned long offset = celebrationElapsed_ms + kReplayLeadIn_ms;
  return offset > kReplayBuffer_ms ? kReplayBuffer_ms : offset;
}

bool ScheduleIsConsistent() {
  const unsigned long goal = 0;
  if (ReplayFiresAt_ms(goal) < goal + kCelebration_ms)
    return false;
  // The restart clears the goal state, so it must not land before the replay.
  if (RestartPrepareAt_ms(goal) <= ReplayFiresAt_ms(goal))
    return false;
  if (RestartKickOffAt_ms(goal) <= RestartPrepareAt_ms(goal))
    return false;
  // The replay has to still be able to reach back past the goal.
  const unsigned long start = ReplayStartOffset_ms(kCelebration_ms);
  return start > kCelebration_ms && start <= kReplayBuffer_ms;
}

}  // namespace GoalSequence

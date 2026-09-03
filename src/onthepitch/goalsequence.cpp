#include <initializer_list>

#include "goalsequence.hpp"

namespace GoalSequence {

unsigned long CelebrationLength_ms(unsigned long animLength_ms) {
  if (animLength_ms == 0)
    return kCelebration_ms;
  if (animLength_ms < kMinCelebration_ms)
    return kMinCelebration_ms;
  return animLength_ms > kLongestCelebration_ms ? kLongestCelebration_ms : animLength_ms;
}

unsigned long ReplayFiresAt_ms(unsigned long goalTime_ms, unsigned long cutsceneEnd_ms,
                               unsigned long celebrationLength_ms) {
  const unsigned long celebrationEnd = goalTime_ms + celebrationLength_ms;
  return cutsceneEnd_ms > celebrationEnd ? cutsceneEnd_ms : celebrationEnd;
}

unsigned long RestartPrepareAt_ms(unsigned long goalTime_ms,
                                  unsigned long celebrationLength_ms) {
  return ReplayFiresAt_ms(goalTime_ms, 0, celebrationLength_ms) +
         kRestartPrepareAfterReplay_ms;
}

unsigned long RestartKickOffAt_ms(unsigned long goalTime_ms,
                                  unsigned long celebrationLength_ms) {
  return RestartPrepareAt_ms(goalTime_ms, celebrationLength_ms) + kKickOffAfterPrepare_ms;
}

unsigned long ReplayStartOffset_ms(unsigned long celebrationElapsed_ms) {
  const unsigned long offset = celebrationElapsed_ms + kReplayLeadIn_ms;
  return offset > kReplayBuffer_ms ? kReplayBuffer_ms : offset;
}

bool ScheduleIsConsistent() {
  const unsigned long goal = 0;
  if (ReplayFiresAt_ms(goal) < goal + kCelebration_ms)
    return false;
  // The restart clears the goal state, so it must not land before the replay -
  // for the celebration that is actually on screen, not just the default one. A
  // cast performance scheduled against the default cleared the state at 10.5 s
  // while the trigger was still waiting on 20 s, and the replay never fired.
  for (unsigned long length :
       {kMinCelebration_ms, kCelebration_ms, kLongestCelebration_ms}) {
    if (RestartPrepareAt_ms(goal, length) <= ReplayFiresAt_ms(goal, 0, length))
      return false;
    if (RestartKickOffAt_ms(goal, length) <= RestartPrepareAt_ms(goal, length))
      return false;
  }
  // The replay has to still be able to reach back past the goal.
  // Every celebration the schedule can run has to leave the goal inside the replay.
  const unsigned long longest = ReplayStartOffset_ms(kLongestCelebration_ms);
  if (longest <= kLongestCelebration_ms || longest > kReplayBuffer_ms)
    return false;
  const unsigned long start = ReplayStartOffset_ms(kCelebration_ms);
  return start > kCelebration_ms && start <= kReplayBuffer_ms;
}

}  // namespace GoalSequence

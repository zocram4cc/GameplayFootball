// The order after a goal is celebration, replay, kickoff, and it only holds if
// two separately-owned timings agree: Match fires the replay once the
// celebration has run, and Referee schedules the kickoff restart, which clears
// the goal state (Match::ResetSituation) and with it the timer the replay
// trigger waits on.
//
// They disagreed. The celebration was given nine seconds while the referee
// prepared the restart six seconds after the goal, so the goal state was gone
// before the trigger was reachable and no goal replay fired at all. Both now
// come from here, and the test below is the one that would have caught it.

#include <gtest/gtest.h>

#include "onthepitch/goalsequence.hpp"

namespace {

constexpr unsigned long kGoal = 400000;  // an arbitrary goal instant

}  // namespace

TEST(GoalSequence, ReplayFiresOnlyAfterTheCelebration) {
  EXPECT_GE(GoalSequence::ReplayFiresAt_ms(kGoal), kGoal + GoalSequence::kCelebration_ms);
}

// The regression: the restart clears the goal state, so it has to come after
// the replay has been triggered, not before.
TEST(GoalSequence, RestartIsPreparedAfterTheReplayHasFired) {
  EXPECT_GT(GoalSequence::RestartPrepareAt_ms(kGoal), GoalSequence::ReplayFiresAt_ms(kGoal));
}

TEST(GoalSequence, KickOffFollowsTheRestartPreparation) {
  EXPECT_GT(GoalSequence::RestartKickOffAt_ms(kGoal), GoalSequence::RestartPrepareAt_ms(kGoal));
}

// A replay that starts after the celebration still has to open on the build-up,
// which means reaching back past the goal - and the buffer has to be long
// enough to still hold that moment.
TEST(GoalSequence, ReplayReachesBackPastTheGoal) {
  const unsigned long start = GoalSequence::ReplayStartOffset_ms(GoalSequence::kCelebration_ms);
  EXPECT_GT(start, GoalSequence::kCelebration_ms)
      << "the replay must start before the goal, not after it";
  EXPECT_LE(start, GoalSequence::kReplayBuffer_ms)
      << "the requested start is outside the recorded buffer";
}

TEST(GoalSequence, ScheduleIsSelfConsistent) {
  EXPECT_TRUE(GoalSequence::ScheduleIsConsistent());
}

// A goal cutscene longer than the plain celebration wins: "when the
// celebration is done and no sooner".
TEST(GoalSequence, ALongCutsceneDelaysTheReplay) {
  const unsigned long cutsceneEnd = kGoal + GoalSequence::kCelebration_ms + 4000;
  EXPECT_EQ(GoalSequence::ReplayFiresAt_ms(kGoal, cutsceneEnd), cutsceneEnd);
}

TEST(GoalSequence, AShortCutsceneDoesNotCutTheCelebrationShort) {
  const unsigned long cutsceneEnd = kGoal + 2000;
  EXPECT_EQ(GoalSequence::ReplayFiresAt_ms(kGoal, cutsceneEnd),
            kGoal + GoalSequence::kCelebration_ms);
}

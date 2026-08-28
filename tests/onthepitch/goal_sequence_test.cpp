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

// Does the replay actually contain the goal?
//
// The window is asked for as an offset back from now: the celebration that has just
// run, plus a lead-in so the build-up is in shot. It is then clamped to the recorded
// buffer, and that clamp is where the goal can fall out of the window - a celebration
// long enough pushes the start of the replay past the moment being replayed, and what
// plays back is the celebration rather than the goal.
//
// So the buffer has to cover the longest celebration the schedule allows plus the
// lead-in, and this says so in one place rather than leaving it to arithmetic nobody
// re-checks.

// The goal has to be inside the window, whatever the celebration did.
static bool GoalIsInsideTheWindow(unsigned long celebrationElapsed_ms) {
  const unsigned long offset = GoalSequence::ReplayStartOffset_ms(celebrationElapsed_ms);
  // The window runs from `offset` ago up to now; the goal was `celebrationElapsed` ago.
  return offset > celebrationElapsed_ms;
}

TEST(GoalReplayWindow, APlainCelebrationLeavesTheGoalInShot) {
  EXPECT_TRUE(GoalIsInsideTheWindow(GoalSequence::kCelebration_ms));
}

TEST(GoalReplayWindow, TheLeadInPutsTheBuildUpInShotToo) {
  const unsigned long offset = GoalSequence::ReplayStartOffset_ms(GoalSequence::kCelebration_ms);
  EXPECT_EQ(offset - GoalSequence::kCelebration_ms, GoalSequence::kReplayLeadIn_ms)
      << "the window should open a lead-in before the goal";
}

TEST(GoalReplayWindow, TheBufferCoversTheLongestCelebrationTheScheduleAllows) {
  // Anything up to this is a celebration the schedule can produce; every one of them
  // has to leave the goal inside the replay.
  for (unsigned long elapsed = 0; elapsed <= GoalSequence::kLongestCelebration_ms;
       elapsed += 500) {
    EXPECT_TRUE(GoalIsInsideTheWindow(elapsed))
        << "a celebration of " << elapsed << " ms pushes the goal out of the replay";
  }
}

TEST(GoalReplayWindow, TheBufferIsLongEnoughForThatByConstruction) {
  EXPECT_GE(GoalSequence::kReplayBuffer_ms,
            GoalSequence::kLongestCelebration_ms + GoalSequence::kReplayLeadIn_ms)
      << "the recorded buffer cannot reach back past the goal";
}

TEST(GoalReplayWindow, TheWindowIsStillCappedByTheBuffer) {
  // however long the celebration, the replay cannot ask for more than was recorded
  EXPECT_LE(GoalSequence::ReplayStartOffset_ms(10 * 60 * 1000), GoalSequence::kReplayBuffer_ms);
}

// How long a celebration is held.
//
// It was a flat nine seconds, which both cut long celebrations off partway and held
// short ones on a player running in place after his clip had finished. The clips say
// how long they are: over the 387 imported celebration animations at 10 ms a frame
// they run 0.4 s to 10.0 s, median 2.7 s, p90 6.8 s.

TEST(CelebrationLength, ItTakesTheClipsOwnLength) {
  EXPECT_EQ(GoalSequence::CelebrationLength_ms(6800), 6800u);
  EXPECT_EQ(GoalSequence::CelebrationLength_ms(10000), 10000u);
}

TEST(CelebrationLength, AVeryShortClipIsFloored) {
  // 0.4 s is a real clip length; cutting to it would be a flicker
  EXPECT_EQ(GoalSequence::CelebrationLength_ms(400), GoalSequence::kMinCelebration_ms);
}

TEST(CelebrationLength, ItCannotOutrunTheBuffer) {
  EXPECT_EQ(GoalSequence::CelebrationLength_ms(60000), GoalSequence::kLongestCelebration_ms);
}

TEST(CelebrationLength, AnUnknownClipFallsBackToTheDefault) {
  EXPECT_EQ(GoalSequence::CelebrationLength_ms(0), GoalSequence::kCelebration_ms);
}

TEST(CelebrationLength, TheReplayFiresWhenTheClipIsDoneRatherThanAtNineSeconds) {
  // Both clips sit above kMinCelebration_ms, so what is under test here is the
  // clip driving the timing - the floor has its own test above.
  const unsigned long shortClip = GoalSequence::CelebrationLength_ms(7000);
  EXPECT_EQ(GoalSequence::ReplayFiresAt_ms(kGoal, 0, shortClip), kGoal + 7000)
      << "a seven second celebration should not be held for nine";
  const unsigned long longClip = GoalSequence::CelebrationLength_ms(10000);
  EXPECT_EQ(GoalSequence::ReplayFiresAt_ms(kGoal, 0, longClip), kGoal + 10000)
      << "a ten second celebration should not be cut at nine";
}

TEST(CelebrationLength, EveryClipLengthLeavesTheGoalInTheReplay) {
  for (unsigned long anim = 0; anim <= 12000; anim += 250) {
    const unsigned long length = GoalSequence::CelebrationLength_ms(anim);
    EXPECT_TRUE(GoalIsInsideTheWindow(length))
        << "a clip of " << anim << " ms leaves the goal outside the replay";
  }
}

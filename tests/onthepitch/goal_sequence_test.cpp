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

TEST(CelebrationLength, TheMontageRunsWhateverTheClipDoes) {
  // This used to assert the clip's own length, which is what made a goal a
  // thirty-second affair: PES's celebration is as long as its three SHOTS
  // (tracking, tight, mob), and the median clip is 2.7 s. A clip shorter than
  // the montage does not shorten it - the cast is released and jogs back while
  // the shots run on, which is what the reference shows.
  EXPECT_EQ(GoalSequence::CelebrationLength_ms(6800), GoalSequence::kMinCelebration_ms);
  EXPECT_EQ(GoalSequence::CelebrationLength_ms(10000), GoalSequence::kMinCelebration_ms);
}

TEST(CelebrationLength, AClipLongerThanTheMontageExtendsIt) {
  // A chained intro-and-loop performance keeps its own length: the mob shot
  // holds while he is still performing.
  EXPECT_EQ(GoalSequence::CelebrationLength_ms(GoalSequence::kMinCelebration_ms + 4000),
            GoalSequence::kMinCelebration_ms + 4000);
}

TEST(GoalMontage, ThreeShotsInOrder) {
  const unsigned long length = GoalSequence::CelebrationLength_ms(0);
  EXPECT_EQ(GoalSequence::ShotAt(0, length), GoalSequence::Shot::Tracking);
  EXPECT_EQ(GoalSequence::ShotAt(GoalSequence::kTrackingShot_ms - 1, length),
            GoalSequence::Shot::Tracking);
  EXPECT_EQ(GoalSequence::ShotAt(GoalSequence::kTrackingShot_ms, length),
            GoalSequence::Shot::Tight);
  EXPECT_EQ(GoalSequence::ShotAt(GoalSequence::kTrackingShot_ms + GoalSequence::kTightShot_ms,
                                 length),
            GoalSequence::Shot::Group);
  // The tail of a clip that outran the montage stays on the mob rather than
  // cycling back to the tracking shot.
  EXPECT_EQ(GoalSequence::ShotAt(length - 1, length), GoalSequence::Shot::Group);
}

TEST(GoalMontage, EachShotStartsItsOwnCameraAtZero) {
  const unsigned long length = GoalSequence::CelebrationLength_ms(0);
  EXPECT_EQ(GoalSequence::ShotStartedAt_ms(0, length), 0u);
  EXPECT_EQ(GoalSequence::ShotStartedAt_ms(GoalSequence::kTrackingShot_ms + 10, length),
            GoalSequence::kTrackingShot_ms);
  EXPECT_EQ(GoalSequence::ShotStartedAt_ms(length - 1, length),
            GoalSequence::kTrackingShot_ms + GoalSequence::kTightShot_ms);
}

TEST(GoalMontage, TheWholeSequenceIsTheReferencesSixtyToEightySeconds) {
  // PES's goal, celebration, replay and restart run 60-80 s end to end
  // (youtu.be/ns5C3zpD6Ig). Ours measured about thirty.
  for (unsigned long anim : {0ul, 400ul, 2700ul, 10000ul, 60000ul}) {
    const unsigned long whole = GoalSequence::WholeSequence_ms(anim);
    EXPECT_GE(whole, 60000u) << "a clip of " << anim << " ms gives a " << whole << " ms sequence";
    EXPECT_LE(whole, 80000u) << "a clip of " << anim << " ms gives a " << whole << " ms sequence";
  }
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
  // The replay fires when the CELEBRATION is done - the montage for an ordinary
  // clip, and the clip itself when it outlasts the montage.
  const unsigned long ordinary = GoalSequence::CelebrationLength_ms(7000);
  EXPECT_EQ(GoalSequence::ReplayFiresAt_ms(kGoal, 0, ordinary),
            kGoal + GoalSequence::kMinCelebration_ms);
  const unsigned long chained =
      GoalSequence::CelebrationLength_ms(GoalSequence::kMinCelebration_ms + 5000);
  EXPECT_EQ(GoalSequence::ReplayFiresAt_ms(kGoal, 0, chained),
            kGoal + GoalSequence::kMinCelebration_ms + 5000)
      << "a performance that outran the montage should not be cut at it";
}

TEST(CelebrationLength, EveryClipLengthLeavesTheGoalInTheReplay) {
  for (unsigned long anim = 0; anim <= 12000; anim += 250) {
    const unsigned long length = GoalSequence::CelebrationLength_ms(anim);
    EXPECT_TRUE(GoalIsInsideTheWindow(length))
        << "a clip of " << anim << " ms leaves the goal outside the replay";
  }
}

// A cast performance is as long as PES authored it, which can outrun the plain
// default. The restart clears the goal state the replay trigger waits on, so
// whatever the celebration's length, preparing the kickoff must still land
// behind the replay - the 20 s cast that was scheduled against the 9 s default
// had the state cleared at 10.5 s and never showed a replay at all.
TEST(GoalSequence, TheRestartStaysBehindTheReplayForAnyCelebration) {
  for (unsigned long length = 0; length <= GoalSequence::kLongestCelebration_ms;
       length += 250) {
    const unsigned long celebration = GoalSequence::CelebrationLength_ms(length);
    EXPECT_GT(GoalSequence::RestartPrepareAt_ms(0, celebration),
              GoalSequence::ReplayFiresAt_ms(0, 0, celebration))
        << "a " << celebration << " ms celebration has the restart on top of the replay";
    EXPECT_GT(GoalSequence::RestartKickOffAt_ms(0, celebration),
              GoalSequence::RestartPrepareAt_ms(0, celebration));
  }
}

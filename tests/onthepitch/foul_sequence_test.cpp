// The sibling of goal_sequence_test.cpp, for the same hazard: the referee's
// restart calls Match::ResetSituation, so anything that has to happen between
// the whistle and the restart must be scheduled ahead of it.
//
// A foul gave the restart two seconds while the telling-off cutscene alone runs
// three and a half, so the cutscene was already being cut off before any replay
// was added to it.

#include <gtest/gtest.h>

#include "onthepitch/foulsequence.hpp"

namespace {

constexpr unsigned long kFoul = 250000;
constexpr int kPlainFoul = 1;
constexpr int kYellow = 2;
constexpr int kRed = 3;

}  // namespace

TEST(FoulSequence, ACardIsGivenLongerOnScreenThanATellingOff) {
  EXPECT_GT(FoulSequence::CutsceneLength_ms(kRed), FoulSequence::CutsceneLength_ms(kPlainFoul));
  EXPECT_EQ(FoulSequence::CutsceneLength_ms(kYellow), FoulSequence::CutsceneLength_ms(kRed));
}

TEST(FoulSequence, TheReplayWaitsForTheCutsceneToFinish) {
  for (int foulType : {kPlainFoul, kYellow, kRed}) {
    EXPECT_GE(FoulSequence::ReplayFiresAt_ms(kFoul, foulType),
              kFoul + FoulSequence::CutsceneLength_ms(foulType))
        << "foul type " << foulType;
  }
}

// The regression guard, same as for goals: the restart clears the state, so it
// must be scheduled after the replay has been triggered.
TEST(FoulSequence, TheRestartIsPreparedAfterTheReplayHasFired) {
  for (int foulType : {kPlainFoul, kYellow, kRed}) {
    EXPECT_GT(FoulSequence::RestartPrepareAt_ms(kFoul, foulType),
              FoulSequence::ReplayFiresAt_ms(kFoul, foulType))
        << "foul type " << foulType;
  }
}

TEST(FoulSequence, TheSetPieceIsTakenAfterItIsPrepared) {
  EXPECT_GT(FoulSequence::RestartTakeAt_ms(kFoul, kRed),
            FoulSequence::RestartPrepareAt_ms(kFoul, kRed));
}

TEST(FoulSequence, TheReplayReachesBackPastTheFoulItself) {
  const unsigned long back =
      FoulSequence::ReplayStartOffset_ms(FoulSequence::CutsceneLength_ms(kRed));
  EXPECT_GT(back, FoulSequence::CutsceneLength_ms(kRed))
      << "the replay must open before the foul, not on the aftermath";
}

TEST(FoulSequence, ScheduleIsSelfConsistent) {
  EXPECT_TRUE(FoulSequence::ScheduleIsConsistent());
}

// An unknown foul type must not produce a shorter window than a plain foul;
// referee.cpp passes its own type numbering straight through.
TEST(FoulSequence, AnUnknownFoulTypeIsTreatedAsAPlainOne) {
  EXPECT_EQ(FoulSequence::CutsceneLength_ms(99), FoulSequence::CutsceneLength_ms(kPlainFoul));
  EXPECT_EQ(FoulSequence::CutsceneLength_ms(0), FoulSequence::CutsceneLength_ms(kPlainFoul));
}

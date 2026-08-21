// Cutscene playback: a clock that stops when the match does, and can be skipped.
//
// Cutscenes were timed off EnvironmentManager::GetTime_ms(), the wall clock, against
// an end stamp taken when the cutscene started. Two reported faults follow:
//
//   Pause did not pause them. Match::Process runs its simulation under `if (!pause)`,
//   but a wall clock does not care, so a paused cutscene played on to its end.
//
//   A replay fired while one was still running showed the cutscene instead of the
//   action, because the cutscene went on driving the camera off its own clock.
//
// And there was no way to skip one, which PES has.

#include "onthepitch/cutsceneplayback.hpp"

#include <gtest/gtest.h>

namespace {

CutscenePlayback::State Started(unsigned long length_ms) {
  CutscenePlayback::State state;
  CutscenePlayback::Start(state, length_ms);
  return state;
}

}  // namespace

TEST(CutscenePlayback, AFreshStateIsNotPlaying) {
  CutscenePlayback::State state;
  EXPECT_FALSE(CutscenePlayback::IsPlaying(state));
  EXPECT_TRUE(CutscenePlayback::IsDone(state));
}

TEST(CutscenePlayback, StartingItMakesItPlay) {
  CutscenePlayback::State state = Started(5000);
  EXPECT_TRUE(CutscenePlayback::IsPlaying(state));
  EXPECT_FALSE(CutscenePlayback::IsDone(state));
  EXPECT_EQ(CutscenePlayback::Elapsed_ms(state), 0u);
}

TEST(CutscenePlayback, ItAdvancesWithTheMatch) {
  CutscenePlayback::State state = Started(5000);
  CutscenePlayback::Advance(state, 1000, false);
  EXPECT_EQ(CutscenePlayback::Elapsed_ms(state), 1000u);
  CutscenePlayback::Advance(state, 1500, false);
  EXPECT_EQ(CutscenePlayback::Elapsed_ms(state), 2500u);
  EXPECT_TRUE(CutscenePlayback::IsPlaying(state));
}

TEST(CutscenePlayback, ItRunsOutAtItsLength) {
  CutscenePlayback::State state = Started(5000);
  CutscenePlayback::Advance(state, 5000, false);
  EXPECT_TRUE(CutscenePlayback::IsDone(state));
  EXPECT_FALSE(CutscenePlayback::IsPlaying(state));
}

TEST(CutscenePlayback, PauseStopsIt) {
  // the whole point: a paused cutscene holds where it is
  CutscenePlayback::State state = Started(5000);
  CutscenePlayback::Advance(state, 1000, false);
  for (int i = 0; i < 20; i++)
    CutscenePlayback::Advance(state, 1000, true);
  EXPECT_EQ(CutscenePlayback::Elapsed_ms(state), 1000u) << "it ran on while paused";
  EXPECT_TRUE(CutscenePlayback::IsPlaying(state));
}

TEST(CutscenePlayback, ItCarriesOnAfterAPause) {
  CutscenePlayback::State state = Started(5000);
  CutscenePlayback::Advance(state, 1000, false);
  CutscenePlayback::Advance(state, 9000, true);
  CutscenePlayback::Advance(state, 1000, false);
  EXPECT_EQ(CutscenePlayback::Elapsed_ms(state), 2000u);
}

TEST(CutscenePlayback, SkippingEndsItAtOnce) {
  CutscenePlayback::State state = Started(60000);
  CutscenePlayback::Skip(state);
  EXPECT_TRUE(CutscenePlayback::IsDone(state));
  EXPECT_FALSE(CutscenePlayback::IsPlaying(state));
}

TEST(CutscenePlayback, SkippingSomethingNotPlayingIsHarmless) {
  CutscenePlayback::State state;
  CutscenePlayback::Skip(state);
  EXPECT_TRUE(CutscenePlayback::IsDone(state));
}

TEST(CutscenePlayback, AZeroLengthCutsceneIsDoneImmediately) {
  CutscenePlayback::State state = Started(0);
  EXPECT_TRUE(CutscenePlayback::IsDone(state));
}

TEST(CutscenePlayback, ARestartClearsTheOldElapsed) {
  CutscenePlayback::State state = Started(5000);
  CutscenePlayback::Advance(state, 4000, false);
  CutscenePlayback::Start(state, 3000);
  EXPECT_EQ(CutscenePlayback::Elapsed_ms(state), 0u);
  EXPECT_TRUE(CutscenePlayback::IsPlaying(state));
}

TEST(CutscenePlayback, ProgressRunsFromZeroToOne) {
  CutscenePlayback::State state = Started(4000);
  EXPECT_FLOAT_EQ(CutscenePlayback::Progress(state), 0.0f);
  CutscenePlayback::Advance(state, 2000, false);
  EXPECT_FLOAT_EQ(CutscenePlayback::Progress(state), 0.5f);
  CutscenePlayback::Advance(state, 2000, false);
  EXPECT_FLOAT_EQ(CutscenePlayback::Progress(state), 1.0f);
}

TEST(CutscenePlayback, ProgressOfSomethingNotPlayingIsFinished) {
  CutscenePlayback::State state;
  EXPECT_FLOAT_EQ(CutscenePlayback::Progress(state), 1.0f);
}

TEST(CutscenePlayback, ElapsedNeverRunsPastTheLength) {
  CutscenePlayback::State state = Started(3000);
  CutscenePlayback::Advance(state, 10000, false);
  EXPECT_EQ(CutscenePlayback::Elapsed_ms(state), 3000u);
}

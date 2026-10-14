// PES 2021's set-piece taker timing: how long the AI holds a restart before the
// kicker takes it, docs/re/pes-re-setpiece-choreography.md 9 item 2.
//
// Measured live (54 Hz logic frames, counted from the set-play counter's first
// frame, i.e. from the restart being armed):
//   corner, long cross   counter 379 (= 2 x 189 + 1)   -> CORNERKICK (50)
//   corner, short        counter  57 and 78            -> CORNERKICK_SHORT (51)
//   free kick, long pass counter 163                    -> FREEKICK_LONGPASS (32)
//   goal kick            counter  83                    -> KP_GOALKICK (63)
//   kick-off             counter  28 (exact, x3)        -> not this rule
//
// The rule is ThinkUnitSetPlay's elapsed-frame count (`MI+0x1198` minus the
// AI context's stored base frame): a corner crosses once elapsed >= 189 frames
// (fps x 3.5), a free kick once elapsed >= 162 (fps x 3), a penalty once
// elapsed >= 54. The kick-off is not governed by it: the receiver's own
// ball-ownership check releases the pass.
//
// `waitTimerMin` in constant::BallplayerSetplay (+0x28) is NOT the source of
// these numbers: nothing reads that field on this path, and the gates are the
// fps multiples above. It is recorded so nobody ports it.

#include <gtest/gtest.h>

#include "onthepitch/pessetpiecetaker.hpp"
#include "onthepitch/setpiecelaws.hpp"

#include <cstdlib>
#include <memory>
#include <string>

#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace {

// The gates in frames at 54 Hz, as measured.
constexpr float kFps = 54.0f;
constexpr int kCornerGateFrames = 189;  // fps * 3.5
constexpr int kFreeKickGateFrames = 162;  // fps * 3.0
constexpr int kPenaltyGateFrames = 54;  // fps * 1.0

int ElapsedAfterSeconds(float seconds) {
  return static_cast<int>(seconds * kFps);
}

// The measured gates, as synthetic constants: the logic tests pin the rule, the
// install's own numbers are checked separately.
pes::SetPieceTakerConstants MeasuredGates() {
  pes::SetPieceTakerConstants k;
  k.cornerGate_s = 3.5f;
  k.freeKickGate_s = 3.0f;
  k.penaltyGate_s = 1.0f;
  k.shortCornerWindow_s = 2.0f;
  k.shortCornerRunningWindow_s = 7.0f;
  return k;
}

const pes::SetPieceTakerConstants kGates = MeasuredGates();

}  // namespace

TEST(SetPieceTakerGate, HoldsUntilTheGatesElapse) {
  // Before the gate the kicker waits.
  EXPECT_FALSE(SetPieceLaws::MayTakeRestart(e_SetPiece_Corner, ElapsedAfterSeconds(0.0f), kFps,
                                           kGates));
  EXPECT_FALSE(SetPieceLaws::MayTakeRestart(e_SetPiece_Corner, ElapsedAfterSeconds(2.0f), kFps,
                                            kGates));
  EXPECT_FALSE(SetPieceLaws::MayTakeRestart(e_SetPiece_Corner, 188, kFps, kGates));
  // At the gate the kicker goes.
  EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_Corner, kCornerGateFrames, kFps, kGates));
  EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_Corner, ElapsedAfterSeconds(3.5f), kFps,
                                           kGates));
  // And keeps going: the live 379-frame corner is the gate's second pass.
  EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_Corner, 379, kFps, kGates));
}

TEST(SetPieceTakerGate, FreeKickGatesEarlierThanACorner) {
  EXPECT_FALSE(SetPieceLaws::MayTakeRestart(e_SetPiece_FreeKick, 161, kFps, kGates));
  EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_FreeKick, kFreeKickGateFrames, kFps, kGates));
  EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_FreeKick, 163, kFps, kGates));
}

TEST(SetPieceTakerGate, PenaltyGatesEarliest) {
  EXPECT_FALSE(SetPieceLaws::MayTakeRestart(e_SetPiece_Penalty, 53, kFps, kGates));
  EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_Penalty, kPenaltyGateFrames, kFps, kGates));
}

TEST(SetPieceTakerGate, KickOffIsNotGatedByTheseNumbers) {
  // PES's AI kick-off passes at counter 29 of the loop - the receiver's own
  // ownership test, not a 3-second wait. The gate must not delay it.
  for (int elapsed : {0, 1, 10, 27, 28}) {
    EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_KickOff, elapsed, kFps, kGates))
        << "kick-off delayed at elapsed " << elapsed;
  }
}

TEST(SetPieceTakerGate, ThrowInAndGoalKickUseTheirOwnWait) {
  // Both are governed by the taker's own arrival/readiness, not by a fixed
  // gate: a goal kick went at counter 83 in one match but is not fenced by
  // this number. Anything is permitted; nothing is forced.
  EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_ThrowIn, 0, kFps, kGates));
  EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_GoalKick, 0, kFps, kGates));
  EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_GoalKick, 83, kFps, kGates));
}

TEST(SetPieceTakerGate, ShortCornerWindowIsABoundedWindow) {
  // The short corner lives inside fps*2 (108 frames) and is released when the
  // partner check passes - not at a fixed frame. Both live samples (57, 78)
  // sit inside that window.
  const int windowFrames = static_cast<int>(kFps * 2.0f);
  EXPECT_TRUE(SetPieceLaws::InShortCornerWindow(57, windowFrames));
  EXPECT_TRUE(SetPieceLaws::InShortCornerWindow(78, windowFrames));
  EXPECT_TRUE(SetPieceLaws::InShortCornerWindow(windowFrames - 1, windowFrames));
  EXPECT_FALSE(SetPieceLaws::InShortCornerWindow(windowFrames, windowFrames));
  EXPECT_FALSE(SetPieceLaws::InShortCornerWindow(ElapsedAfterSeconds(3.5f), windowFrames));
}

TEST(SetPieceTakerGate, GatesScaleWithFrameRate) {
  // At 60 Hz the same seconds become 210/180/60 frames: the numbers are fps
  // multiples, so the rule has to be expressed in seconds or against the real
  // frame rate, never as literal frame counts.
  const float rate60 = 60.0f;
  const float gate60 = 3.5f * rate60;
  EXPECT_FALSE(
      SetPieceLaws::MayTakeRestart(e_SetPiece_Corner, static_cast<int>(gate60) - 1, rate60, kGates));
  EXPECT_TRUE(
      SetPieceLaws::MayTakeRestart(e_SetPiece_Corner, static_cast<int>(gate60), rate60, kGates));
}

// The gates read out of the install's exe, not transcribed into the source.
TEST(SetPieceTakerGate, GatesReadFromTheExe) {
  if (const char* root = std::getenv("PES21_ROOT")) {
    pes::PesVfs vfs;
    std::string err;
    auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
    if (!exe) GTEST_SKIP() << "PES2021.exe unreadable: " << err;
    if (!vfs.Mount(root, &err)) GTEST_SKIP() << "PES install unreadable: " << err;
    auto k = pes::LoadSetPieceTakerConstants(vfs, *exe, &err);
    ASSERT_TRUE(k.has_value()) << err;
    // The measured values: 3.5 s corner, 3 s free kick, 1 s penalty.
    EXPECT_FLOAT_EQ(k->cornerGate_s, 3.5f);
    EXPECT_FLOAT_EQ(k->freeKickGate_s, 3.0f);
    EXPECT_FLOAT_EQ(k->penaltyGate_s, 1.0f);
    // And the gate built from them reproduces the measured frame counts.
    EXPECT_FALSE(SetPieceLaws::MayTakeRestart(e_SetPiece_Corner, 188, 54.0f, *k));
    EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_Corner, 189, 54.0f, *k));
    EXPECT_FALSE(SetPieceLaws::MayTakeRestart(e_SetPiece_FreeKick, 161, 54.0f, *k));
    EXPECT_TRUE(SetPieceLaws::MayTakeRestart(e_SetPiece_FreeKick, 162, 54.0f, *k));
  } else {
    GTEST_SKIP() << "PES21_ROOT not set: the gates are read from the install's exe";
  }
}

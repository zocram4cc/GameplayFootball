// .chor loader: imported PES match-entrance player choreography
// (entrance_pl.py output).

#include <gtest/gtest.h>

#include <sstream>

#include "utils/entrancechoreo.hpp"

namespace {

const char* kChor =
    "chor 1\n"
    "source ent_020_order01_pl.fdc\n"
    "slot 0 anims/stretch.anim phase 4 loop 1\n"
    "k 0 -6.0 -18.0 1.5\n"
    "k 2 -6.2 -18.4 1.7\n"
    "k 4 -6.4 -18.8 1.9\n"
    "slot 11 anims/idle.anim phase 0 loop 0\n"
    "k 0 40.0 0.0 -1.0\n"
    "k 2 40.0 0.0 -1.0\n";

TEST(EntranceChoreo, ParsesSlots) {
  std::istringstream in(kChor);
  blunted::EntranceChoreo choreo;
  ASSERT_TRUE(choreo.Load(in));
  ASSERT_EQ(choreo.GetSlots().size(), 2u);

  const blunted::ChoreoSlot* slot = choreo.GetSlot(0);
  ASSERT_NE(slot, nullptr);
  EXPECT_EQ(slot->animFile, "anims/stretch.anim");
  EXPECT_EQ(slot->phaseFrames, 4);
  EXPECT_TRUE(slot->loop);
  EXPECT_EQ(slot->cycleFrames, 4);
  EXPECT_EQ(choreo.GetSlot(5), nullptr);
}

TEST(EntranceChoreo, SamplesAndInterpolates) {
  std::istringstream in(kChor);
  blunted::EntranceChoreo choreo;
  ASSERT_TRUE(choreo.Load(in));
  const blunted::ChoreoSlot* slot = choreo.GetSlot(0);

  blunted::Vector3 position;
  blunted::radian yaw = 0;
  int animFrame = 0;
  choreo.Sample(*slot, 1.0f, position, yaw, animFrame);
  EXPECT_NEAR(position.coords[0], -6.1f, 1e-4);
  EXPECT_NEAR(position.coords[1], -18.2f, 1e-4);
  EXPECT_NEAR(yaw, 1.6f, 1e-4);
  // The phase offset is applied and the count left unwrapped: the clip's length
  // is not the path's, so the caller wraps this against the clip it plays.
  EXPECT_EQ(animFrame, 4 + 1);
}

TEST(EntranceChoreo, ThePathPlaysOnceHoweverTheSlotIsFlagged) {
  // The clip loops - a walk cycle is meant to - but the path must not. PES flags
  // its walk-on slots "loop 1", and wrapping their path teleported the actor back
  // to the tunnel mouth and walked him in again: the walk-on beat runs 22 seconds
  // over a 13-second path, so the reset happened in plain view, twice a beat.
  std::istringstream in(kChor);
  blunted::EntranceChoreo choreo;
  ASSERT_TRUE(choreo.Load(in));

  blunted::Vector3 position;
  blunted::radian yaw = 0;
  int animFrame = 0;

  // slot 0 is flagged looping, on a 4-frame path; at elapsed 5 it holds its end
  // rather than starting again
  choreo.Sample(*choreo.GetSlot(0), 5.0f, position, yaw, animFrame);
  blunted::Vector3 atEnd;
  choreo.Sample(*choreo.GetSlot(0), 4.0f, atEnd, yaw, animFrame);
  EXPECT_NEAR(position.coords[0], atEnd.coords[0], 1e-4);
  EXPECT_NEAR(position.coords[1], atEnd.coords[1], 1e-4);

  // and the non-looping slot behaves the same way
  choreo.Sample(*choreo.GetSlot(11), 50.0f, position, yaw, animFrame);
  EXPECT_NEAR(position.coords[0], 40.0f, 1e-4);
  EXPECT_NEAR(yaw, -1.0f, 1e-4);
}

TEST(EntranceChoreo, TheClipFrameKeepsCountingSoFeetKeepMoving) {
  // The caller wraps this against the clip's own length (see
  // Match::UpdateEntranceChoreo), so it must not be wrapped against the path's -
  // otherwise an actor who has arrived stands frozen instead of marking time.
  std::istringstream in(kChor);
  blunted::EntranceChoreo choreo;
  ASSERT_TRUE(choreo.Load(in));

  blunted::Vector3 position;
  blunted::radian yaw = 0;
  int early = 0, late = 0;
  choreo.Sample(*choreo.GetSlot(0), 2.0f, position, yaw, early);
  choreo.Sample(*choreo.GetSlot(0), 20.0f, position, yaw, late);
  EXPECT_GT(late, early);
}

TEST(EntranceChoreo, EmptyFails) {
  std::istringstream in("");
  blunted::EntranceChoreo choreo;
  EXPECT_FALSE(choreo.Load(in));
}

}  // namespace

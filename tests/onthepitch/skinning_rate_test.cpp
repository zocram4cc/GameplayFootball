// Which bodies get skinned on which frame.
//
// HumanoidBase::NeedsModelUpdate has always returned false on alternate frames
// for any body more than 14 m from the action, and GameTask::PutPhase uses that
// to leave the body out of the frame's skinning entirely. So every player away
// from the ball animated at half the frame rate - which on a television camera is
// almost the whole pitch, and is exactly the "skipping animation frames" you can
// see. It bought a lot when a body was 453 vertices and skinning it cost 49.7 ms
// a frame; at 11.5 ms it buys much less than it costs in smoothness.
//
// The halving stays available for machines that want it, so the decision is a
// policy, and this is that policy.

#include <gtest/gtest.h>

#include "onthepitch/player/humanoid/skinning.hpp"

TEST(SkinningRate, EveryBodyIsSkinnedEveryFrameByDefault) {
  for (int phase = 0; phase < 2; phase++) {
    for (int offset = 0; offset < 2; offset++) {
      EXPECT_TRUE(Skinning::BodyNeedsSkinning(true, false, phase, offset))
          << "distant body, phase " << phase << ", offset " << offset;
      EXPECT_TRUE(Skinning::BodyNeedsSkinning(false, false, phase, offset))
          << "near body, phase " << phase << ", offset " << offset;
    }
  }
}

TEST(SkinningRate, TheBodyNearTheActionIsAlwaysSkinnedEvenWhenHalving) {
  for (int phase = 0; phase < 2; phase++)
    EXPECT_TRUE(Skinning::BodyNeedsSkinning(false, true, phase, 0)) << "phase " << phase;
}

TEST(SkinningRate, WhenHalvingADistantBodyIsSkinnedOnEveryOtherFrame) {
  int skinned = 0;
  for (int frame = 0; frame < 10; frame++) {
    // the engine flips the phase once per fetch
    if (Skinning::BodyNeedsSkinning(true, true, frame % 2, 0)) skinned++;
  }
  EXPECT_EQ(skinned, 5);
}

TEST(SkinningRate, TheOffsetPutsHalfTheSquadOnTheOppositeFrame) {
  // Bodies are given alternating offsets so the work is spread evenly over the
  // two frames rather than every distant body landing on the same one.
  for (int phase = 0; phase < 2; phase++) {
    EXPECT_NE(Skinning::BodyNeedsSkinning(true, true, phase, 0),
              Skinning::BodyNeedsSkinning(true, true, phase, 1))
        << "phase " << phase;
  }
}

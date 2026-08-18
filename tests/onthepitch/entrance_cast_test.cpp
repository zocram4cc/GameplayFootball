// Who is on the pitch during the entrance, and who is parked.
//
// The cast of a staging is posed by the choreography; everyone else in the two
// squads has no business being visible yet. That was done by calling Hide() on them
// every frame - and Hide() only parks the model at (1000, 1000, -1000), which the
// player's own UpdateFullbodyNodes puts straight back, because it follows the
// humanoid node. The two run on different schedules, so whichever won decided
// whether that player was on screen: measured off the recording, the squads blinked
// in and out of the centre circle every frame or two.
//
// So being parked is a state a humanoid holds (HumanoidBase::SetBenched), and this
// is the decision that sets it - one answer per player per frame, no race.

#include <gtest/gtest.h>

#include "onthepitch/entrancecast.hpp"

TEST(EntranceCast, OnceThePitchIsLiveNobodyIsParked) {
  EXPECT_FALSE(EntranceCast::ShouldBench(/*inEntrance=*/false, /*holdingOpeningFrame=*/false,
                                         /*isStaged=*/false));
  EXPECT_FALSE(EntranceCast::ShouldBench(false, false, true));
  // even mid-hold: if the entrance is over, the flag has to clear itself
  EXPECT_FALSE(EntranceCast::ShouldBench(false, true, false));
}

TEST(EntranceCast, TheStagedCastPlays) {
  EXPECT_FALSE(EntranceCast::ShouldBench(true, false, true));
}

TEST(EntranceCast, EverybodyElseIsParked) {
  EXPECT_TRUE(EntranceCast::ShouldBench(true, false, false));
}

// The establishing beat looks out over a pitch the squads have not walked onto yet.
// Holding a borrowed pack's opening frame put both elevens in a ring on the centre
// circle for the whole stadium card, so that beat parks the cast as well.
TEST(EntranceCast, TheHeldOpeningFrameShowsAnEmptyPitch) {
  EXPECT_TRUE(EntranceCast::ShouldBench(true, true, true));
  EXPECT_TRUE(EntranceCast::ShouldBench(true, true, false));
}

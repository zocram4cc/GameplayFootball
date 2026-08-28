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

// Which ground a camera track was authored for.
//
// PES names its entrance cameras after the stadium, and reading that name as "the
// first st in the path" found the one in "stadiums": every ground asked for tracks
// called "stadi*", got none, and was filmed with whatever shot sorted first. On
// Planet Namek that was st000's, whose lens sits where another ground's tunnel is -
// in among the walking players, two of them filling the frame.
TEST(StadiumToken, ReadsTheCodeAndNotTheWordStadiums) {
  EXPECT_EQ(EntranceCast::StadiumToken(
                "media/objects/stadiums/pes_st017/pes_st017.object"),
            "st017");
}

TEST(StadiumToken, TakesTheDigitsHowManyThereAre) {
  EXPECT_EQ(EntranceCast::StadiumToken("x/pes_st7/a.object"), "st7");
  EXPECT_EQ(EntranceCast::StadiumToken("x/pes_st1234/a.object"), "st1234");
}

TEST(StadiumToken, APathWithNoStadiumCodeHasNoToken) {
  EXPECT_EQ(EntranceCast::StadiumToken("media/objects/stadiums/mine/mine.object"), "");
  EXPECT_EQ(EntranceCast::StadiumToken(""), "");
}

TEST(StadiumToken, TheFirstRealCodeWins) {
  // "stadiums" and "street" both start with st and carry no digits.
  EXPECT_EQ(EntranceCast::StadiumToken("stadiums/street/pes_st060/x.object"), "st060");
}

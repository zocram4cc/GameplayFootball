// Which competition's emblem rings the centre circle.
//
// PES sets a ring of pennant bearers on the centre circle for a competition tie -
// circleflag_afc_cl_01, four flag faces and four bearers - and the 4cc mod does the
// same to its UEFA slot. The face carries the competition's own badge, and these
// packs ship both of them as plain PNGs in common/render/symbol/emblemLc: emb_0004
// is the 4chan Stupor Cup, the four-leaf clover, and emb_0008 the /vg/ Football
// League crest.
//
// Which one a tie flies follows from who is playing. The 4chan cup's teams are
// boards - /a/, /b/, /int/ - and the /vg/ league's are games: LCG, 2HUG. So two
// boards fly the clover and anything else flies the league.

#include <gtest/gtest.h>

#include "onthepitch/competitionemblem.hpp"

TEST(CompetitionEmblem, TwoBoardsFlyTheClover) {
  EXPECT_EQ(CompetitionEmblem::ForTeams("/a/", "/b/"), "4cc");
  EXPECT_EQ(CompetitionEmblem::ForTeams("/int/", "/vg/"), "4cc");
  EXPECT_EQ(CompetitionEmblem::ForTeams("/m/", "/tg/"), "4cc");
}

TEST(CompetitionEmblem, TheLeaguesTeamsFlyItsCrest) {
  EXPECT_EQ(CompetitionEmblem::ForTeams("LCG", "2HUG"), "vgl");
  EXPECT_EQ(CompetitionEmblem::ForTeams("Touhou", "Nep"), "vgl");
}

TEST(CompetitionEmblem, AMixedTieIsTheLeagues) {
  // a board against a named side is not the 4chan cup's own competition
  EXPECT_EQ(CompetitionEmblem::ForTeams("/a/", "LCG"), "vgl");
  EXPECT_EQ(CompetitionEmblem::ForTeams("2HUG", "/b/"), "vgl");
}

TEST(CompetitionEmblem, ABoardIsSlashesRoundAShortTag) {
  EXPECT_TRUE(CompetitionEmblem::IsBoard("/a/"));
  EXPECT_TRUE(CompetitionEmblem::IsBoard("/vip/"));
  EXPECT_TRUE(CompetitionEmblem::IsBoard("/sci/"));
  EXPECT_FALSE(CompetitionEmblem::IsBoard("LCG"));
  EXPECT_FALSE(CompetitionEmblem::IsBoard(""));
  EXPECT_FALSE(CompetitionEmblem::IsBoard("/"));
  EXPECT_FALSE(CompetitionEmblem::IsBoard("//"));
  EXPECT_FALSE(CompetitionEmblem::IsBoard("a/"));
  EXPECT_FALSE(CompetitionEmblem::IsBoard("/a"));
  // long enough to be a name rather than a board
  EXPECT_FALSE(CompetitionEmblem::IsBoard("/somethinglong/"));
}

TEST(CompetitionEmblem, SurroundingSpaceDoesNotHideABoard) {
  EXPECT_TRUE(CompetitionEmblem::IsBoard("  /a/ "));
  EXPECT_EQ(CompetitionEmblem::ForTeams(" /a/ ", "/b/ "), "4cc");
}

TEST(CompetitionEmblem, ThePennantSitsBesideTheStadium) {
  EXPECT_EQ(CompetitionEmblem::ObjectPath("media/objects/stadiums/pes_st017/pes_st017.object",
                                          "4cc"),
            "media/objects/stadiums/pes_st017/entrance/pennant_4cc.object");
  EXPECT_EQ(CompetitionEmblem::ObjectPath("", "4cc"), "");
}

TEST(CompetitionEmblem, AnEmblemNameIsNotAPath) {
  // whatever a config says, this only ever names a file beside the stadium
  EXPECT_EQ(CompetitionEmblem::ObjectPath("media/objects/stadiums/pes_st002/pes_st002.object",
                                          "../../../etc/passwd"),
            "");
  EXPECT_EQ(CompetitionEmblem::ObjectPath("media/objects/stadiums/pes_st002/pes_st002.object", ""),
            "");
}

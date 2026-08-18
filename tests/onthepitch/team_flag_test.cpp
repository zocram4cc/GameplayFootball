// Whose badge the crowd's stand flags fly.
//
// PES's stand flags (mob_prop_teamflag_home01..05, away01) carry a texture called
// sys_zero_bsm, which is not a zero: it is a per-model placeholder PES swaps at run
// time, and each model ships its own picture under that one name - the flag
// bearers' is the flag of the United States, the tunnel arch's and the stand flags'
// are the FC Barcelona crest. Imported verbatim, every crowd in every converted
// ground flew Barcelona.
//
// So the importer does not bring the placeholder in at all
// (tools/pes21_import/stadium_crowd.py: is_placeholder_texture). It points the
// material at the engine's own neutral cloth - teamflag_home.png or
// teamflag_away.png - and the engine paints the playing team's badge over it, which
// is what PES does with it too.

#include <gtest/gtest.h>

#include "onthepitch/teamflag.hpp"

TEST(TeamFlag, TheHomeSideIsRecognisedByItsCloth) {
  EXPECT_EQ(TeamFlag::SideOf("media/textures/stadium/teamflag_home.png"), TeamFlag::e_Home);
  EXPECT_EQ(TeamFlag::SideOf("teamflag_home"), TeamFlag::e_Home);
}

TEST(TeamFlag, TheAwaySideToo) {
  EXPECT_EQ(TeamFlag::SideOf("media/textures/stadium/teamflag_away.png"), TeamFlag::e_Away);
}

TEST(TeamFlag, AnythingElseIsNotAStandFlag) {
  // the crowd's own palette, the cloth's normal map, a stadium texture
  EXPECT_EQ(TeamFlag::SideOf("media/objects/stadiums/pes_st017/crowd/textures/au_h_col_bsm.png"),
            TeamFlag::e_NotAFlag);
  EXPECT_EQ(TeamFlag::SideOf("mob_teamflag_nrm_nomip.png"), TeamFlag::e_NotAFlag);
  EXPECT_EQ(TeamFlag::SideOf(""), TeamFlag::e_NotAFlag);
}

TEST(TeamFlag, TheNameIsMatchedWhereverItSitsInThePath) {
  // an author may keep the cloth beside their own stadium
  EXPECT_EQ(TeamFlag::SideOf("media/objects/stadiums/mine/teamflag_away_v2.png"),
            TeamFlag::e_Away);
}

TEST(TeamFlag, HomeIsNotMatchedInsideAnAwayPath) {
  // "away" wins where both appear, because the file is named for what it is
  EXPECT_EQ(TeamFlag::SideOf("home_ground/teamflag_away.png"), TeamFlag::e_Away);
}

// Which image goes on it: a team's own badge if it has one, and otherwise the cloth
// stays as it is rather than the crowd flying somebody else's crest.
TEST(TeamFlag, ATeamsBadgeIsItsLogo) {
  EXPECT_EQ(TeamFlag::BadgeFor("images_teams/lcg/lcg_logo.png"), "images_teams/lcg/lcg_logo.png");
}

TEST(TeamFlag, ATeamWithNoBadgeLeavesTheClothAlone) {
  EXPECT_EQ(TeamFlag::BadgeFor(""), "");
  EXPECT_EQ(TeamFlag::BadgeFor("   "), "");
}

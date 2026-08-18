// Keeping a shot's camera and its choreography together.
//
// PES authors both per stadium and per variant, and names them in pairs:
//
//     ent_009_st002_cmn_cam.canm   the camerawork
//     ent_009_st002_cmn_pl.fdc     the players it films
//
// Asking for the family alone ("ent_009") resolved each independently, and the
// two came from different grounds: the staging from stadium 000, the camera from
// stadium 002. The camera then sat wherever stadium 002's tunnel mouth is, which
// on stadium 000's choreography is inside the column - the lens spent the whole
// walk-on buried in a player's chest.

#include <gtest/gtest.h>

#include "onthepitch/prematchshotpair.hpp"

TEST(PrematchShotPair, TheStagingIsTheCamerasOwn) {
  EXPECT_EQ(PrematchShotPair::StagingForCamera("ent_009_st002_cmn_cam"), "ent_009_st002_cmn_pl");
  EXPECT_EQ(PrematchShotPair::StagingForCamera("ent_009_st002_arch_cam"), "ent_009_st002_arch_pl");
}

TEST(PrematchShotPair, AFamilyWithNoVariantPairsJustAsWell) {
  EXPECT_EQ(PrematchShotPair::StagingForCamera("ent_020_st002_cam"), "ent_020_st002_pl");
}

TEST(PrematchShotPair, ANumberedCameraStillNamesOnePlayerPack) {
  // ent_020 ships cam and cam_1 against a single _pl pack.
  EXPECT_EQ(PrematchShotPair::StagingForCamera("ent_020_st002_cam_1"), "ent_020_st002_pl");
}

TEST(PrematchShotPair, APathOrExtensionIsIgnored) {
  EXPECT_EQ(PrematchShotPair::StagingForCamera("media/cutscenes/ent/009/ent_009_st002_cmn_cam.camtrack"),
            "ent_009_st002_cmn_pl");
}

TEST(PrematchShotPair, SomethingThatIsNotACameraNamesNothing) {
  EXPECT_EQ(PrematchShotPair::StagingForCamera("ent_009_st000_cmn_pl"), "");
  EXPECT_EQ(PrematchShotPair::StagingForCamera(""), "");
  EXPECT_EQ(PrematchShotPair::StagingForCamera("camera"), "");
}

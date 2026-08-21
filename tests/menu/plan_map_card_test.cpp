// What a game-plan card says about one player.
//
// The card is PES's: portrait, a strip with the position on the left and the rating on
// the right, the name beneath. The colours are measured off the /vg/ League 26
// broadcast rather than guessed - GK yellow, defenders cyan, midfielders green,
// forwards red - so these tests pin the mapping that reading produced.

#include <gtest/gtest.h>

#include "menu/widgets/planmapcard.hpp"

namespace {

bool IsYellowish(const PlanMapCard::Colour& c) {
  return c.r > 180 && c.g > 150 && c.b < 110;
}
bool IsCyanish(const PlanMapCard::Colour& c) { return c.r < 130 && c.g > 150 && c.b > 180; }
bool IsGreenish(const PlanMapCard::Colour& c) { return c.g > 150 && c.r < 140 && c.b < 140; }
bool IsRedish(const PlanMapCard::Colour& c) { return c.r > 180 && c.g < 130 && c.b < 160; }

}  // namespace

TEST(PlanMapCardLines, EveryRoleFallsInALine) {
  EXPECT_EQ(PlanMapCard::LineOf(e_PlayerRole_GK), PlanMapCard::e_Line_Keeper);
  EXPECT_EQ(PlanMapCard::LineOf(e_PlayerRole_CB), PlanMapCard::e_Line_Defence);
  EXPECT_EQ(PlanMapCard::LineOf(e_PlayerRole_LB), PlanMapCard::e_Line_Defence);
  EXPECT_EQ(PlanMapCard::LineOf(e_PlayerRole_RB), PlanMapCard::e_Line_Defence);
  EXPECT_EQ(PlanMapCard::LineOf(e_PlayerRole_DM), PlanMapCard::e_Line_Midfield);
  EXPECT_EQ(PlanMapCard::LineOf(e_PlayerRole_CM), PlanMapCard::e_Line_Midfield);
  EXPECT_EQ(PlanMapCard::LineOf(e_PlayerRole_LM), PlanMapCard::e_Line_Midfield);
  EXPECT_EQ(PlanMapCard::LineOf(e_PlayerRole_RM), PlanMapCard::e_Line_Midfield);
  EXPECT_EQ(PlanMapCard::LineOf(e_PlayerRole_AM), PlanMapCard::e_Line_Midfield);
  EXPECT_EQ(PlanMapCard::LineOf(e_PlayerRole_CF), PlanMapCard::e_Line_Attack);
}

TEST(PlanMapCardLines, TheColoursAreTheOnesTheBroadcastShows) {
  EXPECT_TRUE(IsYellowish(PlanMapCard::LineColour(PlanMapCard::e_Line_Keeper)));
  EXPECT_TRUE(IsCyanish(PlanMapCard::LineColour(PlanMapCard::e_Line_Defence)));
  EXPECT_TRUE(IsGreenish(PlanMapCard::LineColour(PlanMapCard::e_Line_Midfield)));
  EXPECT_TRUE(IsRedish(PlanMapCard::LineColour(PlanMapCard::e_Line_Attack)));
}

TEST(PlanMapCardLines, EachLineIsToldApartFromTheOthers) {
  const PlanMapCard::e_Line lines[] = {PlanMapCard::e_Line_Keeper, PlanMapCard::e_Line_Defence,
                                       PlanMapCard::e_Line_Midfield, PlanMapCard::e_Line_Attack};
  for (int a = 0; a < 4; a++) {
    for (int b = a + 1; b < 4; b++) {
      EXPECT_GT(PlanMapCard::Distance(PlanMapCard::LineColour(lines[a]),
                                      PlanMapCard::LineColour(lines[b])), 60.0f) << "lines " << a << " and " << b
                                                    << " are too close to tell apart";
    }
  }
}

TEST(PlanMapCardAptitude, HisOwnPositionIsNatural) {
  const std::vector<e_PlayerRole> roles = {e_PlayerRole_CB, e_PlayerRole_RB};
  EXPECT_EQ(PlanMapCard::AptitudeFor(e_PlayerRole_CB, roles), PlanMapCard::e_Aptitude_Natural);
  EXPECT_EQ(PlanMapCard::AptitudeFor(e_PlayerRole_RB, roles), PlanMapCard::e_Aptitude_Natural);
}

TEST(PlanMapCardAptitude, AnotherPositionOnTheSameLineIsCloseEnough) {
  const std::vector<e_PlayerRole> roles = {e_PlayerRole_CB};
  EXPECT_EQ(PlanMapCard::AptitudeFor(e_PlayerRole_LB, roles), PlanMapCard::e_Aptitude_SameLine);
}

TEST(PlanMapCardAptitude, AnotherLineIsOutOfPosition) {
  const std::vector<e_PlayerRole> roles = {e_PlayerRole_CB};
  EXPECT_EQ(PlanMapCard::AptitudeFor(e_PlayerRole_CF, roles),
            PlanMapCard::e_Aptitude_OutOfPosition);
  // and a keeper anywhere else is the starkest case of it
  const std::vector<e_PlayerRole> keeper = {e_PlayerRole_GK};
  EXPECT_EQ(PlanMapCard::AptitudeFor(e_PlayerRole_CM, keeper),
            PlanMapCard::e_Aptitude_OutOfPosition);
}

TEST(PlanMapCardAptitude, APlayerWithNoRolesRecordedIsNotAccused) {
  // an empty roster entry is missing data, not a man out of position
  EXPECT_EQ(PlanMapCard::AptitudeFor(e_PlayerRole_CM, {}), PlanMapCard::e_Aptitude_Natural);
}

TEST(PlanMapCardAptitude, TheThreeVerdictsLookDifferent) {
  const PlanMapCard::Colour natural =
      PlanMapCard::AptitudeColour(PlanMapCard::e_Aptitude_Natural);
  const PlanMapCard::Colour same = PlanMapCard::AptitudeColour(PlanMapCard::e_Aptitude_SameLine);
  const PlanMapCard::Colour out =
      PlanMapCard::AptitudeColour(PlanMapCard::e_Aptitude_OutOfPosition);
  EXPECT_GT(PlanMapCard::Distance(natural, same), 40.0f);
  EXPECT_GT(PlanMapCard::Distance(same, out), 40.0f);
  EXPECT_GT(PlanMapCard::Distance(natural, out), 40.0f);
}

TEST(PlanMapCardRating, ARatingPrintsAsAnInteger) {
  EXPECT_EQ(PlanMapCard::RatingText(0.82f), "82");
  EXPECT_EQ(PlanMapCard::RatingText(1.09f), "109");
  EXPECT_EQ(PlanMapCard::RatingText(0.645f), "65");
}

TEST(PlanMapCardRating, AnUnratedPlayerPrintsNothingRatherThanZero) {
  EXPECT_EQ(PlanMapCard::RatingText(0.0f), "");
  EXPECT_EQ(PlanMapCard::RatingText(-1.0f), "");
}

TEST(PlanMapCardRating, ARatingIsNeverWiderThanThreeDigits) {
  // the strip has room for three, and a stat above 1.0 is normal here
  EXPECT_LE(PlanMapCard::RatingText(9.99f).size(), 3u);
  EXPECT_LE(PlanMapCard::RatingText(1.0f).size(), 3u);
}

TEST(PlanMapCardName, AShortNamePrintsWhole) {
  EXPECT_EQ(PlanMapCard::NameText("DANTE", 11), "DANTE");
  EXPECT_EQ(PlanMapCard::NameText("HEATHCLIFF", 11), "HEATHCLIFF");
}

TEST(PlanMapCardName, ALongNameIsCutRatherThanCoveringItsNeighbours) {
  const std::string got = PlanMapCard::NameText("THE FAULT LIES WITH YOU", 11);
  EXPECT_LE(got.size(), 11u);
  EXPECT_EQ(got, "THE FAULT .");
}

TEST(PlanMapCardName, NoBudgetPrintsNothing) {
  EXPECT_EQ(PlanMapCard::NameText("DANTE", 0), "");
}

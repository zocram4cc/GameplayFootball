// How a team's colour survives the trip out of the database.
//
// color1/color2 drive the scoreboard, the crowd banners and the stats overlay.
// Four of the shipped teams do not state them the way the parser expected:
// /lcg/ and /ink/ store "#RRGGBB", and atof("#325ca8") is 0, so both took the
// pitch with a black scoreboard, black banners and a black overlay while the
// data describing their real colours sat right there in the row.

#include <gtest/gtest.h>

#include "base/math/vector3.hpp"
#include "base/utils.hpp"

namespace {

using blunted::GetVectorFromString;
using blunted::Vector3;

TEST(ColourString, ReadsTheCommaSeparatedFormMostTeamsUse) {
  const Vector3 colour = GetVectorFromString("255, 232, 0");
  EXPECT_FLOAT_EQ(colour.coords[0], 255.0f);
  EXPECT_FLOAT_EQ(colour.coords[1], 232.0f);
  EXPECT_FLOAT_EQ(colour.coords[2], 0.0f);
}

TEST(ColourString, ReadsTheHexFormLcgAndInkStore) {
  const Vector3 colour = GetVectorFromString("#325ca8");
  EXPECT_FLOAT_EQ(colour.coords[0], 50.0f);
  EXPECT_FLOAT_EQ(colour.coords[1], 92.0f);
  EXPECT_FLOAT_EQ(colour.coords[2], 168.0f);
}

TEST(ColourString, HexIsCaseInsensitive) {
  const Vector3 lower = GetVectorFromString("#eba4e1");
  const Vector3 upper = GetVectorFromString("#EBA4E1");
  EXPECT_FLOAT_EQ(lower.coords[0], upper.coords[0]);
  EXPECT_FLOAT_EQ(lower.coords[1], upper.coords[1]);
  EXPECT_FLOAT_EQ(lower.coords[2], upper.coords[2]);
  EXPECT_FLOAT_EQ(upper.coords[0], 235.0f);
}

TEST(ColourString, ShorthandHexExpandsEachDigit) {
  // "#abc" is "#aabbcc", as everywhere else that writes colours this way.
  const Vector3 colour = GetVectorFromString("#f0a");
  EXPECT_FLOAT_EQ(colour.coords[0], 255.0f);
  EXPECT_FLOAT_EQ(colour.coords[1], 0.0f);
  EXPECT_FLOAT_EQ(colour.coords[2], 170.0f);
}

TEST(ColourString, WhiteAndBlackSurviveIntact) {
  const Vector3 white = GetVectorFromString("#ffffff");
  EXPECT_FLOAT_EQ(white.coords[0], 255.0f);
  EXPECT_FLOAT_EQ(white.coords[2], 255.0f);
  const Vector3 black = GetVectorFromString("#000000");
  EXPECT_FLOAT_EQ(black.coords[0], 0.0f);
  EXPECT_FLOAT_EQ(black.coords[2], 0.0f);
}

TEST(ColourString, SomethingThatIsNotHexDoesNotBecomeANumber) {
  // Better a visible black than a colour invented out of a typo.
  const Vector3 colour = GetVectorFromString("#zzzzzz");
  EXPECT_FLOAT_EQ(colour.coords[0], 0.0f);
  EXPECT_FLOAT_EQ(colour.coords[1], 0.0f);
  EXPECT_FLOAT_EQ(colour.coords[2], 0.0f);
}

TEST(ColourString, AHashOfTheWrongLengthFallsBackRatherThanReadingGarbage) {
  const Vector3 colour = GetVectorFromString("#12345");
  EXPECT_FLOAT_EQ(colour.coords[0], 0.0f);
}

}  // namespace

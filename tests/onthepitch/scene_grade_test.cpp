// Which of PES's colour grading tables a match is graded through.
//
// PES runs every frame through a 33-cubed lookup table chosen by time of day and
// weather, and the absence of that grade is why an imported stadium looks flat
// beside the broadcast: against the VGL26 reference our midtones measured 1.68x
// low (median 74 against 124) while our highlights were already hotter and our
// shadows 13x more crushed - a missing tone curve, not a missing light. PES's own
// day table lifts mid grey 0.5 to 0.616 and rolls 0.75..1.0 into 0.666..0.689,
// which takes our pitch from 26/92/110 to 19/132/152 against the reference's
// 32/139/197.
//
// tools/pes21_import/lut_strip.py unrolls the tables into one PNG, four bands in
// a documented order; this picks the band. The order has to agree with that
// script, so both name it in the same words.

#include <gtest/gtest.h>

#include "systems/graphics/scenegrade.hpp"

TEST(SceneGrade, TheBandsAreTheOnesTheImporterWrites) {
  // lut_strip.py BAND_ORDER: day, cloudy, evening, night
  EXPECT_EQ(SceneGrade::kBandCount, 4);
  EXPECT_EQ(SceneGrade::e_Band_Day, 0);
  EXPECT_EQ(SceneGrade::e_Band_Cloudy, 1);
  EXPECT_EQ(SceneGrade::e_Band_Evening, 2);
  EXPECT_EQ(SceneGrade::e_Band_Night, 3);
}

TEST(SceneGrade, AFineAfternoonIsGradedAsDay) {
  // the reference broadcast: match_time_of_day 0, match_weather 0
  EXPECT_EQ(SceneGrade::BandForConditions(0.0f, 0.0f), SceneGrade::e_Band_Day);
}

TEST(SceneGrade, RainInTheDaytimeTakesTheCloudyTable) {
  EXPECT_EQ(SceneGrade::BandForConditions(0.0f, 0.6f), SceneGrade::e_Band_Cloudy);
  EXPECT_EQ(SceneGrade::BandForConditions(0.0f, 1.0f), SceneGrade::e_Band_Cloudy);
}

TEST(SceneGrade, TheEveningAndNightTablesFollowTheEnginesOwnScale) {
  // match_time_of_day is 0 day, 0.5 evening, 1 night (Match::SetRandomSunParams)
  EXPECT_EQ(SceneGrade::BandForConditions(0.5f, 0.0f), SceneGrade::e_Band_Evening);
  EXPECT_EQ(SceneGrade::BandForConditions(1.0f, 0.0f), SceneGrade::e_Band_Night);
}

TEST(SceneGrade, WeatherDoesNotOverrideTheHourOnceTheSunIsDown) {
  // PES's cloudy table is a daylight grade; a wet night match is still a night.
  EXPECT_EQ(SceneGrade::BandForConditions(1.0f, 1.0f), SceneGrade::e_Band_Night);
  EXPECT_EQ(SceneGrade::BandForConditions(0.5f, 1.0f), SceneGrade::e_Band_Evening);
}

TEST(SceneGrade, ValuesOffTheEndOfTheSlidersStillPickARealBand) {
  EXPECT_EQ(SceneGrade::BandForConditions(-1.0f, -1.0f), SceneGrade::e_Band_Day);
  EXPECT_EQ(SceneGrade::BandForConditions(2.0f, 2.0f), SceneGrade::e_Band_Night);
}

TEST(SceneGrade, TheStripIsWhereTheImporterPutIt) {
  EXPECT_STREQ(SceneGrade::kStripPath, "media/textures/lut/grade.png");
}

TEST(SceneGrade, TheStripsShapeIsReadOffTheImageRatherThanConfigured) {
  // 33 slices of 33x33, four bands: 1089 x 132, which is what lut_strip.py wrote.
  int size = 0, bands = 0;
  ASSERT_TRUE(SceneGrade::StripDimensions(1089, 132, size, bands));
  EXPECT_EQ(size, 33);
  EXPECT_EQ(bands, 4);
}

TEST(SceneGrade, ASingleBandStripIsFine) {
  int size = 0, bands = 0;
  ASSERT_TRUE(SceneGrade::StripDimensions(1089, 33, size, bands));
  EXPECT_EQ(size, 33);
  EXPECT_EQ(bands, 1);
}

TEST(SceneGrade, AnotherTableSizeWorksToo) {
  // 16-cubed tables are the other common size; nothing here is 33-specific.
  int size = 0, bands = 0;
  ASSERT_TRUE(SceneGrade::StripDimensions(256, 32, size, bands));
  EXPECT_EQ(size, 16);
  EXPECT_EQ(bands, 2);
}

TEST(SceneGrade, APictureThatIsNotAStripIsRefusedRatherThanSampledAsGarbage) {
  // Grading through some unrelated PNG would tint the whole match.
  int size = 0, bands = 0;
  EXPECT_FALSE(SceneGrade::StripDimensions(800, 600, size, bands));   // width not a square
  EXPECT_FALSE(SceneGrade::StripDimensions(1089, 40, size, bands));   // height not whole bands
  EXPECT_FALSE(SceneGrade::StripDimensions(0, 0, size, bands));
  EXPECT_FALSE(SceneGrade::StripDimensions(1089, 0, size, bands));
  EXPECT_FALSE(SceneGrade::StripDimensions(1, 1, size, bands));  // a single texel is not a table
}

// What the pre-match screen can offer, derived from what is actually installed.
//
// The screen already sets the weather, the time of day, the kits, the difficulty
// and the duration, and opens each side's game plan. It cannot pick the stadium,
// nor which entrance plays, nor which post-match presentation follows - all three
// exist in the engine and are reachable only by editing a config file.
//
// PES's own pre-match screen is the reference (docs/VGL26_REFERENCE.md): a row of
// Strip / Stadium / Kick Off / Game Plan / General Rigging / Camera buttons.
//
// The lists come from the filesystem, so the discovery lives at the call site and
// the shaping - what counts, what it is called, what order it comes in - lives
// here where it can be tested.

#include <gtest/gtest.h>

#include "menu/prematchchoices.hpp"

namespace {

std::vector<std::string> Labels(const std::vector<PrematchChoices::Choice>& choices) {
  std::vector<std::string> labels;
  for (const auto& choice : choices) labels.push_back(choice.label);
  return labels;
}

std::vector<std::string> Values(const std::vector<PrematchChoices::Choice>& choices) {
  std::vector<std::string> values;
  for (const auto& choice : choices) values.push_back(choice.value);
  return values;
}

}  // namespace

TEST(PrematchStadiums, EachStadiumIsNamedAfterItsDirectory) {
  const std::vector<std::string> paths = {
      "media/objects/stadiums/pes_st017/pes_st017.object",
      "media/objects/stadiums/test/test.object",
  };
  const auto choices = PrematchChoices::Stadiums(paths);
  EXPECT_EQ(Labels(choices), (std::vector<std::string>{"pes_st017", "test"}));
  EXPECT_EQ(Values(choices), paths);
}

TEST(PrematchStadiums, TheListIsSortedSoTheOrderIsStableBetweenRuns) {
  const auto choices = PrematchChoices::Stadiums({
      "media/objects/stadiums/pes_st060/pes_st060.object",
      "media/objects/stadiums/pes_st002/pes_st002.object",
      "media/objects/stadiums/pes_st011/pes_st011.object",
  });
  EXPECT_EQ(Labels(choices), (std::vector<std::string>{"pes_st002", "pes_st011", "pes_st060"}));
}

TEST(PrematchStadiums, TheSkyDomeIsNotAStadium) {
  // media/objects/stadiums/sky is the optional dome a stadium can add, not
  // somewhere to play; offering it would load a match with no pitch around it.
  const auto choices = PrematchChoices::Stadiums({
      "media/objects/stadiums/sky/sky.object",
      "media/objects/stadiums/test/test.object",
  });
  EXPECT_EQ(Labels(choices), (std::vector<std::string>{"test"}));
}

TEST(PrematchStadiums, NothingInstalledIsAnEmptyList) {
  EXPECT_TRUE(PrematchChoices::Stadiums({}).empty());
}

TEST(PrematchEntrances, AnyComesFirstAndNoneComesLast) {
  // "" lets the engine pick a family by competition and stadium, which is what
  // it does today; "none" skips the walkout entirely. Both have to stay reachable.
  const auto choices = PrematchChoices::Entrances({"020", "009", "001"});
  EXPECT_EQ(Values(choices), (std::vector<std::string>{"", "001", "009", "020", "none"}));
  EXPECT_EQ(choices.front().label, "entrance_any");
  EXPECT_EQ(choices.back().label, "entrance_none");
}

TEST(PrematchEntrances, WithNothingInstalledThereIsStillSomethingToChoose) {
  EXPECT_EQ(Values(PrematchChoices::Entrances({})), (std::vector<std::string>{"", "none"}));
}

TEST(PrematchResultCutscenes, FamiliesAreReadOffTheFileNamesAndDeduplicated) {
  // The post-match pool is flat, with the family in the name.
  const auto choices = PrematchChoices::ResultCutscenes({
      "result_001_st000_cam1.camtrack",
      "result_001_st000_cam2_large.camtrack",
      "result_012_st002_aerial_cam_df.camtrack",
  });
  EXPECT_EQ(Values(choices), (std::vector<std::string>{"", "001", "012"}));
}

TEST(PrematchResultCutscenes, AFileWithNoFamilyInItsNameIsIgnored) {
  const auto choices = PrematchChoices::ResultCutscenes({"end_audience_st000_L.camtrack"});
  EXPECT_EQ(Values(choices), (std::vector<std::string>{""}));
}

TEST(FamilyFromCamtrackName, ThePesFamilyNumberIsTheSecondToken) {
  EXPECT_EQ(PrematchChoices::FamilyFromCamtrackName("result_001_st000_cam1.camtrack"), "001");
  EXPECT_EQ(PrematchChoices::FamilyFromCamtrackName("result_012_st002_aerial.camtrack"), "012");
}

TEST(FamilyFromCamtrackName, ANameWithoutANumberedFamilyHasNone) {
  EXPECT_EQ(PrematchChoices::FamilyFromCamtrackName("end_audience_st000_L.camtrack"), "");
  EXPECT_EQ(PrematchChoices::FamilyFromCamtrackName(""), "");
  EXPECT_EQ(PrematchChoices::FamilyFromCamtrackName("result.camtrack"), "");
}

TEST(PrematchSlider, AnIndexSurvivesTheRoundTripThroughASliderPosition) {
  for (int count = 1; count <= 20; count++) {
    for (int index = 0; index < count; index++) {
      const float value = PrematchChoices::SliderFromIndex(index, count);
      EXPECT_GE(value, 0.0f);
      EXPECT_LE(value, 1.0f);
      EXPECT_EQ(PrematchChoices::IndexFromSlider(value, count), index)
          << "index " << index << " of " << count;
    }
  }
}

TEST(PrematchSlider, AnOutOfRangeSliderIsClampedRatherThanIndexingPastTheList) {
  EXPECT_EQ(PrematchChoices::IndexFromSlider(-1.0f, 5), 0);
  EXPECT_EQ(PrematchChoices::IndexFromSlider(2.0f, 5), 4);
  EXPECT_EQ(PrematchChoices::IndexFromSlider(0.5f, 0), 0);  // empty list
}

TEST(PrematchIndexOfValue, TheConfiguredValueIsWhereTheSliderStarts) {
  const std::vector<PrematchChoices::Choice> choices = {
      {"a", "media/a.object"}, {"b", "media/b.object"}, {"c", "media/c.object"}};
  EXPECT_EQ(PrematchChoices::IndexOfValue(choices, "media/b.object"), 1);
}

TEST(PrematchIndexOfValue, SomethingNotInstalledStartsAtTheBeginningRatherThanNowhere) {
  const std::vector<PrematchChoices::Choice> choices = {{"a", "media/a.object"}};
  EXPECT_EQ(PrematchChoices::IndexOfValue(choices, "media/gone.object"), 0);
  EXPECT_EQ(PrematchChoices::IndexOfValue({}, "anything"), 0);
}

// The pre-match stadium slider offered "ADBOARDS" as somewhere to play: the
// hoarding ring is furniture every ground borrows, not a venue (owner, 04-09).
TEST(PrematchChoicesTest, TheFurnitureDirectoriesAreNotStadiums) {
  const std::vector<std::string> paths = {
      "media/objects/stadiums/pes_st002/pes_st002.object",
      "media/objects/stadiums/adboards/adboards.object",
      "media/objects/stadiums/sky/sky.object",
      "media/objects/stadiums/goals/goals.object",
      "media/objects/stadiums/pes_st017/pes_st017.object",
  };
  const std::vector<PrematchChoices::Choice> choices = PrematchChoices::Stadiums(paths);
  ASSERT_EQ(choices.size(), 2u);
  EXPECT_EQ(choices.at(0).label, "pes_st002");
  EXPECT_EQ(choices.at(1).label, "pes_st017");
}

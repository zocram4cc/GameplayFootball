// Which tactics belong on a slider.
//
// The game plan builds its tactics sliders by walking every key in the team's
// userProperties, captioning each from humanReadableNames and falling back to the
// raw key. That is fine while every key is a number, and philosophy is not: the
// philosophy menu writes the *string* "balanced" into the same map
// (GamePlanPage::PhilosophyMenuOnClick), so choosing a philosophy grows an extra
// slider in the tactics menu - unmarked, because there is no readable name for it,
// sitting at zero because GetReal of "balanced" is zero, and dragging it writes a
// number over the philosophy the button had just set.
//
// So the slider list asks this instead of taking every key it finds.

#include "onthepitch/teamphilosophy.hpp"

#include <gtest/gtest.h>

TEST(SliderTactics, ANumericTacticBelongsOnASlider) {
  EXPECT_TRUE(TeamPhilosophy::IsSliderTactic("team_pressure"));
  EXPECT_TRUE(TeamPhilosophy::IsSliderTactic("position_offense_depth_factor"));
  EXPECT_TRUE(TeamPhilosophy::IsSliderTactic("dribble_centermagnet"));
  EXPECT_TRUE(TeamPhilosophy::IsSliderTactic("counter_attack"));
}

TEST(SliderTactics, PhilosophyDoesNot) {
  // it has the philosophy menu, and it is not a number
  EXPECT_FALSE(TeamPhilosophy::IsSliderTactic("philosophy"));
}

TEST(SliderTactics, AnUnknownKeyIsStillAllowed) {
  // a team may carry tactics this build has never heard of; they are numbers and
  // belong on sliders like the rest. Only the keys with their own editor are refused.
  EXPECT_TRUE(TeamPhilosophy::IsSliderTactic("some_future_slider"));
}

TEST(SliderTactics, TheCheckIsExactRatherThanASubstring) {
  // "philosophy_note" is not the philosophy key
  EXPECT_TRUE(TeamPhilosophy::IsSliderTactic("philosophy_note"));
  EXPECT_TRUE(TeamPhilosophy::IsSliderTactic("myphilosophy"));
}

TEST(SliderTactics, AnEmptyKeyIsRefused) {
  EXPECT_FALSE(TeamPhilosophy::IsSliderTactic(""));
}

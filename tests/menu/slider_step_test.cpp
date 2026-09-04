#include "utils/gui2/widgets/sliderstep.hpp"

#include <gtest/gtest.h>

// The owner's complaint: sliders in the game plan and the pre-match are
// ambiguous - no way to tell how many values one has, or which is set. A
// slider now carries its step count, prints the step, and draws a tick each.

using namespace blunted;

TEST(SliderStepTest, TheEndsAreTheFirstAndLastStep) {
  EXPECT_EQ(SliderStep::IndexFor(0.0f, 11), 1);
  EXPECT_EQ(SliderStep::IndexFor(1.0f, 11), 11);
  EXPECT_EQ(SliderStep::IndexFor(0.5f, 11), 6);
}

TEST(SliderStepTest, TheIndexAgreesWithWhereTheBarIsDrawn) {
  // The widget quantises with round(value * (steps - 1)); the label has to
  // land on the same position or it contradicts the picture.
  const int steps = 7;
  for (int step = 0; step < steps; step++) {
    const float value = step / (float)(steps - 1);
    EXPECT_EQ(SliderStep::IndexFor(value, steps), step + 1) << "step " << step;
  }
}

TEST(SliderStepTest, ValuesOffTheScaleAreClampedNotWrapped) {
  EXPECT_EQ(SliderStep::IndexFor(-0.4f, 5), 1);
  EXPECT_EQ(SliderStep::IndexFor(1.7f, 5), 5);
}

TEST(SliderStepTest, ALabelIsStepOfTotal) {
  EXPECT_EQ(SliderStep::Label(0.0f, 3), "1/3");
  EXPECT_EQ(SliderStep::Label(1.0f, 3), "3/3");
}

TEST(SliderStepTest, TooManyStepsToCountDrawsAsAPlainSlider) {
  // 51 is the widget's own default - a continuous-feeling slider. Ticks that
  // close together are noise, and "26/51" tells a reader nothing.
  EXPECT_FALSE(SliderStep::DrawsTicks(51));
  EXPECT_EQ(SliderStep::Label(0.5f, 51), "");
  EXPECT_TRUE(SliderStep::DrawsTicks(SliderStep::kMaxDrawnSteps));
  EXPECT_FALSE(SliderStep::DrawsTicks(SliderStep::kMaxDrawnSteps + 1));
}

TEST(SliderStepTest, ASingleStepSliderIsNotDrawnWithTicks) {
  EXPECT_FALSE(SliderStep::DrawsTicks(1));
  EXPECT_EQ(SliderStep::IndexFor(0.5f, 1), 1);
}

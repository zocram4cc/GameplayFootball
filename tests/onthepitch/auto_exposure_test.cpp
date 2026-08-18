// How fast the exposure is allowed to change.
//
// The first version measured the frame in the shader and applied the result the same
// frame, with no memory of what it did last time. Sixteen taps at fixed screen
// positions see completely different things as a camera moves, so the gain jumped
// every frame: measured off the recorded match, the picture's mean brightness moved
// by 0.02 frame to frame through the opening cutscene, with single-frame jumps of
// -0.073 and +0.038. It read as a flicker on every cut and pan.
//
// A real exposure adapts the way an eye does: slowly, toward what it currently sees.
// That needs a value carried from frame to frame, which a fragment shader has none
// of, so the measuring and the smoothing both live on this side and the shader is
// handed a single gain.

#include <gtest/gtest.h>

#include <cmath>

#include "systems/graphics/rendering/autoexposure.hpp"

namespace {
constexpr float kKey = 0.45f;
constexpr float kMin = 0.55f;
constexpr float kMax = 1.6f;
constexpr float kHalfLife = 0.8f;
}  // namespace

TEST(AutoExposure, AFrameOnTheKeyWantsNoCorrection) {
  EXPECT_NEAR(AutoExposure::TargetGain(kKey, kKey, kMin, kMax), 1.0f, 0.001f);
}

TEST(AutoExposure, ADarkFrameWantsMoreLight) {
  EXPECT_GT(AutoExposure::TargetGain(0.30f, kKey, kMin, kMax), 1.0f);
}

TEST(AutoExposure, ABrightFrameWantsLess) {
  EXPECT_LT(AutoExposure::TargetGain(0.60f, kKey, kMin, kMax), 1.0f);
}

TEST(AutoExposure, TheGainStaysInsideItsBounds) {
  EXPECT_FLOAT_EQ(AutoExposure::TargetGain(0.001f, kKey, kMin, kMax), kMax);
  EXPECT_FLOAT_EQ(AutoExposure::TargetGain(1.0f, kKey, kMin, kMax), kMin);
}

TEST(AutoExposure, AnUnmeasuredFrameAsksForNothing) {
  // before the first readback lands, and if one ever fails
  EXPECT_FLOAT_EQ(AutoExposure::TargetGain(-1.0f, kKey, kMin, kMax), 1.0f);
}

// The correction is applied in linear light while the key is a displayed brightness,
// so the ratio goes through the transfer: half the light is not half the brightness.
TEST(AutoExposure, TheCorrectionIsInLinearLight) {
  const float gain = AutoExposure::TargetGain(0.225f, 0.45f, 0.0f, 100.0f);
  EXPECT_NEAR(gain, std::pow(2.0f, 2.2f), 0.01f);
}

TEST(AutoExposure, OneFrameMovesItOnlyPartOfTheWay) {
  // 1/60 s against a 0.8 s half-life: a couple of per cent, not the whole jump
  const float moved = AutoExposure::Adapt(1.0f, 2.0f, 1.0f / 60.0f, kHalfLife);
  EXPECT_GT(moved, 1.0f);
  EXPECT_LT(moved, 1.03f);
}

TEST(AutoExposure, AHalfLifeCoversHalfTheDistance) {
  EXPECT_NEAR(AutoExposure::Adapt(1.0f, 2.0f, kHalfLife, kHalfLife), 1.5f, 0.001f);
  EXPECT_NEAR(AutoExposure::Adapt(1.0f, 2.0f, kHalfLife * 2.0f, kHalfLife), 1.75f, 0.001f);
}

TEST(AutoExposure, ItGetsThereEventually) {
  float gain = 1.0f;
  for (int frame = 0; frame < 600; ++frame)
    gain = AutoExposure::Adapt(gain, 1.4f, 1.0f / 60.0f, kHalfLife);
  EXPECT_NEAR(gain, 1.4f, 0.001f);
}

TEST(AutoExposure, ALongStallDoesNotOvershoot) {
  const float moved = AutoExposure::Adapt(1.0f, 2.0f, 30.0f, kHalfLife);
  EXPECT_LE(moved, 2.0f);
  EXPECT_GT(moved, 1.99f);
}

TEST(AutoExposure, GoingDownIsJustAsSlow) {
  const float moved = AutoExposure::Adapt(1.5f, 1.0f, 1.0f / 60.0f, kHalfLife);
  EXPECT_LT(moved, 1.5f);
  EXPECT_GT(moved, 1.47f);
}

TEST(AutoExposure, NoTimeMeansNoMovement) {
  EXPECT_FLOAT_EQ(AutoExposure::Adapt(1.2f, 0.5f, 0.0f, kHalfLife), 1.2f);
  EXPECT_FLOAT_EQ(AutoExposure::Adapt(1.2f, 0.5f, -1.0f, kHalfLife), 1.2f);
}

TEST(AutoExposure, AnInstantHalfLifeSnaps) {
  // 0 turns the smoothing off rather than dividing by it
  EXPECT_FLOAT_EQ(AutoExposure::Adapt(1.0f, 2.0f, 1.0f / 60.0f, 0.0f), 2.0f);
}

// What the renderer hands over: the mean displayed brightness of a window of pixels.
TEST(AutoExposure, BrightnessIsMeasuredAsDisplayed) {
  // mid grey bytes: 128/255 is 0.502 displayed, whatever it was in linear
  const unsigned char grey[4] = {128, 128, 128, 255};
  EXPECT_NEAR(AutoExposure::MeanDisplayedLuminance(grey, 1), 0.502f, 0.005f);
}

TEST(AutoExposure, ItWeighsTheChannelsTheWayAnEyeDoes) {
  const unsigned char green[4] = {0, 255, 0, 255};
  const unsigned char blue[4] = {0, 0, 255, 255};
  EXPECT_GT(AutoExposure::MeanDisplayedLuminance(green, 1),
            AutoExposure::MeanDisplayedLuminance(blue, 1));
}

TEST(AutoExposure, NoPixelsIsNotAMeasurement) {
  EXPECT_LT(AutoExposure::MeanDisplayedLuminance(nullptr, 0), 0.0f);
}

#include "autoexposure.hpp"

#include <atomic>
#include <cmath>

namespace AutoExposure {

namespace {
// One float across two threads, which is all the sharing this needs.
std::atomic<float> latestMeasurement{-1.0f};
}  // namespace

void SetMeasuredBrightness(float displayed) {
  latestMeasurement.store(displayed, std::memory_order_relaxed);
}

float GetMeasuredBrightness() { return latestMeasurement.load(std::memory_order_relaxed); }

float MeanDisplayedLuminance(const unsigned char* rgba, size_t count) {
  if (!rgba || count == 0) return -1.0f;
  double sum = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const unsigned char* pixel = rgba + i * 4;
    sum += (0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2]) / 255.0;
  }
  return static_cast<float>(sum / static_cast<double>(count));
}

float TargetGain(float measuredDisplayed, float key, float minGain, float maxGain) {
  if (measuredDisplayed < 0.0f) return 1.0f;   // nothing measured yet
  const float measured = measuredDisplayed < 0.0001f ? 0.0001f : measuredDisplayed;
  // The key is a displayed brightness and the gain multiplies linear light, so the
  // ratio goes through the transfer rather than being used as it stands.
  float gain = std::pow(key / measured, 2.2f);
  if (gain < minGain) gain = minGain;
  if (gain > maxGain) gain = maxGain;
  return gain;
}

float Adapt(float currentGain, float targetGain, float dt_seconds, float halfLife_seconds) {
  if (dt_seconds <= 0.0f) return currentGain;
  if (halfLife_seconds <= 0.0f) return targetGain;
  const float travelled = 1.0f - std::exp2(-dt_seconds / halfLife_seconds);
  return currentGain + (targetGain - currentGain) * travelled;
}

}  // namespace AutoExposure

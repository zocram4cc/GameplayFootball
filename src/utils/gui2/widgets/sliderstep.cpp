#include "sliderstep.hpp"

#include <algorithm>
#include <cmath>

namespace blunted {
namespace SliderStep {

int IndexFor(float value, int steps) {
  if (steps < 2) return 1;
  const float clamped = std::min(1.0f, std::max(0.0f, value));
  // The widget quantises with round(value * (steps - 1)), so the label has to
  // agree with the position the bar is drawn at rather than re-deriving it.
  const int zeroBased = (int)std::lround(clamped * (float)(steps - 1));
  return std::min(steps, std::max(1, zeroBased + 1));
}

std::string Label(float value, int steps) {
  if (!DrawsTicks(steps)) return "";
  return std::to_string(IndexFor(value, steps)) + "/" + std::to_string(steps);
}

bool DrawsTicks(int steps) { return steps >= 2 && steps <= kMaxDrawnSteps; }

}  // namespace SliderStep
}  // namespace blunted

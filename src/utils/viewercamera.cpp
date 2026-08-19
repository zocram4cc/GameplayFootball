#include "utils/viewercamera.hpp"

#include <algorithm>
#include <cmath>

namespace blunted {
namespace ViewerCamera {

namespace {
constexpr float kPi = 3.14159265358979f;
}

Shot Frame(const std::array<float, 3>& low, const std::array<float, 3>& high, float fov) {
  Shot shot;
  shot.fov = fov > 1.0f ? fov : 35.0f;
  for (int c = 0; c < 3; c++) shot.target[c] = (low[c] + high[c]) * 0.5f;

  const float width = high[0] - low[0];
  const float depth = high[1] - low[1];
  const float height = high[2] - low[2];
  // The radius that has to fit: half the largest span, and never zero - a mesh that
  // arrived empty must still give a usable shot rather than a division by its size.
  const float radius = std::max({width, depth, height, 0.2f}) * 0.5f;
  const float halfLens = std::tan(shot.fov * 0.5f * kPi / 180.0f);
  // A little air round it, so the model is not jammed against the frame edge.
  shot.distance = radius / std::max(0.01f, halfLens) * 1.35f;
  return Clamp(shot);
}

std::array<float, 3> Position(const Shot& shot) {
  const float horizontal = shot.distance * std::cos(shot.pitch);
  return {shot.target[0] + horizontal * std::sin(shot.yaw),
          shot.target[1] - horizontal * std::cos(shot.yaw),
          shot.target[2] + shot.distance * std::sin(shot.pitch)};
}

Shot Clamp(const Shot& shot) {
  Shot out = shot;
  out.pitch = std::max(-kMaxPitch, std::min(kMaxPitch, out.pitch));
  out.distance = std::max(kMinDistance, out.distance);
  return out;
}

float TurntableYaw(const Shot& shot, int index, int frames) {
  if (frames <= 1) return shot.yaw;
  return shot.yaw + 2.0f * kPi * static_cast<float>(index) / static_cast<float>(frames);
}

}  // namespace ViewerCamera
}  // namespace blunted

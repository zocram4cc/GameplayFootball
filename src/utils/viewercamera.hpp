// The orbit camera a standalone model viewer needs.
//
// Troubleshooting a model inside a running match is the wrong tool: it loads a
// stadium, a crowd, 22 players and a presentation before you can look at one mesh.
// This camera orbits a model, frames it from its own bounds, and has no idea a pitch
// exists. It is deliberately reusable rather than debug-only, because an EDIT mode
// will want exactly the same thing.

#ifndef _HPP_UTILS_VIEWERCAMERA
#define _HPP_UTILS_VIEWERCAMERA

#include <array>

namespace blunted {
namespace ViewerCamera {

struct Shot {
  std::array<float, 3> target{{0.0f, 0.0f, 0.0f}};
  float distance = 1.0f;
  float yaw = 0.0f;    // radians about world Z; 0 puts the camera on -y
  float pitch = 0.2f;  // radians above the horizon
  float fov = 35.0f;
};

// How close to straight up or down the camera may be pitched before the up vector
// stops meaning anything.
constexpr float kMaxPitch = 1.5f;
// Never let a zoom pass through the model.
constexpr float kMinDistance = 0.05f;

// A shot that fits a model's bounding box in frame at `fov` degrees.
Shot Frame(const std::array<float, 3>& low, const std::array<float, 3>& high, float fov);

// Where the camera sits, given the orbit.
std::array<float, 3> Position(const Shot& shot);

// The same shot with its pitch and distance brought back into usable range.
Shot Clamp(const Shot& shot);

// Yaw for frame `index` of a `frames`-long turntable: one full revolution, which is
// what a headless viewer writing stills wants.
float TurntableYaw(const Shot& shot, int index, int frames);

}  // namespace ViewerCamera
}  // namespace blunted

#endif

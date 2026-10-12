#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <optional>

#include "gdext/pes_library.hpp"
#include "pes/camera.hpp"

namespace godot {

// PES's in-play Dynamic Wide camera (docs/re/pes-re-stadium-camera.md 4.3-4.6),
// driving a Camera3D from a focus point (the ball).
class PesBroadcastCamera : public RefCounted {
  GDCLASS(PesBroadcastCamera, RefCounted)

 public:
  Error load(const Ref<PesLibrary>& library, double field_half_width);
  void set_sliders(double zoom, double height);  // the user's camera sliders, [0, 1]
  void reset(const Vector3& focus);              // snap: target = focus, k at rest
  void update(Camera3D* camera, const Vector3& focus, double delta);

 protected:
  static void _bind_methods();

 private:
  void Step(const Vector3& focus);
  std::optional<pes::DynamicWideParams> params_;
  float halfWidth_ = 0.0f;
  float zoom_, height_;
  Vector3 target_;
  pes::ZoomSpring spring_{1.0f, 0.0f};
  double accumulator_ = 0.0;
};

}  // namespace godot

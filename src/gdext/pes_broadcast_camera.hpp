#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <optional>

#include "gdext/pes_library.hpp"
#include "pes/camera.hpp"

namespace godot {

// PES's in-play Dynamic Wide camera (docs/re/pes-re-camera-target.md,
// docs/re/pes-re-stadium-camera.md 4.3-4.6), driving a Camera3D from a focus
// point (the ball). Every camera step is one rendered frame at PES's 54 Hz
// reference (report CameraTargetRE item 3), fixed-rate, so the smoothing, the
// k spring and the ball-height lift all run at PES's speed regardless of how
// fast the engine renders.
class PesBroadcastCamera : public RefCounted {
  GDCLASS(PesBroadcastCamera, RefCounted)

 public:
  Error load(const Ref<PesLibrary>& library, double field_half_width, double field_half_length);
  void set_sliders(double zoom, double height);  // the user's camera sliders, [0, 1]
  // The camera-moving margin time: while it is active the smoothing weight
  // grows to (start + up) per frame, otherwise it decays back to the floor.
  // Defaults to false, as GF's sim has no such state yet (report
  // CameraTargetRE 3a: [rbp-0x40]==2 && [r15+0x11d4]!=2 && byte[rbp-0x7e]!=0).
  void set_margin_time_active(bool active);
  void reset(const Vector3& focus);  // snap: target = focus, k at rest
  void update(Camera3D* camera, const Vector3& focus, double delta);

 protected:
  static void _bind_methods();

 private:
  pes::CameraShot Shot() const;
  std::optional<pes::DynamicWideParams> params_;
  float halfWidth_ = 0.0f, halfLength_ = 0.0f;
  float zoom_, height_;
  float stepS_ = 1.0f / 54.0f;   // PES's frame-rate reference, read from the exe
  float liftTimeScale_ = 1.0f;   // 60 / FpsRef (report CameraTargetRE item 1)
  bool marginTimeActive_ = false;
  Vector3 target_;
  pes::TargetSmoothing smoothing_;
  pes::ZoomSpring spring_;
  double accumulator_ = 0.0;
};

}  // namespace godot

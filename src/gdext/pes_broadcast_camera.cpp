#include "gdext/pes_broadcast_camera.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

namespace godot {
namespace {
// Owner's Dynamic Wide sliders, fitted to two live poses (report 4.6).
// ponytail: provisional until the stored/default slider values are read
// (CameraTargetRE item 5).
constexpr float LIVE_FIT_ZOOM = 0.79f, LIVE_FIT_HEIGHT = 0.20f;
// Target smoothing tgt = (raw + w*prev)/(w + 1), default w (report 4.3).
constexpr float TARGET_SMOOTHING_WEIGHT = 10.0f;
// ponytail: smoothing/spring step rate not yet pinned to PES's (render frame or
// logic tick; CameraTargetRE item 3); 60 Hz provisional.
constexpr double CAMERA_STEP_S_PROVISIONAL = 1.0 / 60.0;
constexpr float ZOOM_GOAL = 1.0f;          // k goal in open play (report 4.3)
constexpr double NEAR_PLANE_M = 0.9;       // InplayCamera::update, modes other than 2/3/4
constexpr double FAR_PLANE_M = 4000.0;     // report 7 recipe
constexpr int MAX_STEPS_PER_UPDATE = 30;
}  // namespace

void PesBroadcastCamera::_bind_methods() {
  ClassDB::bind_method(D_METHOD("load", "library", "field_half_width"), &PesBroadcastCamera::load);
  ClassDB::bind_method(D_METHOD("set_sliders", "zoom", "height"), &PesBroadcastCamera::set_sliders);
  ClassDB::bind_method(D_METHOD("reset", "focus"), &PesBroadcastCamera::reset);
  ClassDB::bind_method(D_METHOD("update", "camera", "focus", "delta"), &PesBroadcastCamera::update);
}

Error PesBroadcastCamera::load(const Ref<PesLibrary>& library, double field_half_width) {
  const auto* params = library.is_valid() ? library->camera_params() : nullptr;
  if (!params) {
    UtilityFunctions::push_error("PesBroadcastCamera: library lacks the camera tables");
    return ERR_UNCONFIGURED;
  }
  params_ = *params;
  halfWidth_ = float(field_half_width);
  zoom_ = LIVE_FIT_ZOOM;
  height_ = LIVE_FIT_HEIGHT;
  return OK;
}

void PesBroadcastCamera::set_sliders(double zoom, double height) {
  zoom_ = float(zoom);
  height_ = float(height);
}

void PesBroadcastCamera::reset(const Vector3& focus) {
  target_ = focus;
  spring_ = {ZOOM_GOAL, 0.0f};
  accumulator_ = 0.0;
}

void PesBroadcastCamera::Step(const Vector3& focus) {
  target_ = (focus + target_ * TARGET_SMOOTHING_WEIGHT) / (TARGET_SMOOTHING_WEIGHT + 1.0f);
  spring_.Step(ZOOM_GOAL);
}

void PesBroadcastCamera::update(Camera3D* camera, const Vector3& focus, double delta) {
  if (!params_ || !camera) return;
  accumulator_ += delta;
  for (int i = 0; accumulator_ >= CAMERA_STEP_S_PROVISIONAL && i < MAX_STEPS_PER_UPDATE; ++i) {
    Step(focus);
    accumulator_ -= CAMERA_STEP_S_PROVISIONAL;
  }
  // t reads the clamped target z: the live 23.26 deg lens fits t from z = -8
  // (the mode-5 Z_min), not from the unclamped focus (report 4.6).
  const float tz = std::clamp(float(target_.z), params_->zMin, params_->zMax);
  const auto shot = pes::DynamicWideShot(*params_, zoom_, height_, tz, halfWidth_);
  // ponytail: the ball-height lift f(ball height) is unresolved (CameraTargetRE item 1); none yet.
  const auto pose = pes::FramePose(shot, target_.x, target_.z, 0.0f, spring_.k);
  camera->set_keep_aspect_mode(Camera3D::KEEP_HEIGHT);  // PES's FOV is vertical
  camera->set_fov(pose.vfovDeg);
  camera->set_near(NEAR_PLANE_M);
  camera->set_far(FAR_PLANE_M);
  camera->look_at_from_position(Vector3(pose.cx, pose.cy, pose.cz), Vector3(pose.tx, pose.ty, pose.tz));
}

}  // namespace godot

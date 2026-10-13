#include "gdext/pes_broadcast_camera.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

namespace godot {
namespace {
// The owner's live Dynamic Wide sliders, read off the plugin object
// ([InplayCamera+0x0C/+0x10], docs/re/pes-re-camera-live.md 2). These are the
// user's settings, not PES defaults: the plugin ctor initialises both to 1.0
// (report item 7, 0x14087e965) and the persisted CAMERASETTINGS defaults sit
// behind the mutation-obfuscated serializer (report item 5, still open), so no
// default is readable at runtime. Height matches the old fit; zoom 0.80 is the
// live-observed user value (the 0.79 fit predates the u x100 correction).
constexpr float LIVE_ZOOM = 0.80f, LIVE_HEIGHT = 0.20f;
constexpr float ZOOM_GOAL = 1.0f;          // k goal in open play (report 4.3)
constexpr double NEAR_PLANE_M = 0.9;       // InplayCamera::update, modes other than 2/3/4
constexpr double FAR_PLANE_M = 4000.0f;    // report 7 recipe
constexpr int MAX_STEPS_PER_UPDATE = 30;
}  // namespace

void PesBroadcastCamera::_bind_methods() {
  ClassDB::bind_method(D_METHOD("load", "library", "field_half_width", "field_half_length"),
                       &PesBroadcastCamera::load);
  ClassDB::bind_method(D_METHOD("set_sliders", "zoom", "height"), &PesBroadcastCamera::set_sliders);
  ClassDB::bind_method(D_METHOD("set_margin_time_active", "active"), &PesBroadcastCamera::set_margin_time_active);
  ClassDB::bind_method(D_METHOD("reset", "focus"), &PesBroadcastCamera::reset);
  ClassDB::bind_method(D_METHOD("update", "camera", "focus", "delta"), &PesBroadcastCamera::update);
}

Error PesBroadcastCamera::load(const Ref<PesLibrary>& library, double field_half_width, double field_half_length) {
  const auto* params = library.is_valid() ? library->camera_params() : nullptr;
  if (!params) {
    UtilityFunctions::push_error("PesBroadcastCamera: library lacks the camera tables");
    return ERR_UNCONFIGURED;
  }
  params_ = *params;
  halfWidth_ = float(field_half_width);
  halfLength_ = float(field_half_length);
  zoom_ = LIVE_ZOOM;
  height_ = LIVE_HEIGHT;
  smoothing_ = pes::TargetSmoothing(*params_);
  spring_ = pes::ZoomSpring(ZOOM_GOAL, 0.0f, params_->kSpringGain, params_->kSpringStepLimit);
  // PES steps the camera once per rendered frame at this reference rate
  // (report CameraTargetRE item 3), and the lift's numerator rides on it.
  if (params_->fpsRef > 0.0f) {
    stepS_ = 1.0f / params_->fpsRef;
    liftTimeScale_ = params_->liftNumerator > 0.0f ? params_->liftNumerator / params_->fpsRef : 1.0f;
  }
  return OK;
}

void PesBroadcastCamera::set_sliders(double zoom, double height) {
  zoom_ = float(zoom);
  height_ = float(height);
}

void PesBroadcastCamera::set_margin_time_active(bool active) { marginTimeActive_ = active; }

void PesBroadcastCamera::reset(const Vector3& focus) {
  target_ = focus;
  smoothing_.Reset({float(focus.x), float(focus.y), float(focus.z)});
  spring_ = pes::ZoomSpring(ZOOM_GOAL, 0.0f, params_ ? params_->kSpringGain : 0.02f,
                            params_ ? params_->kSpringStepLimit : 0.005f);
  accumulator_ = 0.0;
}

void PesBroadcastCamera::update(Camera3D* camera, const Vector3& focus, double delta) {
  if (!params_ || !camera) return;
  // The raw focus PES's smoothing consumes is the per-situation focus point
  // blended towards the ball by the lead (report CameraTargetRE item 2). GF
  // tracks no separate focus point yet, so the ball is it and the lead is
  // degenerate: the zero-horizontal-distance case returns the focus as is.
  const pes::Vec3 raw{float(focus.x), float(focus.y), float(focus.z)};
  accumulator_ += delta;
  for (int i = 0; accumulator_ >= stepS_ && i < MAX_STEPS_PER_UPDATE; ++i) {
    smoothing_.Step(raw, marginTimeActive_);
    spring_.Step(ZOOM_GOAL);
    target_ = Vector3(smoothing_.target().x, smoothing_.target().y, smoothing_.target().z);
    accumulator_ -= stepS_;
  }
  // t reads the clamped target z: the live 23.26 deg lens fits t from z = -8
  // (the mode-5 Z_min), not from the unclamped focus (report 4.6).
  const float tz = std::clamp(float(target_.z), params_->zMin, params_->zMax);
  const auto shot = pes::DynamicWideShot(*params_, zoom_, height_, tz, halfWidth_, liftTimeScale_);
  // target.y = LookY + the ball-height lift, from the raw focus y (item 1).
  const auto pose = pes::FramePose(shot, target_.x, target_.z, raw.y, spring_.k, halfLength_, halfWidth_);
  camera->set_keep_aspect_mode(Camera3D::KEEP_HEIGHT);  // PES's FOV is vertical
  camera->set_fov(pose.vfovDeg);
  camera->set_near(NEAR_PLANE_M);
  camera->set_far(FAR_PLANE_M);
  camera->look_at_from_position(Vector3(pose.cx, pose.cy, pose.cz), Vector3(pose.tx, pose.ty, pose.tz));
}

}  // namespace godot

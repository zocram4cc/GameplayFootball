#include "gdext/pes_animation.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "pes/clips.hpp"
#include "pes/gani.hpp"

namespace godot {

void PesAnimation::_bind_methods() {
  ClassDB::bind_method(D_METHOD("load", "library", "name_or_hash"), &PesAnimation::load);
  ClassDB::bind_method(D_METHOD("length_seconds"), &PesAnimation::length_seconds);
  ClassDB::bind_method(D_METHOD("apply", "skeleton", "time_seconds", "in_place"), &PesAnimation::apply);
}

Error PesAnimation::load(const Ref<PesLibrary>& library, const String& name_or_hash) {
  if (library.is_null() || !library->rig()) {
    UtilityFunctions::push_error("PesAnimation: library is not open");
    return ERR_UNCONFIGURED;
  }
  std::string err;
  auto blob = pes::FindBodyClip(library->vfs(), pes::ClipId(FromGd(name_or_hash)), &err);
  if (!blob) return ERR_FILE_NOT_FOUND;
  auto clip = pes::ParseGani(*blob, &err);
  if (!clip) {
    UtilityFunctions::push_error("PesAnimation: ", name_or_hash, ": ", ToGd(err));
    return ERR_PARSE_ERROR;
  }
  sampler_ = std::make_unique<pesrig::ClipSampler>(*clip, *library->rig());
  return OK;
}

double PesAnimation::length_seconds() const { return sampler_ ? sampler_->LengthSeconds() : 0.0; }

Error PesAnimation::apply(Skeleton3D* skeleton, double time_seconds, bool in_place) const {
  if (!sampler_ || !skeleton) return ERR_UNCONFIGURED;
  if (!skeleton->is_inside_tree()) {
    UtilityFunctions::push_error("PesAnimation.apply: skeleton is not inside the scene tree");
    return ERR_UNCONFIGURED;
  }
  const auto pose = sampler_->Sample(time_seconds / pesrig::PES_FRAME_S);
  const int n = std::min<int>(skeleton->get_bone_count(), int(pose.local.size()));
  for (int j = 1; j < n; ++j) {
    const auto& q = pose.local[j];
    skeleton->set_bone_pose_rotation(j, Quaternion(q.x, q.y, q.z, q.w));
  }
  const auto& bq = pose.bodyRotation;
  const auto& bp = pose.bodyPosition;
  skeleton->set_bone_pose_rotation(0, Quaternion(bq.x, bq.y, bq.z, bq.w));
  skeleton->set_bone_pose_position(0, in_place ? Vector3(0, bp.y, 0) : Vector3(bp.x, bp.y, bp.z));
  return OK;
}

}  // namespace godot

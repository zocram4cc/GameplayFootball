#include "gdext/pes_animation.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>

#include "pes/clips.hpp"
#include "pes/gani.hpp"
#include "pes/strcode.hpp"

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
  library_ = library;
  motionNo_ = -1;
  if (const auto* names = library->motion_names()) {
    const uint32_t id = pes::ClipId(FromGd(name_or_hash));
    for (size_t m = 0; m < names->size() && motionNo_ < 0; ++m)
      if (pes::StrCode32((*names)[m]) == id) motionNo_ = int(m);
  }
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
  ApplyHands(skeleton, time_seconds);
  return OK;
}

// Each hand blends up to four static poses from the clip's own timeline. A clip
// without one, or whose Sequenceinfo bit hands that hand over, plays the idle
// fallback cycle (category 0: free play) at the clip time (pes-re-animselect.md
// 11.3). ponytail: player-state categories (keeper ready/holding, linesman)
// need the match; the viewer is always category 0.
void PesAnimation::ApplyHands(Skeleton3D* skeleton, double time_seconds) const {
  const auto* seq = library_.is_valid() ? library_->hand_sequences() : nullptr;
  const auto* lib = library_.is_valid() ? library_->hand_poses() : nullptr;
  if (!seq || !lib) return;
  constexpr int FREE_PLAY_CATEGORY = 0;
  static constexpr const char* DEFAULT_HAND_POSE = "normal3";  // the engine's track default (FUN_141eb6840)
  const int fallbackPose = int(std::find(seq->poses.begin(), seq->poses.end(), DEFAULT_HAND_POSE) - seq->poses.begin());
  auto bits = motionNo_ >= 0 ? seq->useFallback.find(motionNo_) : seq->useFallback.end();
  std::vector<pesrig::Quat> local(library_->rig()->bones.size());
  for (const bool right : {false, true}) {
    const auto& table = right ? seq->right : seq->left;
    auto it = motionNo_ >= 0 ? table.find(motionNo_) : table.end();
    const bool handedOver = bits != seq->useFallback.end() && (bits->second >> (right ? 1 : 0)) & 1;
    auto weights = it != table.end() && !handedOver ? pes::EvaluateHand(it->second, float(time_seconds))
                                                    : pes::EvaluateFallback(*seq, FREE_PLAY_CATEGORY, float(time_seconds), right);
    if (weights.empty()) weights = {{fallbackPose, 1.0f}};
    pesrig::ApplyHandPose(*library_->rig(), *lib, weights, right, local);
  }
  const auto& bones = library_->rig()->bones;
  for (size_t j = 0; j < bones.size() && int(j) < skeleton->get_bone_count(); ++j)
    if (bones[j].pesBone.rfind("skh_", 0) == 0) {
      const auto& q = local[j];
      skeleton->set_bone_pose_rotation(int(j), Quaternion(q.x, q.y, q.z, q.w));
    }
}

}  // namespace godot

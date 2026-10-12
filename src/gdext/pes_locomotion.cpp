#include "gdext/pes_locomotion.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

#include "pes/locomotion.hpp"

namespace godot {
namespace {
// Standing in open play: the only still clip seen live between walks
// (report 7, P9: walkslant_1_1 -> stand_d -> walkslant_1_1). INF: PES's own
// rule for the still clip is not decoded.
constexpr const char* STAND_CLIP = "stand_d";
constexpr int STILL_CLASS = 0;            // speed class 0 = still (report 4.2)
constexpr double KMH_PER_MPS = 3.6;

Quaternion Q(const pesrig::Quat& q) { return Quaternion(q.x, q.y, q.z, q.w); }
pesrig::Quat P(const Quaternion& q) { return {q.x, q.y, q.z, q.w}; }

pesrig::ClipPose Blend(const pesrig::ClipPose& a, const pesrig::ClipPose& b, float t) {
  pesrig::ClipPose out = a;
  for (size_t j = 0; j < out.local.size() && j < b.local.size(); ++j)
    out.local[j] = P(Q(a.local[j]).slerp(Q(b.local[j]), t));
  out.bodyRotation = P(Q(a.bodyRotation).slerp(Q(b.bodyRotation), t));
  out.bodyPosition.x += (b.bodyPosition.x - a.bodyPosition.x) * t;
  out.bodyPosition.y += (b.bodyPosition.y - a.bodyPosition.y) * t;
  out.bodyPosition.z += (b.bodyPosition.z - a.bodyPosition.z) * t;
  return out;
}
}  // namespace

void PesLocomotion::_bind_methods() {
  ClassDB::bind_method(D_METHOD("load", "library"), &PesLocomotion::load);
  ClassDB::bind_method(D_METHOD("is_still", "speed_mps"), &PesLocomotion::is_still);
  ClassDB::bind_method(D_METHOD("rate_for_speed", "speed_mps"), &PesLocomotion::rate_for_speed);
  ClassDB::bind_method(D_METHOD("phase_rate", "speed_mps"), &PesLocomotion::phase_rate);
  ClassDB::bind_method(D_METHOD("apply", "skeleton", "speed_mps", "phase"), &PesLocomotion::apply);
}

Error PesLocomotion::load(const Ref<PesLibrary>& library) {
  const auto* names = library.is_valid() ? library->motion_names() : nullptr;
  const auto* classes = library.is_valid() ? library->speed_classes() : nullptr;
  if (!names || !classes) {
    UtilityFunctions::push_error("PesLocomotion: library lacks the exe tables");
    return ERR_UNCONFIGURED;
  }
  std::string err;
  auto table = pes::LoadBlendTable(library->vfs(), &err);
  if (!table) {
    UtilityFunctions::push_error("PesLocomotion: ", ToGd(err));
    return ERR_FILE_NOT_FOUND;
  }
  members_.clear();
  params_.clear();
  speeds_.clear();
  for (const auto& m : pes::BlendGroup(*table, pes::LOCOMOTION_BLEND_GROUP)) {
    // ponytail: group 80 has no mirrored member; other groups (sidestep) would need mirroring.
    if (m.mirror || m.motionNo >= int(names->size())) return ERR_INVALID_DATA;
    Ref<PesAnimation> clip;
    clip.instantiate();
    if (clip->load(library, ToGd((*names)[m.motionNo])) != OK) return ERR_FILE_NOT_FOUND;
    const double len = clip->length_seconds();
    const auto a = clip->Pose(0.0).bodyPosition, b = clip->Pose(len).bodyPosition;
    const float speed = len > 0.0 ? float(std::hypot(b.x - a.x, b.z - a.z) / len) : 0.0f;
    members_.push_back({clip, m.param, speed});
    params_.push_back(m.param);
    speeds_.push_back(speed);
  }
  if (members_.empty()) return ERR_INVALID_DATA;
  stand_.instantiate();
  if (stand_->load(library, STAND_CLIP) != OK) return ERR_FILE_NOT_FOUND;
  stillBound_mps_ = float((*classes)[STILL_CLASS] / KMH_PER_MPS);
  return OK;
}

bool PesLocomotion::is_still(double v) const { return v < stillBound_mps_; }

double PesLocomotion::rate_for_speed(double v) const { return pes::RateForSpeed(params_, speeds_, float(v)); }

double PesLocomotion::phase_rate(double v) const {
  if (members_.empty()) return 0.0;
  if (is_still(v)) return stand_->length_seconds() > 0.0 ? 1.0 / stand_->length_seconds() : 0.0;
  const auto b = pes::Bracket(params_, float(rate_for_speed(v)));
  const auto &lo = members_[b.lo], &hi = members_[b.hi];
  const double cycle = lo.clip->length_seconds() + (hi.clip->length_seconds() - lo.clip->length_seconds()) * b.t;
  const double root = lo.rootSpeed_mps + (hi.rootSpeed_mps - lo.rootSpeed_mps) * b.t;
  return cycle > 0.0 && root > 0.0 ? (v / root) / cycle : 0.0;
}

Error PesLocomotion::apply(Skeleton3D* skeleton, double v, double phase) const {
  if (members_.empty() || !skeleton) return ERR_UNCONFIGURED;
  if (!skeleton->is_inside_tree()) return ERR_UNCONFIGURED;
  phase -= std::floor(phase);
  if (is_still(v)) {
    const double t = phase * stand_->length_seconds();
    stand_->Write(skeleton, stand_->Pose(t), true, t);
    return OK;
  }
  const auto b = pes::Bracket(params_, float(rate_for_speed(v)));
  const auto &lo = members_[b.lo], &hi = members_[b.hi];
  const double tl = phase * lo.clip->length_seconds(), th = phase * hi.clip->length_seconds();
  const auto& lead = b.t < 0.5f ? lo : hi;  // hands follow the heavier member
  lead.clip->Write(skeleton, Blend(lo.clip->Pose(tl), hi.clip->Pose(th), b.t), true, b.t < 0.5f ? tl : th);
  return OK;
}

}  // namespace godot

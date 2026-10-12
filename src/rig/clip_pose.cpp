// Port of GameplayFootball tools/pes21_import/gani_to_anim.py Sampler,
// build_samplers and sample_root (without the Fox->GF basis change: output
// stays in Fox space).
#include "rig/clip_pose.hpp"

#include <cmath>

#include "pes/ask.hpp"
#include "rig/rig_names.hpp"

namespace pesrig {
namespace {

struct D3 { double x, y, z; };
struct DQ { double x, y, z, w; };

DQ ToD(Quat q) { return {q.x, q.y, q.z, q.w}; }
Quat ToF(DQ q) { return {float(q.x), float(q.y), float(q.z), float(q.w)}; }

DQ Mul(DQ a, DQ b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
DQ Norm(DQ q) {
  const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return n > 0 ? DQ{q.x / n, q.y / n, q.z / n, q.w / n} : DQ{0, 0, 0, 1};
}
D3 Rot(DQ q, D3 v) {
  const DQ r = Mul(Mul(q, {v.x, v.y, v.z, 0}), {-q.x, -q.y, -q.z, q.w});
  return {r.x, r.y, r.z};
}

// Keys bracketing fractional frame t, and the blend between them.
void Bracket(const std::vector<uint32_t>& frames, double t, size_t* a, size_t* b, double* f) {
  *f = 0;
  if (t <= frames.front()) { *a = *b = 0; return; }
  if (t >= frames.back()) { *a = *b = frames.size() - 1; return; }
  size_t i = 1;
  while (frames[i] < t) ++i;
  *a = i - 1;
  *b = i;
  const double span = double(frames[i]) - frames[i - 1];
  *f = span > 0 ? (t - frames[i - 1]) / span : 0;
}

DQ SampleQuat(const pes::GaniSegment* s, double t) {
  if (!s || s->quats.empty()) return {0, 0, 0, 1};
  size_t a, b;
  double f;
  Bracket(s->frames, t, &a, &b, &f);
  DQ qa = ToD(s->quats[a]), qb = ToD(s->quats[b]);
  if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0) qb = {-qb.x, -qb.y, -qb.z, -qb.w};
  return Norm({qa.x + (qb.x - qa.x) * f, qa.y + (qb.y - qa.y) * f, qa.z + (qb.z - qa.z) * f, qa.w + (qb.w - qa.w) * f});
}

D3 SampleVec(const pes::GaniSegment* s, double t) {
  if (!s || s->vecs.empty()) return {0, 0, 0};
  size_t a, b;
  double f;
  Bracket(s->frames, t, &a, &b, &f);
  const auto &va = s->vecs[a], &vb = s->vecs[b];
  return {va.x + (vb.x - va.x) * f, va.y + (vb.y - va.y) * f, va.z + (vb.z - va.z) * f};
}

const pes::GaniSegment* Segment(const pes::Gani& g, int unit, int seg) {
  if (unit >= int(g.units.size()) || seg >= int(g.units[unit].segments.size())) return nullptr;
  return &g.units[unit].segments[seg];
}

}  // namespace

ClipSampler::ClipSampler(const pes::Gani& clip, const Rig& rig)
    : jointTracks_(rig.bones.size()), bodyBind_(rig.bones.empty() ? Vec3{} : rig.bones[0].animGlobalPos),
      frameCount_(clip.frameCount), clip_(clip) {
  for (const auto& slot : PES_TRACK_MAP)
    for (size_t j = 0; j < rig.bones.size(); ++j)
      if (rig.bones[j].pesBone == slot.bone) {
        const auto* seg = Segment(clip_, slot.unit, slot.segment);
        if (seg && !seg->quats.empty()) jointTracks_[j] = seg;
      }
  auto withQuats = [&](int unit, int seg) {
    const auto* s = Segment(clip_, unit, seg);
    return s && !s->quats.empty() ? s : nullptr;
  };
  auto withVecs = [&](int unit, int seg) {
    const auto* s = Segment(clip_, unit, seg);
    return s && !s->vecs.empty() ? s : nullptr;
  };
  rootQ_ = withQuats(PES_ROOT_UNIT, 0);
  rootP_ = withVecs(PES_ROOT_UNIT, 1);
  moverQ_ = withQuats(PES_MOTION_UNIT, 0);
  moverP_ = withVecs(PES_MOTION_UNIT, 1);
}

ClipPose ClipSampler::Sample(double t) const {
  ClipPose pose;
  pose.local.resize(jointTracks_.size());
  for (size_t j = 0; j < jointTracks_.size(); ++j) pose.local[j] = ToF(SampleQuat(jointTracks_[j], t));

  const DQ rq = SampleQuat(rootQ_, t), mq = SampleQuat(moverQ_, t);
  const D3 rpRaw = SampleVec(rootP_, t), mpRaw = SampleVec(moverP_, t);
  const D3 rp{rpRaw.x * PES_POS_TO_M_GAMEPLAY, rpRaw.y * PES_POS_TO_M_GAMEPLAY, rpRaw.z * PES_POS_TO_M_GAMEPLAY};
  const D3 mover{bodyBind_.x + mpRaw.x * PES_POS_TO_M_GAMEPLAY, bodyBind_.y + mpRaw.y * PES_POS_TO_M_GAMEPLAY,
                 bodyBind_.z + mpRaw.z * PES_POS_TO_M_GAMEPLAY};
  const D3 offset = Rot(rq, mover);
  pose.bodyRotation = ToF(Mul(rq, mq));
  pose.bodyPosition = {float(rp.x + offset.x), float(rp.y + offset.y), float(rp.z + offset.z)};
  return pose;
}


namespace {
constexpr const char* HAND_SKELETON = "common/anime/FoxAnim/Hand/CharacterAssets/hand_anim_skel.ask";
constexpr size_t FIRST_FINGER_UNIT = 2;  // unit 0 RIG_ROOT, unit 1 skh_hand_l (the wrist: from the body clip)
}  // namespace

std::optional<HandPoseLibrary> LoadHandPoses(const pes::PesVfs& vfs, const std::vector<std::string>& poseOrder,
                                             std::string* error) {
  auto skelBytes = vfs.Read(HAND_SKELETON, error);
  auto skel = skelBytes ? pes::ParseAsk(*skelBytes, error) : std::nullopt;
  if (!skel) return std::nullopt;
  HandPoseLibrary lib;
  // Hand skeleton bone b = unit b + 1; bone 0 is the hand itself.
  for (size_t b = FIRST_FINGER_UNIT - 1; b < skel->size(); ++b) lib.fingers.push_back((*skel)[b].name);
  for (const auto& stem : poseOrder) {
    const std::string path = std::string(pes::kHandPoseDir) + stem + ".gani";
    auto blob = vfs.Read(path, error);
    auto g = blob ? pes::ParseGani(*blob, error) : std::nullopt;
    if (!g) return std::nullopt;
    if (g->units.size() < FIRST_FINGER_UNIT + lib.fingers.size()) {
      *error = path + ": fewer units than hand bones";
      return std::nullopt;
    }
    std::vector<Quat> pose;
    for (size_t f = 0; f < lib.fingers.size(); ++f) {
      const auto& unit = g->units[FIRST_FINGER_UNIT + f];
      pose.push_back(unit.segments.empty() ? Quat{} : ToF(SampleQuat(&unit.segments[0], 0.0)));
    }
    lib.poses.push_back(std::move(pose));
  }
  return lib;
}

void ApplyHandPose(const Rig& rig, const HandPoseLibrary& lib, const std::vector<pes::HandWeight>& weights, bool right,
                   std::vector<Quat>& local) {
  for (size_t f = 0; f < lib.fingers.size(); ++f) {
    std::string bone = lib.fingers[f];
    if (right) bone.back() = 'r';
    int joint = -1;
    for (size_t j = 0; j < rig.bones.size(); ++j)
      if (rig.bones[j].pesBone == bone) joint = int(j);
    if (joint < 0) continue;
    // Weighted nlerp, each pose sign-aligned to the first.
    DQ sum{0, 0, 0, 0}, first{};
    bool have = false;
    for (const auto& w : weights) {
      if (w.clip < 0 || size_t(w.clip) >= lib.poses.size() || w.weight <= 0) continue;
      const Quat& q = lib.poses[size_t(w.clip)][f];
      DQ d{q.x, q.y, q.z, q.w};
      if (!have) first = d, have = true;
      const double s = (d.x * first.x + d.y * first.y + d.z * first.z + d.w * first.w) < 0 ? -w.weight : w.weight;
      sum = {sum.x + s * d.x, sum.y + s * d.y, sum.z + s * d.z, sum.w + s * d.w};
    }
    if (!have) continue;
    const double n = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z + sum.w * sum.w);
    Quat q = ToF({sum.x / n, sum.y / n, sum.z / n, sum.w / n});
    // Mirror across x = 0: the right hand is the left skeleton reflected in x.
    if (right) q = {q.x, -q.y, -q.z, q.w};
    local[size_t(joint)] = q;
  }
}

}  // namespace pesrig

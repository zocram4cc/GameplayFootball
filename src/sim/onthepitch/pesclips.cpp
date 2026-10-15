#include "onthepitch/pesclips.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

#include "pes/clips.hpp"
#include "pes/strcode.hpp"
#include "rig/clip_pose.hpp"
#include "rig/rig_names.hpp"

namespace pes {
namespace {

// The engine's animation frame.
constexpr double kGfFrame_s = 0.01;
// A clip needs at least two frames for the engine's start/end bookkeeping.
constexpr int kMinFrames = 2;

struct FamilyPrefix {
  PesClipFamily family;
  std::string_view prefix;
};
constexpr FamilyPrefix kFamilies[] = {
    {PesClipFamily::kDodge, "dodge_"},      // ball dodges
    {PesClipFamily::kDodge, "avoidjump_"},  // human jump dodges
    {PesClipFamily::kDodge, "avoidslide_"}, // human slide dodges
    {PesClipFamily::kDodge, "goalnet_"},    // goal-net dodges
    {PesClipFamily::kDodge, "rapidstop_"},  // human stop/decelerate dodges
    {PesClipFamily::kDodge, "rapid_3_2_dodge_"},
    // The standing-tackle family: every prefix the Tackle action's sub-kind
    // group tables' rows resolve to (docs/re/pes-re-contact-port.md stage C).
    // tacklefoot_ is the tackle proper; tacklehold_* hold the carrier,
    // tacklepulled_* is pulled over, tackleshoulder_* shoulders past him, and
    // block090_/js_idle_*_passcut are the two rows the tables also reach.
    {PesClipFamily::kTackle, "tacklefoot_"},
    {PesClipFamily::kTackle, "tacklehold_"},
    {PesClipFamily::kTackle, "tacklepulled_"},
    {PesClipFamily::kTackle, "tackleshoulder_"},
    {PesClipFamily::kTackle, "block090_"},
    {PesClipFamily::kTackle, "js_idle_0_0_000_y00_tacklefoot_passcut"},
};

// Fox (Y up, +Z forward) -> engine (Z up, faces -Y), verbatim on both the
// vectors and the rotation axes.
Vec3 MapVec(const Vec3& v) { return {v.x, -v.z, v.y}; }
Quat MapQuat(const Quat& q) {
  const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  const float s = n > 0.0f ? 1.0f / n : 1.0f;
  return {q.x * s, -q.z * s, q.y * s, q.w * s};
}

}  // namespace

std::optional<PesClipSet> LoadPesClips(const PeImage& exe, const PesVfs& vfs, std::string* error) {
  auto names = MotionNames(exe, vfs, error);
  if (!names) return std::nullopt;
  PesClipSet set;
  for (size_t m = 0; m < names->size(); ++m) {
    const std::string& name = (*names)[m];
    for (const auto& f : kFamilies) {
      if (!name.starts_with(f.prefix)) continue;
      auto blob = FindBodyClip(vfs, StrCode32(name), error);
      if (!blob) return std::nullopt;
      auto gani = ParseGani(*blob, error);
      if (!gani) return std::nullopt;
      set.clips.push_back({name, int(m), f.family, std::move(*gani)});
    }
  }
  if (set.clips.empty()) {
    *error = "pes clips: the install's motion table names no clip of any imported family";
    return std::nullopt;
  }
  return set;
}

std::string PesClipAnimText(const PesClip& clip, const pesrig::Rig& rig) {
  const pesrig::ClipSampler sampler(clip.gani, rig);
  const int frames = std::max(kMinFrames, int(std::lround(sampler.LengthSeconds() / kGfFrame_s)));
  const int joints = std::min<int>(pesrig::BODY_NODE_COUNT, int(rig.bones.size()));
  const Vec3 bodyBind = MapVec(rig.bones[0].animGlobalPos);

  std::string player = "player";
  std::vector<std::string> lines(joints);
  for (int j = 0; j < joints; ++j) lines[j] = rig.bones[j].gfName;
  float originX = 0.0f, originY = 0.0f;
  char buf[160];
  for (int f = 0; f <= frames; ++f) {
    const double t = std::min(f * kGfFrame_s / pesrig::PES_FRAME_S, double(clip.gani.frameCount));
    const pesrig::ClipPose pose = sampler.Sample(t);
    const Vec3 p = MapVec(pose.bodyPosition);
    const Vec3 root{p.x - bodyBind.x, p.y - bodyBind.y, p.z - bodyBind.z};
    if (f == 0) originX = root.x, originY = root.y;
    std::snprintf(buf, sizeof buf, ",%d,%f,%f,%f", f, root.x - originX, root.y - originY, root.z);
    player += buf;
    for (int j = 0; j < joints; ++j) {
      const Quat q = MapQuat(j == 0 ? pose.bodyRotation : pose.local[j]);
      std::snprintf(buf, sizeof buf, ",%d,%f,%f,%f,%f", f, q.x, q.y, q.z, q.w);
      lines[j] += buf;
    }
  }
  std::string text = player + "\n";
  for (const auto& l : lines) text += l + "\n";
  text += "<specialvar1>\n\t" + std::to_string(int(clip.family)) + "\n</specialvar1>\n";
  text += "<specialvar2>\n\t" + std::to_string(clip.motionNo) + "\n</specialvar2>\n";
  text += "<type>\n\tspecial\n</type>\n<tpose_rig>\n\t1\n</tpose_rig>\n";
  return text;
}

}  // namespace pes

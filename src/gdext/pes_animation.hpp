#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <memory>

#include "gdext/pes_library.hpp"
#include "rig/clip_pose.hpp"

namespace godot {

// One PES body clip, sampled on the anim rig and applied to a character skeleton.
class PesAnimation : public RefCounted {
  GDCLASS(PesAnimation, RefCounted)

 public:
  Error load(const Ref<PesLibrary>& library, const String& name_or_hash);
  double length_seconds() const;
  // Poses the skeleton at `time_seconds` (clamped to the clip): body from the
  // clip, fingers from its hand timelines (pes-re-animselect.md 11). With in_place
  // the body joint's horizontal travel is removed. The skeleton must be inside
  // the tree: Godot 4.7 only invalidates cached global poses there, so posing
  // an out-of-tree skeleton would silently show the old pose (ERR_UNCONFIGURED).
  Error apply(Skeleton3D* skeleton, double time_seconds, bool in_place) const;

  // C++ side, for blends: the body pose at a time, and writing a (possibly
  // blended) body pose plus this clip's hands at `hand_time` to a skeleton.
  pesrig::ClipPose Pose(double time_seconds) const;
  void Write(Skeleton3D* skeleton, const pesrig::ClipPose& pose, bool in_place, double hand_time) const;
  int motion_no() const { return motionNo_; }

 protected:
  static void _bind_methods();

 private:
  void ApplyHands(Skeleton3D* skeleton, double time_seconds) const;
  std::unique_ptr<pesrig::ClipSampler> sampler_;
  Ref<PesLibrary> library_;
  int motionNo_ = -1;  // index in the game's motion table; -1 = unknown
};

}  // namespace godot

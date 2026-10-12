#pragma once

#include <vector>

#include "pes/gani.hpp"
#include "rig/rig.hpp"

namespace pesrig {

// One PES animation frame (GF gani_to_anim.py PES_FRAME_MS = 1000 / 59.94).
inline constexpr double PES_FRAME_S = 1.0 / 59.94;
// Root/mover position tracks, raw units -> metres for match animation. Measured
// by GF calibrate_pos_scale.py (stance feet do not slide, ankles reach the
// grass, a lying player's pelvis ~0.14 m up); see GF retarget.py.
inline constexpr double PES_POS_TO_M_GAMEPLAY = 1.0 / 20480.0;

struct ClipPose {
  Quat bodyRotation;         // world rotation of the body joint (RIG_ROOT o mover), Fox space
  Vec3 bodyPosition;         // world position of the body joint, metres
  std::vector<Quat> local;   // per rig joint (joint-ID order); identity where no track drives it
};

// Samples a body clip on the PES animation rig (GF gani_to_anim.py
// build_samplers / sample_root): nlerp for rotations, lerp for positions.
class ClipSampler {
 public:
  ClipSampler(const pes::Gani& clip, const Rig& rig);
  // Segment pointers point into the owned clip: a copy would alias the source.
  ClipSampler(const ClipSampler&) = delete;
  ClipSampler& operator=(const ClipSampler&) = delete;
  ClipSampler(ClipSampler&&) = default;  // vector storage moves with its heap buffer
  ClipSampler& operator=(ClipSampler&&) = default;
  ClipPose Sample(double pesFrame) const;
  double LengthSeconds() const { return frameCount_ * PES_FRAME_S; }

 private:
  std::vector<const pes::GaniSegment*> jointTracks_;  // per joint; null = rest
  const pes::GaniSegment *rootQ_ = nullptr, *rootP_ = nullptr, *moverQ_ = nullptr, *moverP_ = nullptr;
  Vec3 bodyBind_;
  uint32_t frameCount_ = 0;
  pes::Gani clip_;  // owned copy; the pointers above point into it
};

}  // namespace pesrig

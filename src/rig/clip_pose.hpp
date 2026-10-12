#pragma once

#include <vector>

#include "pes/gani.hpp"
#include "rig/rig.hpp"

namespace pesrig {

// One match-gani frame: 1/30 s. FUN_1406af720 divides frames by the constant
// 30.0 at 0x142591140, and run cadences only make sense at 30 fps
// (docs/re/pes-re-animselect.md 1). GF's 1/59.94 played clips at 2x.
inline constexpr double PES_FRAME_S = 1.0 / 30.0;
// Root/mover position tracks: 16384 raw units per metre. The developer export
// anim_infos.json (cm) against the raw root gives 163.8 raw/cm on 5 clips, and
// run_3_3 then moves at 3.95 m/s against 3.88 m/s measured live (same report).
// GF's 1/20480 was a visual fit (feet not sliding) on GF's skeleton; a
// planted-ankle slide measurement here could not separate the two (ankles
// roll through contact), so PES's own data decides.
inline constexpr double PES_POS_TO_M_GAMEPLAY = 1.0 / 16384.0;

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

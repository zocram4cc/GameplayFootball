#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <vector>

#include "gdext/pes_animation.hpp"
#include "gdext/pes_library.hpp"

namespace godot {

// PES's steady locomotion (docs/re/pes-re-animselect.md 2.6, 4.2, 5.2): the
// speed blend walk_1_1 .. run_5_5 (Parametricblendtable group 80), every member
// at one normalised phase, and a standing clip below the "still" speed class.
// Shared by all players; each player keeps its own phase.
class PesLocomotion : public RefCounted {
  GDCLASS(PesLocomotion, RefCounted)

 public:
  Error load(const Ref<PesLibrary>& library);
  bool is_still(double speed_mps) const;
  double rate_for_speed(double speed_mps) const;
  // Phase cycles per second at a ground speed: the blended cycle played so its
  // root speed meets the ground speed (report 4.3.3: ground speed = blended
  // root speed x playbackSpeed).
  double phase_rate(double speed_mps) const;
  // Poses the skeleton in place at `phase` in [0, 1).
  Error apply(Skeleton3D* skeleton, double speed_mps, double phase) const;

 protected:
  static void _bind_methods();

 private:
  struct Member {
    Ref<PesAnimation> clip;
    float param;
    float rootSpeed_mps;  // natural: horizontal root travel / clip length
  };
  std::vector<Member> members_;  // sorted by param
  std::vector<float> params_, speeds_;
  Ref<PesAnimation> stand_;
  float stillBound_mps_ = 0.0f;
};

}  // namespace godot

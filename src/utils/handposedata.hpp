// A library of named finger poses, and how a hand moves between them.
//
// PES has no finger channels in its body animation - every one of the 4,389 body
// ganis is the twenty bones of body_skel.frig and nothing else. It keeps a separate
// hand rig (pes_human_hand_141203.frig) and 162 one-frame ganis, and picks between
// them by name from code. handposes.txt is that library converted
// (tools/pes21_import/hand_poses.py): one pose per name, one local rotation per
// finger joint of both hands.
//
//     # gfhandposes 1
//     pose relax
//     left_thumb_mata <qx> <qy> <qz> <qw>
//     ...

#ifndef _HPP_UTILS_HANDPOSEDATA
#define _HPP_UTILS_HANDPOSEDATA

#include <istream>
#include <map>
#include <string>
#include <vector>

#include "base/math/quaternion.hpp"

namespace blunted {

// joint name -> its local rotation, relative to the bind pose
using HandPose = std::map<std::string, Quaternion>;

class HandPoseData {
public:
  bool Load(std::istream& in);

  bool Has(const std::string& pose) const { return poses.count(pose) > 0; }
  const HandPose* Find(const std::string& pose) const;
  size_t PoseCount() const { return poses.size(); }

  // Every joint any pose mentions, in the order first seen: what a rig has to
  // find in the skeleton before it can drive anything.
  const std::vector<std::string>& Joints() const { return joints; }

private:
  std::map<std::string, HandPose> poses;
  std::vector<std::string> joints;
};

// Moves `current` a `factor` of the way toward `target`, in place. A joint the
// target does not mention goes back to the bind pose rather than keeping the last
// pose's angle, so a hand cannot be left half in a pose it has stopped wearing.
void BlendHandPose(HandPose& current, const HandPose& target, float factor);

}  // namespace blunted

#endif

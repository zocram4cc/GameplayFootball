#include "onthepitch/player/humanoid/handrig.hpp"

#include <fstream>

namespace blunted {

const char* HandPoseName(e_HandPose pose) {
  // PES's own pose names, from common/anime/FoxAnim/Hand/Animations. Chosen by
  // what they are (measured index-finger curl at the tip, against the 0.1541 m
  // bind reach): open_full_ball 0.1556, kp_hold 0.1546, taore 0.1541 with the
  // fingers braced out of plane, normal 0.1509, relax 0.1487, move_nigiri 0.1459.
  switch (pose) {
    case e_HandPose::Running:
      return "relax";
    case e_HandPose::Sprinting:
      return "move_nigiri";
    case e_HandPose::Celebrating:
      return "clap";
    case e_HandPose::Falling:
      return "taore";
    case e_HandPose::KeeperReaching:
      return "open_full_ball";
    case e_HandPose::KeeperHolding:
      return "kp_hold";
    case e_HandPose::Neutral:
      break;
  }
  return "normal";
}

bool HandRig::Load(const std::string& dir) {
  active = false;
  nodes.clear();
  current.clear();
  std::ifstream file(dir + "/handposes.txt");
  if (!file.good()) return false;
  return data.Load(file);
}

void HandRig::Bind(
    const std::map<const std::string, boost::intrusive_ptr<Node>>& nodeMap) {
  nodes.clear();
  active = false;
  if (data.PoseCount() == 0) return;
  for (const std::string& joint : data.Joints()) {
    std::map<const std::string, boost::intrusive_ptr<Node>>::const_iterator found =
        nodeMap.find(joint);
    if (found != nodeMap.end()) nodes[joint] = found->second;
  }
  // A skeleton without the finger joints - the legacy utility rig - is not an
  // error; it simply has no fingers to pose.
  active = !nodes.empty();
}

void HandRig::Apply(e_HandPose pose) {
  if (!active) return;
  const HandPose* target = data.Find(HandPoseName(pose));
  static const HandPose bind;
  BlendHandPose(current, target ? *target : bind, kHandPoseBlend);
  for (const std::pair<const std::string, Quaternion>& joint : current) {
    std::map<std::string, boost::intrusive_ptr<Node>>::const_iterator found =
        nodes.find(joint.first);
    if (found != nodes.end()) found->second->SetRotation(joint.second, false);
  }
}

}  // namespace blunted

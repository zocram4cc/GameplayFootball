// HandRig: drives a player's fingers from PES's own pose library.
//
// PES models a hand flat with the fingers spread and poses it at runtime. It does
// not do that from the body animation - measured: all 4,389 body ganis carry
// exactly the twenty bones of body_skel.frig, and body.skl has no skh_* bone in it.
// It does it from a second rig (pes_human_hand_141203.frig, nineteen bones of ONE
// hand) and a library of 162 one-frame ganis under common/anime/FoxAnim/Hand, which
// the game picks between by name from code - all 162 names sit in a string pool in
// PES2021.exe and no shipped table binds them to animations.
//
// So the engine picks by name too. tools/pes21_import/hand_poses.py converts the
// library to media/objects/players/handposes.txt (mirroring PES's one authored hand
// onto both), ChooseHandPose decides which one from what a body is doing, and the
// rig blends the finger nodes toward it each frame.
//
// The rig WINS over a clip's own finger channels: every converted pose names all 38
// finger joints, and Apply() sets each of them after Animation::Apply has run. No
// shipped clip authors a finger channel - measured, 4,389 of 4,389 body clips carry
// only the twenty body bones - so today there is nothing to lose. The first clip
// that does author one will fight the blend; when that clip exists, Apply should
// skip the joints it keyed, and not before, because dead flexibility is a cost too.

#ifndef _HPP_ONTHEPITCH_PLAYER_HUMANOID_HANDRIG
#define _HPP_ONTHEPITCH_PLAYER_HUMANOID_HANDRIG

#include <map>
#include <string>
#include <vector>

#include "base/math/quaternion.hpp"
#include "gamedefines.hpp"
#include "scene/scene3d/node.hpp"
#include "utils/handposedata.hpp"

namespace blunted {

enum class e_HandPose {
  Neutral,         // standing, walking
  Running,
  Sprinting,
  Celebrating,
  Falling,         // tripped, sliding
  KeeperReaching,  // hands opening onto the ball
  KeeperHolding,   // hands closed on it
};

// The PES pose each state wears. Names are Konami's own, from
// common/anime/FoxAnim/Hand/Animations.
const char* HandPoseName(e_HandPose pose);

// Which hand a body is wearing. Its own function so the rule is testable rather
// than buried in HumanoidBase::Process, and it takes only what the engine already
// knows: the current clip's function and how fast the player is going.
inline e_HandPose ChooseHandPose(e_FunctionType functionType, float speed) {
  // Going down wins: a player tripped while dribbling still puts his hands out.
  if (functionType == e_FunctionType_Trip || functionType == e_FunctionType_Sliding)
    return e_HandPose::Falling;
  if (functionType == e_FunctionType_Catch) return e_HandPose::KeeperHolding;
  if (functionType == e_FunctionType_Deflect) return e_HandPose::KeeperReaching;
  if (functionType == e_FunctionType_Special) return e_HandPose::Celebrating;
  // Sprinting, not merely running: the same 7 m/s the face rig calls a sprint.
  if (speed > 7.0f) return e_HandPose::Sprinting;
  if (speed > 2.0f) return e_HandPose::Running;
  return e_HandPose::Neutral;
}

// How far a hand travels toward its pose in one put. PES cross-fades its hand
// poses; a hard switch reads as a twitch at the wrist, and this is slow enough to
// look like a hand closing and fast enough to be closed by the time a sprint is.
const float kHandPoseBlend = 0.2f;

class HandRig {
public:
  // Loads <dir>/handposes.txt. False (rig disabled) when it is absent or unusable.
  bool Load(const std::string& dir);

  // Finds the finger joints in the player's own skeleton. A skeleton without them
  // - the legacy utility rig - disables the rig rather than failing.
  void Bind(const std::map<const std::string, boost::intrusive_ptr<Node>>& nodeMap);

  // One step: blend toward `pose` and write the finger nodes' local rotations.
  void Apply(e_HandPose pose);

  bool IsActive() const { return active; }
  int BoundJointCount() const { return (int)nodes.size(); }
  int PoseCount() const { return (int)data.PoseCount(); }

private:
  HandPoseData data;
  std::map<std::string, boost::intrusive_ptr<Node>> nodes;
  HandPose current;
  bool active = false;
};

}  // namespace blunted

#endif

// The order joint IDs are counted in.
//
// A skin weight names its joint by number, so the numbering is part of every
// model's file format: change it and ninety-odd already-converted bodies drive the
// wrong bone. It used to be the depth-first order of player.object, which was safe
// only while the skeleton never changed. PES's hand rig hangs nineteen finger bones
// off each wrist, and in a depth-first walk those land between left_hand and
// right_clavicle - so the order is stated here instead: the twenty body joints
// first, in the order they have always had, then everything else in DFS order.
//
// tools/pes21_import/retarget.py's GF_JOINT_ORDER is the same list, and the
// generated player.object is the same tree.

#ifndef _HPP_ONTHEPITCH_PLAYER_HUMANOID_JOINTORDER
#define _HPP_ONTHEPITCH_PLAYER_HUMANOID_JOINTORDER

#include <string>
#include <vector>

namespace JointOrder {

// The twenty body joints, joint 0 first. A skeleton missing some of them (the
// legacy utility skeleton has no clavicles) keeps only what it has.
const std::vector<std::string>& BodyJoints();

// Indices into `dfsNames` in joint-ID order: the body joints it carries, in
// BodyJoints() order, then every other node in the order given.
std::vector<int> Permutation(const std::vector<std::string>& dfsNames);

}  // namespace JointOrder

#endif

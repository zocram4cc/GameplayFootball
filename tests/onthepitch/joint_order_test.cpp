// Which joint a weight's number means.
//
// Joint IDs used to be the DFS order of player.object, which was fine while the
// skeleton never changed. Hanging PES's hand rig off the wrists changes it: in a
// depth-first walk the left hand's nineteen finger nodes come between left_hand
// and right_clavicle, so right_clavicle would slide from 16 to 35 and every
// already-converted body would drive its right arm from a finger. The order is
// therefore stated rather than walked - the twenty body joints first, in the order
// they have always had, then everything else in DFS order.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "onthepitch/player/humanoid/jointorder.hpp"

namespace {

const std::vector<std::string> kLegacyDfs = {
    "body", "hip", "left_thigh", "left_knee", "left_ankle", "right_thigh",
    "right_knee", "right_ankle", "middle", "chest", "neck", "head",
    "left_clavicle", "left_shoulder", "left_elbow", "left_hand",
    "right_clavicle", "right_shoulder", "right_elbow", "right_hand"};

std::vector<std::string> Reorder(const std::vector<std::string>& dfsNames) {
  const std::vector<int> permutation = JointOrder::Permutation(dfsNames);
  std::vector<std::string> out;
  for (int index : permutation) out.push_back(dfsNames[index]);
  return out;
}

// The rig as player.object now walks it: the fingers interleave.
std::vector<std::string> RiggedDfs() {
  const char* fingers[] = {"thumb_mata", "thumb_mcp",  "thumb_pip",
                           "index_mata", "index_mcp",  "index_pip", "index_dip",
                           "middle_mata", "middle_mcp", "middle_pip", "middle_dip",
                           "pinky_mata", "pinky_mcp",  "pinky_pip", "pinky_dip",
                           "ring_mata",  "ring_mcp",   "ring_pip",  "ring_dip"};
  std::vector<std::string> out;
  for (const std::string& name : kLegacyDfs) {
    out.push_back(name);
    if (name == "left_hand" || name == "right_hand") {
      const std::string side = name == "left_hand" ? "left_" : "right_";
      for (const char* finger : fingers) out.push_back(side + finger);
    }
  }
  return out;
}

}  // namespace

TEST(JointOrder, TheBodyAloneIsAlreadyInOrder) {
  EXPECT_EQ(Reorder(kLegacyDfs), kLegacyDfs);
}

TEST(JointOrder, TheBodyJointsKeepTheirNumbersWhenFingersAreAdded) {
  const std::vector<std::string> ordered = Reorder(RiggedDfs());
  ASSERT_EQ(ordered.size(), 58u);
  for (size_t i = 0; i < kLegacyDfs.size(); i++) {
    EXPECT_EQ(ordered[i], kLegacyDfs[i]) << "joint " << i << " moved";
  }
}

TEST(JointOrder, FingersFollowInDepthFirstOrder) {
  const std::vector<std::string> ordered = Reorder(RiggedDfs());
  ASSERT_EQ(ordered.size(), 58u);
  // left hand's nineteen, then the right hand's, chain by chain
  EXPECT_EQ(ordered[20], "left_thumb_mata");
  EXPECT_EQ(ordered[22], "left_thumb_pip");
  EXPECT_EQ(ordered[23], "left_index_mata");
  EXPECT_EQ(ordered[38], "left_ring_dip");
  EXPECT_EQ(ordered[39], "right_thumb_mata");
  EXPECT_EQ(ordered[57], "right_ring_dip");
}

TEST(JointOrder, ThePermutationIsAPermutation) {
  const std::vector<std::string> dfs = RiggedDfs();
  const std::vector<int> permutation = JointOrder::Permutation(dfs);
  ASSERT_EQ(permutation.size(), dfs.size());
  std::vector<bool> seen(dfs.size(), false);
  for (int index : permutation) {
    ASSERT_GE(index, 0);
    ASSERT_LT(index, (int)dfs.size());
    EXPECT_FALSE(seen[index]);
    seen[index] = true;
  }
}

TEST(JointOrder, AnIncompleteSkeletonKeepsWhatItHas) {
  // the legacy utility skeleton has no clavicles or hip; the joints it does
  // have must still come out in body order, and nothing may be invented
  const std::vector<std::string> partial = {"body", "middle", "neck", "head",
                                            "left_shoulder", "left_elbow",
                                            "left_hand"};
  const std::vector<std::string> ordered = Reorder(partial);
  ASSERT_EQ(ordered.size(), partial.size());
  EXPECT_EQ(ordered, partial);
}

TEST(JointOrder, AnUnknownNodeGoesAfterTheBody) {
  const std::vector<std::string> dfs = {"body", "cape_01", "hip", "middle"};
  const std::vector<std::string> ordered = Reorder(dfs);
  ASSERT_EQ(ordered.size(), 4u);
  EXPECT_EQ(ordered[0], "body");
  EXPECT_EQ(ordered[1], "hip");
  EXPECT_EQ(ordered[2], "middle");
  EXPECT_EQ(ordered[3], "cape_01");
}

TEST(JointOrder, TheBodyNamesAreTheTwentyTheEncodingCanReach) {
  // the vertex-colour fallback can name joints 0..25; all twenty body joints
  // have to sit inside that, or an old model cannot address them at all
  const std::vector<std::string>& body = JointOrder::BodyJoints();
  ASSERT_EQ(body.size(), 20u);
  EXPECT_EQ(body.front(), "body");
  EXPECT_EQ(body.back(), "right_hand");
}

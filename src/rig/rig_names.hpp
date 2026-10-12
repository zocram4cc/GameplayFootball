#pragma once

// Structural knowledge of the PES body + hand rig: names, hierarchy and track
// assignment only, no PES numbers. Ported from GameplayFootball
// tools/pes21_import/retarget.py (GF_BODY_NODES, _finger_nodes, HELPER_TO_GF,
// _HELPER_FAMILIES, PES_TRACK_MAP, _ALIGN_REF_CHILD).

#include <string>
#include <utility>
#include <vector>

namespace pesrig {

struct NodeName {
  std::string gfName, pesBone, gfParent;  // gfParent empty for the root
};

// PES's mover: the anim skeleton root, driven by gani unit 1 ("motion").
inline constexpr const char* ROOT_BONE = "sk_root_hip";

// The twenty body joints first, in joint-ID order (GF jointorder.cpp), then
// the thirty-eight finger joints of PES's hand rig in its unit order
// (thumb, index, middle, pinky, ring; thumb has three bones, the rest four).
inline const std::vector<NodeName>& RigNodeNames() {
  static const std::vector<NodeName> nodes = [] {
    std::vector<NodeName> out = {
        {"body", ROOT_BONE, ""},
        {"hip", "dsk_hip", "body"},
        {"left_thigh", "sk_thigh_l", "hip"},
        {"left_knee", "sk_leg_l", "left_thigh"},
        {"left_ankle", "sk_foot_l", "left_knee"},
        {"right_thigh", "sk_thigh_r", "hip"},
        {"right_knee", "sk_leg_r", "right_thigh"},
        {"right_ankle", "sk_foot_r", "right_knee"},
        {"middle", "sk_belly", "body"},
        {"chest", "sk_chest", "middle"},
        {"neck", "sk_neck", "chest"},
        {"head", "sk_head", "neck"},
        {"left_clavicle", "sk_shoulder_l", "chest"},
        {"left_shoulder", "sk_upperarm_l", "left_clavicle"},
        {"left_elbow", "sk_forearm_l", "left_shoulder"},
        {"left_hand", "sk_hand_l", "left_elbow"},
        {"right_clavicle", "sk_shoulder_r", "chest"},
        {"right_shoulder", "sk_upperarm_r", "right_clavicle"},
        {"right_elbow", "sk_forearm_r", "right_shoulder"},
        {"right_hand", "sk_hand_r", "right_elbow"},
    };
    const std::pair<const char*, std::vector<const char*>> fingers[] = {
        {"thumb", {"mata", "mcp", "pip"}},
        {"index", {"mata", "mcp", "pip", "dip"}},
        {"middle", {"mata", "mcp", "pip", "dip"}},
        {"pinky", {"mata", "mcp", "pip", "dip"}},
        {"ring", {"mata", "mcp", "pip", "dip"}},
    };
    for (const auto& [side, suffix] : {std::pair{"left", "_l"}, std::pair{"right", "_r"}}) {
      for (const auto& [finger, segments] : fingers) {
        for (size_t i = 0; i < segments.size(); ++i) {
          const std::string node = std::string(side) + "_" + finger + "_" + segments[i];
          const std::string parent = i == 0 ? std::string(side) + "_hand"
                                            : std::string(side) + "_" + finger + "_" + segments[i - 1];
          out.push_back({node, std::string("skh_") + finger + "_" + segments[i] + suffix, parent});
        }
      }
    }
    return out;
  }();
  return nodes;
}

inline constexpr int BODY_NODE_COUNT = 20;

// Reference child per bone: the child offset whose render->anim swing defines
// the bone's alignment W. Leaves (feet, head, hands) use Fox tip helpers.
inline const std::vector<std::pair<std::string, std::string>>& AlignRefChild() {
  static const std::vector<std::pair<std::string, std::string>> table = {
      {ROOT_BONE, "sk_belly"}, {"dsk_hip", "sk_thigh_l"},
      {"sk_thigh_l", "sk_leg_l"}, {"sk_leg_l", "sk_foot_l"},
      {"sk_thigh_r", "sk_leg_r"}, {"sk_leg_r", "sk_foot_r"},
      {"sk_belly", "sk_chest"}, {"sk_chest", "sk_neck"}, {"sk_neck", "sk_head"},
      {"sk_shoulder_l", "sk_upperarm_l"}, {"sk_upperarm_l", "sk_forearm_l"}, {"sk_forearm_l", "sk_hand_l"},
      {"sk_shoulder_r", "sk_upperarm_r"}, {"sk_upperarm_r", "sk_forearm_r"}, {"sk_forearm_r", "sk_hand_r"},
  };
  return table;
}

// Body gani track assignment: (unit, segment) -> PES bone. Units follow
// body_skel.frig order; unit 0 is RIG_ROOT, unit 1 the mover.
struct TrackSlot {
  int unit, segment;
  const char* bone;
};
inline constexpr TrackSlot PES_TRACK_MAP[] = {
    {2, 0, "dsk_hip"},
    {3, 1, "sk_thigh_l"}, {3, 2, "sk_leg_l"}, {4, 0, "sk_foot_l"},
    {5, 1, "sk_thigh_r"}, {5, 2, "sk_leg_r"}, {6, 0, "sk_foot_r"},
    {7, 0, "sk_belly"}, {8, 0, "sk_chest"},
    {9, 0, "sk_neck"}, {10, 0, "sk_head"},
    {11, 0, "sk_shoulder_l"}, {11, 2, "sk_upperarm_l"}, {11, 3, "sk_forearm_l"},
    {12, 0, "sk_hand_l"},
    {13, 0, "sk_shoulder_r"}, {13, 2, "sk_upperarm_r"}, {13, 3, "sk_forearm_r"},
    {14, 0, "sk_hand_r"},
};
inline constexpr int PES_ROOT_UNIT = 0;
inline constexpr int PES_MOTION_UNIT = 1;

// Helper bones that are never independently animated -> the GF node they
// rigidly follow. Curated by anatomy from the body.skl bone list.
inline const std::vector<std::pair<std::string, std::string>>& HelperToGf() {
  static const std::vector<std::pair<std::string, std::string>> table = {
      {"dsk_back", "middle"}, {"dsk_belly_scale", "middle"},
      {"dsk_hip_l", "hip"}, {"dsk_hip_r", "hip"},
      {"dsk_pants_l", "hip"}, {"dsk_pants_r", "hip"},
      {"dsk_pos_pants_l", "hip"}, {"dsk_pos_pants_r", "hip"},
      {"dsk_sternum_l", "chest"}, {"dsk_sternum_r", "chest"},
      {"dsk_pectoralis_l", "chest"}, {"dsk_pectoralis_r", "chest"},
      {"dsk_pos_pectoralis_l", "chest"}, {"dsk_pos_pectoralis_r", "chest"},
      {"dsk_scapula_l", "chest"}, {"dsk_scapula_r", "chest"},
      {"dsk_pos_scapula_l", "chest"}, {"dsk_pos_scapula_r", "chest"},
      {"dsk_clavicle_l", "left_clavicle"}, {"dsk_clavicle_r", "right_clavicle"},
      {"dsk_trapezius_l", "left_clavicle"}, {"dsk_trapezius_r", "right_clavicle"},
      {"dsk_collar_l", "chest"}, {"dsk_collar_r", "chest"},
      {"dsk_scm", "neck"}, {"dsk_neckback", "neck"},
      {"dsk_deltoid_l", "left_shoulder"}, {"dsk_deltoid_r", "right_shoulder"},
      {"dsk_underarm_l", "left_shoulder"}, {"dsk_underarm_r", "right_shoulder"},
      {"dsk_underarm_ba_l", "left_shoulder"}, {"dsk_underarm_ba_r", "right_shoulder"},
      {"dsk_elbow_l", "left_elbow"}, {"dsk_elbow_r", "right_elbow"},
      {"dsk_forearm_t_l", "left_elbow"}, {"dsk_forearm_t_r", "right_elbow"},
      {"dsk_forearm_l", "left_elbow"}, {"dsk_forearm_r", "right_elbow"},
      {"dsk_wrist_l", "left_hand"}, {"dsk_wrist_r", "right_hand"},
      {"dsk_thighmain_l", "left_thigh"}, {"dsk_thighmain_r", "right_thigh"},
      {"dsk_thigh_l", "left_thigh"}, {"dsk_thigh_r", "right_thigh"},
      {"dsk_pos_thigh_l", "left_thigh"}, {"dsk_pos_thigh_r", "right_thigh"},
      {"dsk_knee_l", "left_knee"}, {"dsk_knee_r", "right_knee"},
      {"dsk_kneeback_l", "left_knee"}, {"dsk_kneeback_r", "right_knee"},
      {"dsk_leg_l", "left_knee"}, {"dsk_leg_r", "right_knee"},
      {"dsk_foot_l", "left_ankle"}, {"dsk_foot_r", "right_ankle"},
      {"dsk_toe_l", "left_ankle"}, {"dsk_toe_r", "right_ankle"},
      {"sk_toe_l", "left_ankle"}, {"sk_toe_r", "right_ankle"},
  };
  return table;
}

// Name-pattern families (prefix match), checked after the exact table.
struct HelperFamily {
  const char* prefix;
  const char* left;   // nullptr = no mapping for that side
  const char* right;
  const char* plain;  // name with neither side marker; nullptr = use left
};
inline constexpr HelperFamily HELPER_FAMILIES[] = {
    {"dsk_hem_", "left_thigh", "right_thigh", nullptr},
    {"dsk_pos_hem_", "left_thigh", "right_thigh", nullptr},
    {"dsk_belly_", "middle", "middle", nullptr},
    {"dsk_pos_belly_", "middle", "middle", nullptr},
    {"dsk_sleeve_", "left_shoulder", "right_shoulder", nullptr},
    {"tip_dsk_sleeve_", "left_shoulder", "right_shoulder", nullptr},
    {"tip_dsk_pos_sleeve_", "left_shoulder", "right_shoulder", nullptr},
    {"pos_arm_target_", "left_shoulder", "right_shoulder", nullptr},
    {"dsk_upperarm_", "left_shoulder", "right_shoulder", nullptr},
    {"dsk_ear", "head", "head", nullptr},
    {"tip_dsk_toe", "left_ankle", "right_ankle", nullptr},
    // Fingers PES ships beyond the hand rig (glove/cloth variants) ride the wrist.
    {"skh_", "left_hand", "right_hand", nullptr},
    // Face muscles collapse onto the head.
    {"skf_", "head", "head", "head"},
};

}  // namespace pesrig

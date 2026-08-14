// FaceRig math: weight-map parsing and pose-offset blending (pure parts).

#include <gtest/gtest.h>

#include <sstream>

#include "utils/facerigdata.hpp"

namespace {

const char* kWeights =
    "bone,skf_jaw,0.0,0.0,1.60\n"
    "bone,skf_lip_t_c,0.0,-0.05,1.62\n"
    "v,0,0.010000,-0.060000,1.610000,skf_jaw:0.7500,skf_lip_t_c:0.2500\n"
    "v,1,-0.010000,-0.060000,1.630000,skf_lip_t_c:1.0000\n";

TEST(FaceRigData, ParsesBonesAndWeightedVertices) {
  std::istringstream in(kWeights);
  blunted::FaceRigData rig;
  ASSERT_TRUE(rig.Load(in));
  EXPECT_EQ(rig.bones.size(), 2u);
  ASSERT_EQ(rig.vertices.size(), 2u);
  EXPECT_FLOAT_EQ(rig.vertices[0].position[2], 1.61f);
  ASSERT_EQ(rig.vertices[0].weights.size(), 2u);
  EXPECT_EQ(rig.vertices[0].weights[0].bone, 0);  // skf_jaw is bone index 0
  EXPECT_FLOAT_EQ(rig.vertices[0].weights[0].weight, 0.75f);
}

TEST(FaceRigData, PoseOffsetsBlendByWeight) {
  std::istringstream in(kWeights);
  blunted::FaceRigData rig;
  ASSERT_TRUE(rig.Load(in));

  // a pose that drops the jaw 2cm
  std::map<std::string, std::array<float, 3>> pose;
  pose["skf_jaw"] = {0.0f, 0.0f, -0.02f};

  auto offsets = rig.PoseOffsets(pose, 1.0f);
  ASSERT_EQ(offsets.size(), 2u);
  EXPECT_NEAR(offsets[0][2], -0.015f, 1e-5);  // 0.75 * -0.02
  EXPECT_NEAR(offsets[1][2], 0.0f, 1e-5);     // lip vertex: jaw weight 0

  // half blend
  auto half = rig.PoseOffsets(pose, 0.5f);
  EXPECT_NEAR(half[0][2], -0.0075f, 1e-5);
}

TEST(FaceRigData, RejectsEmpty) {
  std::istringstream in("");
  blunted::FaceRigData rig;
  EXPECT_FALSE(rig.Load(in));
}

}  // namespace

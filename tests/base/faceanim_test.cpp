// .faceanim loader: the open text format tools/pes21_import/face_to_anim.py
// exports PES facial expressions into (skf_* bone rotation/translation keys).

#include <gtest/gtest.h>

#include <sstream>

#include "utils/faceanim.hpp"

namespace {

const char* kSample =
    "skf_jaw,0,0.000000,0.000000,0.000000,1.000000,10,0.100000,0.000000,"
    "0.000000,0.994987\n"
    "skf_jaw_pos,0,0.000000,-0.001000,0.000000\n"
    "skf_brow_o_l,0,-0.070000,0.000000,0.000000,0.997546\n"
    "<frames>\n"
    "\t10\n"
    "</frames>\n";

TEST(FaceAnim, ParsesRotationAndTranslationTracks) {
  std::istringstream in(kSample);
  blunted::FaceAnim anim;
  ASSERT_TRUE(anim.Load(in));
  EXPECT_EQ(anim.GetFrameCount(), 10);
  ASSERT_EQ(anim.GetRotationTracks().size(), 2u);
  ASSERT_EQ(anim.GetTranslationTracks().size(), 1u);

  const auto& jaw = anim.GetRotationTracks().at("skf_jaw");
  ASSERT_EQ(jaw.size(), 2u);
  EXPECT_EQ(jaw[0].frame, 0);
  EXPECT_FLOAT_EQ(jaw[0].values[3], 1.0f);
  EXPECT_EQ(jaw[1].frame, 10);
  EXPECT_FLOAT_EQ(jaw[1].values[0], 0.1f);

  const auto& jawPos = anim.GetTranslationTracks().at("skf_jaw");
  ASSERT_EQ(jawPos.size(), 1u);
  EXPECT_FLOAT_EQ(jawPos[0].values[1], -0.001f);
}

TEST(FaceAnim, RejectsGarbage) {
  std::istringstream in("this is not a faceanim");
  blunted::FaceAnim anim;
  EXPECT_FALSE(anim.Load(in));
}

TEST(FaceAnim, EmptyFails) {
  std::istringstream in("");
  blunted::FaceAnim anim;
  EXPECT_FALSE(anim.Load(in));
}

}  // namespace

// A clip may carry finger channels, and a clip without them leaves the fingers
// alone.
//
// The .anim format is one CSV line per node, and Animation::LoadData keys frames by
// the name in column zero - so finger lines are additive, and a clip that has none
// simply never touches those nodes. That is what lets the hand rig drive them
// (handrig.hpp) while a clip that wants to author them itself still can. These
// tests pin both halves, because "additive" is a property of the loader and not of
// the file.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "utils/animation.hpp"

using blunted::Animation;
using blunted::Quaternion;
using blunted::Vector3;

namespace {

const char* kBodyOnly =
    "player,0,0,0,0,10,0,0,0\n"
    "body,0,0,0,0,1,10,0,0,0,1\n"
    "left_hand,0,0,0,0,1,10,0,0,0,1\n"
    "<type>\n\tmovement\n</type>\n";

// the same clip with two finger channels appended
const char* kWithFingers =
    "player,0,0,0,0,10,0,0,0\n"
    "body,0,0,0,0,1,10,0,0,0,1\n"
    "left_hand,0,0,0,0,1,10,0,0,0,1\n"
    "left_index_pip,0,0.5,0,0,0.866025,10,0.258819,0,0,0.965926\n"
    "right_index_pip,0,0.5,0,0,0.866025,10,0.258819,0,0,0.965926\n"
    "<type>\n\tmovement\n</type>\n";

std::string WriteTemp(const char* name, const char* text) {
  const std::string path = std::string("/tmp/gf_anim_fingers_") + name;
  std::ofstream out(path);
  out << text;
  out.close();
  return path;
}

float Angle(const Quaternion& q) {
  return 2.0f * acos(fminf(1.0f, fabsf(q.elements[3]))) * 180.0f / M_PI;
}

}  // namespace

TEST(AnimFingerChannels, AClipWithoutThemHasNoFingerKeys) {
  Animation anim;
  anim.Load(WriteTemp("body_only.anim", kBodyOnly));
  Quaternion orientation;
  Vector3 position;
  EXPECT_FALSE(anim.GetKeyFrame("left_index_pip", 0, orientation, position));
  // and the nodes it does carry are still there
  EXPECT_TRUE(anim.GetKeyFrame("left_hand", 0, orientation, position));
}

TEST(AnimFingerChannels, FingerKeysSurviveTheRoundTrip) {
  Animation anim;
  anim.Load(WriteTemp("with_fingers.anim", kWithFingers));

  Quaternion orientation;
  Vector3 position;
  ASSERT_TRUE(anim.GetKeyFrame("left_index_pip", 0, orientation, position));
  EXPECT_NEAR(Angle(orientation), 60.0f, 0.01f);
  ASSERT_TRUE(anim.GetKeyFrame("left_index_pip", 10, orientation, position));
  EXPECT_NEAR(Angle(orientation), 30.0f, 0.01f);
  ASSERT_TRUE(anim.GetKeyFrame("right_index_pip", 0, orientation, position));
  EXPECT_NEAR(Angle(orientation), 60.0f, 0.01f);
}

TEST(AnimFingerChannels, FingerLinesDoNotDisturbTheBodyOrTheRoot) {
  Animation body;
  body.Load(WriteTemp("body_only2.anim", kBodyOnly));
  Animation fingers;
  fingers.Load(WriteTemp("with_fingers2.anim", kWithFingers));

  EXPECT_EQ(body.GetFrameCount(), fingers.GetFrameCount());
  EXPECT_EQ(body.GetAnimType(), fingers.GetAnimType());

  Quaternion a, b;
  Vector3 pa, pb;
  ASSERT_TRUE(body.GetKeyFrame("player", 10, a, pa));
  ASSERT_TRUE(fingers.GetKeyFrame("player", 10, b, pb));
  EXPECT_FLOAT_EQ(pa.coords[0], pb.coords[0]);
  EXPECT_FLOAT_EQ(pa.coords[1], pb.coords[1]);
  EXPECT_FLOAT_EQ(pa.coords[2], pb.coords[2]);
  ASSERT_TRUE(body.GetKeyFrame("left_hand", 10, a, pa));
  ASSERT_TRUE(fingers.GetKeyFrame("left_hand", 10, b, pb));
  for (int i = 0; i < 4; i++) EXPECT_FLOAT_EQ(a.elements[i], b.elements[i]);
}

TEST(AnimFingerChannels, AFingerChannelInterpolatesBetweenItsKeys) {
  Animation anim;
  anim.Load(WriteTemp("with_fingers3.anim", kWithFingers));
  Quaternion orientation;
  Vector3 position;
  // frame 5 has no key of its own; the loader interpolates
  anim.GetKeyFrame("left_index_pip", 5, orientation, position);
  const float halfway = Angle(orientation);
  EXPECT_GT(halfway, 30.0f);
  EXPECT_LT(halfway, 60.0f);
}

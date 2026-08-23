// Which hand a player wears, and where that hand came from.
//
// PES's body animation carries no finger channels: all 4,389 clips in dt13's
// body_anime_file*.mtar are fifteen units / twenty-seven segments, the twenty bones
// of body_skel.frig, none of them skh_*. The fingers come from a separate library -
// pes_human_hand_141203.frig plus 162 one-frame ganis under
// common/anime/FoxAnim/Hand/Animations - which the game picks between by name from
// code (all 162 names sit in a string pool in PES2021.exe; no shipped table binds
// them to animations).
//
// So the engine does the same: tools/pes21_import/hand_poses.py converts that
// library to handposes.txt, and ChooseHandPose below decides which of them a player
// is wearing from what the engine already knows. The pose names are PES's own.

#include <gtest/gtest.h>

#include <cmath>
#include <sstream>
#include <string>

#include "base/math/quaternion.hpp"
#include "onthepitch/player/humanoid/handrig.hpp"
#include "utils/handposedata.hpp"

using blunted::BlendHandPose;
using blunted::ChooseHandPose;
using blunted::e_HandPose;
using blunted::HandPose;
using blunted::HandPoseData;
using blunted::HandPoseName;
using blunted::Quaternion;

namespace {

Quaternion AxisAngle(float x, float y, float z, float degrees) {
  Quaternion q;
  q.SetAngleAxis(degrees * M_PI / 180.0f, blunted::Vector3(x, y, z));
  return q;
}

std::string TwoPoses() {
  return "# gfhandposes 1\n"
         "pose nigiri\n"
         "left_index_pip 0.500000 0.000000 0.000000 0.866025\n"
         "right_index_pip 0.500000 0.000000 0.000000 0.866025\n"
         "pose relax\n"
         "left_index_pip 0.258819 0.000000 0.000000 0.965926\n"
         "right_index_pip 0.258819 0.000000 0.000000 0.965926\n";
}

float Angle(const Quaternion& q) {
  return 2.0f * acos(fminf(1.0f, fabsf(q.elements[3]))) * 180.0f / M_PI;
}

}  // namespace

TEST(HandPoseData, ReadsNamedPosesAndTheirJoints) {
  std::istringstream in(TwoPoses());
  HandPoseData data;
  ASSERT_TRUE(data.Load(in));
  EXPECT_EQ(data.PoseCount(), 2u);
  EXPECT_TRUE(data.Has("nigiri"));
  EXPECT_TRUE(data.Has("relax"));
  EXPECT_FALSE(data.Has("no_such_pose"));

  const HandPose* pose = data.Find("nigiri");
  ASSERT_NE(pose, nullptr);
  ASSERT_EQ(pose->size(), 2u);
  EXPECT_NEAR(Angle(pose->at("left_index_pip")), 60.0f, 1e-3f);
}

TEST(HandPoseData, RejectsAFileWithoutTheHeader) {
  std::istringstream in("pose relax\nleft_index_pip 0 0 0 1\n");
  HandPoseData data;
  EXPECT_FALSE(data.Load(in));
  EXPECT_EQ(data.PoseCount(), 0u);
}

TEST(HandPoseData, ARottenLineCostsItsOwnJointOnly) {
  std::istringstream in("# gfhandposes 1\n"
                        "pose relax\n"
                        "left_index_pip nonsense\n"
                        "right_index_pip 0.000000 0.000000 0.000000 1.000000\n");
  HandPoseData data;
  ASSERT_TRUE(data.Load(in));
  const HandPose* pose = data.Find("relax");
  ASSERT_NE(pose, nullptr);
  EXPECT_EQ(pose->size(), 1u);
  EXPECT_EQ(pose->count("right_index_pip"), 1u);
}

TEST(HandPoseData, EveryJointItMentionsIsListedOnce) {
  std::istringstream in(TwoPoses());
  HandPoseData data;
  ASSERT_TRUE(data.Load(in));
  const std::vector<std::string>& joints = data.Joints();
  ASSERT_EQ(joints.size(), 2u);
  EXPECT_EQ(joints[0], "left_index_pip");
  EXPECT_EQ(joints[1], "right_index_pip");
}

TEST(HandPoseSelection, EachContextPicksPesOwnPoseForIt) {
  // the keeper meeting the ball opens his hands, and closes them on it
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Deflect, 3.0f),
            e_HandPose::KeeperReaching);
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Catch, 3.0f), e_HandPose::KeeperHolding);
  // a celebration claps
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Special, 2.0f), e_HandPose::Celebrating);
  // going down, the hands brace
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Trip, 6.0f), e_HandPose::Falling);
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Sliding, 6.0f), e_HandPose::Falling);
}

TEST(HandPoseSelection, SpeedClosesTheHand) {
  // standing and walking keep PES's neutral hand; running loosens it; a sprint
  // grips, which is what PES's own move_nigiri is
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Movement, 0.0f), e_HandPose::Neutral);
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Movement, 1.5f), e_HandPose::Neutral);
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Movement, 4.0f), e_HandPose::Running);
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Movement, 8.0f), e_HandPose::Sprinting);
}

TEST(HandPoseSelection, TheSprintThresholdIsTheEnginesOwn) {
  // the same 7 m/s the face rig calls a sprint (facerig.hpp)
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Movement, 6.9f), e_HandPose::Running);
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Movement, 7.1f), e_HandPose::Sprinting);
}

TEST(HandPoseSelection, BeingOnTheBallDoesNotOverrideGoingDown) {
  // a player tripped while dribbling still puts his hands out
  EXPECT_EQ(ChooseHandPose(e_FunctionType_Trip, 0.0f), e_HandPose::Falling);
}

TEST(HandPoseSelection, EveryPoseNamesAPesPose) {
  const e_HandPose all[] = {e_HandPose::Neutral,        e_HandPose::Running,
                            e_HandPose::Sprinting,      e_HandPose::Celebrating,
                            e_HandPose::Falling,        e_HandPose::KeeperReaching,
                            e_HandPose::KeeperHolding};
  for (e_HandPose pose : all) {
    const std::string name = HandPoseName(pose);
    EXPECT_FALSE(name.empty());
    // PES's library names, verbatim
    EXPECT_NE(name.find_first_of("abcdefghijklmnopqrstuvwxyz_"), std::string::npos);
  }
  EXPECT_EQ(std::string(HandPoseName(e_HandPose::Neutral)), "normal");
  EXPECT_EQ(std::string(HandPoseName(e_HandPose::Running)), "relax");
  EXPECT_EQ(std::string(HandPoseName(e_HandPose::Sprinting)), "move_nigiri");
  EXPECT_EQ(std::string(HandPoseName(e_HandPose::Celebrating)), "clap");
  EXPECT_EQ(std::string(HandPoseName(e_HandPose::Falling)), "taore");
  EXPECT_EQ(std::string(HandPoseName(e_HandPose::KeeperReaching)), "open_full_ball");
  EXPECT_EQ(std::string(HandPoseName(e_HandPose::KeeperHolding)), "kp_hold");
}

TEST(HandPoseBlending, AHandArrivesAtItsPoseRatherThanSnappingToIt) {
  HandPose target;
  target["left_index_pip"] = AxisAngle(1, 0, 0, 60.0f);
  HandPose current;
  current["left_index_pip"] = Quaternion(QUATERNION_IDENTITY);

  BlendHandPose(current, target, 0.25f);
  const float afterOne = Angle(current["left_index_pip"]);
  EXPECT_GT(afterOne, 0.5f);
  EXPECT_LT(afterOne, 59.0f);

  for (int i = 0; i < 200; i++) BlendHandPose(current, target, 0.25f);
  EXPECT_NEAR(Angle(current["left_index_pip"]), 60.0f, 0.05f);
}

TEST(HandPoseBlending, AJointNoPoseMentionsReturnsToBind) {
  // a pose that says nothing about the thumb must not leave the last pose's
  // thumb behind
  HandPose target;
  target["left_index_pip"] = Quaternion(QUATERNION_IDENTITY);
  HandPose current;
  current["left_index_pip"] = Quaternion(QUATERNION_IDENTITY);
  current["left_thumb_mcp"] = AxisAngle(1, 0, 0, 80.0f);

  for (int i = 0; i < 300; i++) BlendHandPose(current, target, 0.25f);
  EXPECT_NEAR(Angle(current["left_thumb_mcp"]), 0.0f, 0.01f);
}

TEST(HandPoseBlending, ANewJointStartsFromBind) {
  HandPose target;
  target["left_pinky_dip"] = AxisAngle(1, 0, 0, 40.0f);
  HandPose current;
  BlendHandPose(current, target, 1.0f);
  ASSERT_EQ(current.count("left_pinky_dip"), 1u);
  EXPECT_NEAR(Angle(current["left_pinky_dip"]), 40.0f, 0.01f);
}

TEST(HandPoseBlending, AFullStepLandsExactlyOnThePose) {
  HandPose target;
  target["left_index_dip"] = AxisAngle(0, 1, 0, 33.0f);
  HandPose current;
  current["left_index_dip"] = Quaternion(QUATERNION_IDENTITY);
  BlendHandPose(current, target, 1.0f);
  EXPECT_NEAR(Angle(current["left_index_dip"]), 33.0f, 0.01f);
}

TEST(HandPoseData, AFutureVersionIsRejectedNotHalfRead) {
  // A prefix match would take "# gfhandposes 10" for version 1 and mis-read
  // whatever that format turns out to mean. The tools reject it; so must this.
  std::istringstream in("# gfhandposes 10\npose relax\nleft_index_pip 0 0 0 1\n");
  HandPoseData data;
  EXPECT_FALSE(data.Load(in));
}

TEST(HandPoseData, ACarriageReturnOnTheHeaderIsForgiven) {
  std::istringstream in("# gfhandposes 1\r\npose relax\nleft_index_pip 0 0 0 1\n");
  HandPoseData data;
  EXPECT_TRUE(data.Load(in));
  EXPECT_EQ(data.PoseCount(), 1u);
}

// .camtrack loader: imported PES camera cuts (canm_to_camtrack.py output).

#include <gtest/gtest.h>

#include <cmath>
#include <sstream>

#include "utils/camtrack.hpp"

namespace {

const char* kTrack =
    "0,82.33,-72.51,28.98,-0.5931,-0.2548,-0.2892,-0.7069,67.381,0.50,400.0\n"
    "1,80.00,-70.00,28.00,-0.5931,-0.2548,-0.2892,-0.7069,60.000,0.50,400.0\n"
    "2,78.00,-68.00,27.00,-0.5931,-0.2548,-0.2892,-0.7069,50.000,0.50,400.0\n";

TEST(CamTrack, ParsesFrames) {
  std::istringstream in(kTrack);
  blunted::CamTrack track;
  ASSERT_TRUE(track.Load(in));
  EXPECT_EQ(track.GetFrameCount(), 3);
  auto f0 = track.Sample(0.0f);
  EXPECT_FLOAT_EQ(f0.position[0], 82.33f);
  EXPECT_FLOAT_EQ(f0.fov, 67.381f);
  EXPECT_FLOAT_EQ(f0.nearPlane, 0.5f);
}

TEST(CamTrack, InterpolatesBetweenFrames) {
  std::istringstream in(kTrack);
  blunted::CamTrack track;
  ASSERT_TRUE(track.Load(in));
  auto mid = track.Sample(0.5f);
  EXPECT_NEAR(mid.position[0], 81.165f, 1e-3);
  EXPECT_NEAR(mid.fov, 63.6905f, 1e-3);
  // clamps past the end
  auto end = track.Sample(9.0f);
  EXPECT_FLOAT_EQ(end.position[0], 78.0f);
}

TEST(CamTrack, EmptyFails) {
  std::istringstream in("");
  blunted::CamTrack track;
  EXPECT_FALSE(track.Load(in));
}

// --- retargeting: goal camtracks are authored against PES's celebration
// staging; the engine re-aims them at the actual celebrating player ---

TEST(CamTrackRetarget, ForwardOfIdentityIsMinusZ) {
  auto f = blunted::CamTrackForward({0.0f, 0.0f, 0.0f, 1.0f});
  EXPECT_NEAR(f[0], 0.0f, 1e-5);
  EXPECT_NEAR(f[1], 0.0f, 1e-5);
  EXPECT_NEAR(f[2], -1.0f, 1e-5);
}

TEST(CamTrackRetarget, ForwardOfXRotation) {
  // +90 deg about X points the camera along +Y
  const float s = 0.70710678f;
  auto f = blunted::CamTrackForward({s, 0.0f, 0.0f, s});
  EXPECT_NEAR(f[0], 0.0f, 1e-5);
  EXPECT_NEAR(f[1], 1.0f, 1e-5);
  EXPECT_NEAR(f[2], 0.0f, 1e-5);
}

TEST(CamTrackRetarget, AimsAtTarget) {
  blunted::CamTrackFrame frame;
  frame.position = {0.0f, -10.0f, 2.0f};
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};  // authored: looking down
  frame.fov = 0.9f;                           // authored: super-telephoto
  frame.nearPlane = 35.0f;                    // authored: subject past 35m
  auto out = blunted::RetargetCamTrackFrame(frame, {0.0f, 0.0f, 2.0f}, 1.5f,
                                            0.75f);
  // position untouched (target is 10m away, beyond the minimum distance)
  EXPECT_NEAR(out.position[1], -10.0f, 1e-4);
  // now looks straight at the target (+Y), i.e. +90 deg about X
  auto fwd = blunted::CamTrackForward(out.rotation);
  EXPECT_NEAR(fwd[0], 0.0f, 1e-4);
  EXPECT_NEAR(fwd[1], 1.0f, 1e-4);
  EXPECT_NEAR(fwd[2], 0.0f, 1e-4);
  // lens widened just enough to cover the subject at 10m:
  // 2*atan(0.75/10) = 8.578 deg
  EXPECT_NEAR(out.fov, 8.578f, 0.01f);
  // near clip pulled in so the subject can't be culled
  EXPECT_LT(out.nearPlane, 10.0f);
  EXPECT_GE(out.nearPlane, 0.1f);
}

TEST(CamTrackRetarget, KeepsAuthoredWideLens) {
  blunted::CamTrackFrame frame;
  frame.position = {0.0f, -10.0f, 2.0f};
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  frame.fov = 40.0f;
  frame.nearPlane = 0.5f;
  auto out = blunted::RetargetCamTrackFrame(frame, {0.0f, 0.0f, 2.0f}, 1.5f,
                                            0.75f);
  EXPECT_NEAR(out.fov, 40.0f, 1e-4);      // wide authored lens is kept
  EXPECT_NEAR(out.nearPlane, 0.5f, 1e-4); // sane authored near is kept
}

TEST(CamTrackRetarget, PushesBackWhenInsideMinimumDistance) {
  blunted::CamTrackFrame frame;
  frame.position = {0.0f, -0.5f, 2.0f};  // 0.5m from the player's head
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  auto out = blunted::RetargetCamTrackFrame(frame, {0.0f, 0.0f, 2.0f}, 1.5f,
                                            0.75f);
  // pushed straight back along the aim line to the minimum distance
  EXPECT_NEAR(out.position[0], 0.0f, 1e-4);
  EXPECT_NEAR(out.position[1], -1.5f, 1e-4);
  EXPECT_NEAR(out.position[2], 2.0f, 1e-4);
  auto fwd = blunted::CamTrackForward(out.rotation);
  EXPECT_NEAR(fwd[1], 1.0f, 1e-4);
}

TEST(CamTrackRetarget, DegenerateZeroDistanceBacksOutAlongView) {
  blunted::CamTrackFrame frame;
  frame.position = {3.0f, 4.0f, 2.0f};  // exactly at the target
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};  // authored forward: -Z
  auto out = blunted::RetargetCamTrackFrame(frame, {3.0f, 4.0f, 2.0f}, 1.5f,
                                            0.75f);
  // backs out opposite the authored view axis, then looks back at the target
  EXPECT_NEAR(out.position[0], 3.0f, 1e-4);
  EXPECT_NEAR(out.position[1], 4.0f, 1e-4);
  EXPECT_NEAR(out.position[2], 3.5f, 1e-4);
  auto fwd = blunted::CamTrackForward(out.rotation);
  EXPECT_NEAR(fwd[2], -1.0f, 1e-4);
  // no NaNs anywhere
  for (int c = 0; c < 4; c++) EXPECT_TRUE(out.rotation[c] == out.rotation[c]);
}

TEST(CamTrackRetarget, KeepsCameraUpright) {
  // aim from a high vantage: the camera's up vector must stay world-up-ish
  blunted::CamTrackFrame frame;
  frame.position = {10.0f, -20.0f, 15.0f};
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  auto out = blunted::RetargetCamTrackFrame(frame, {0.0f, 0.0f, 1.5f}, 1.5f,
                                            0.75f);
  // rotate local +Y (camera up) by the result: its Z must be positive
  const auto& q = out.rotation;
  // q * (0,1,0): standard quaternion-vector rotation
  float ux = 2.0f * (q[0] * q[1] - q[3] * q[2]);
  float uy = 1.0f - 2.0f * (q[0] * q[0] + q[2] * q[2]);
  float uz = 2.0f * (q[1] * q[2] + q[3] * q[0]);
  (void)ux; (void)uy;
  EXPECT_GT(uz, 0.5f);
}

}  // namespace

// PES's entrance camerawork is a montage, and the export carries it as one
// file: the cuts are concatenated, each keeping its own frame numbering, so the
// first column is where that cut starts in the demo's timeline. The .fdc cut
// table for ent_009_st002_cmn says 0, 100, 200, 300, 400 - a shot change every
// hundred frames, three and a third seconds - and the exported track's segments
// begin on exactly those numbers.
//
// Read as one contiguous track (which is what indexing rows does) a cut lands
// only every ten seconds, at the segment joins, and every shot drifts through
// the three seconds it should have been cut at. Sampling by the timeline the
// numbers describe makes it cut when PES cuts.
namespace {

blunted::CamTrack LoadMontage() {
  // two cuts: the first starting at 0, the second at 100, each five frames long
  std::istringstream in(
      "0,0,0,1,0,0,0,1,35,0.5,400\n"
      "1,1,0,1,0,0,0,1,35,0.5,400\n"
      "2,2,0,1,0,0,0,1,35,0.5,400\n"
      "3,3,0,1,0,0,0,1,35,0.5,400\n"
      "4,4,0,1,0,0,0,1,35,0.5,400\n"
      "100,50,0,1,0,0,0,1,35,0.5,400\n"
      "101,51,0,1,0,0,0,1,35,0.5,400\n"
      "102,52,0,1,0,0,0,1,35,0.5,400\n"
      "103,53,0,1,0,0,0,1,35,0.5,400\n"
      "104,54,0,1,0,0,0,1,35,0.5,400\n");
  blunted::CamTrack track;
  track.Load(in);
  return track;
}

}  // namespace

TEST(CamTrackTimeline, PlaysTheCutThatHasStarted) {
  const blunted::CamTrack track = LoadMontage();
  EXPECT_NEAR(track.SampleTimeline(0.0f).position[0], 0.0f, 1e-4);
  EXPECT_NEAR(track.SampleTimeline(3.0f).position[0], 3.0f, 1e-4);
  EXPECT_NEAR(track.SampleTimeline(100.0f).position[0], 50.0f, 1e-4);
  EXPECT_NEAR(track.SampleTimeline(102.0f).position[0], 52.0f, 1e-4);
}

TEST(CamTrackTimeline, HoldsACutsLastFrameUntilTheNextOneStarts) {
  // The cut's own clip is longer than the time it is given, and shorter than the
  // gap in some packs; either way the picture holds rather than running on into
  // the next cut's frames.
  const blunted::CamTrack track = LoadMontage();
  EXPECT_NEAR(track.SampleTimeline(50.0f).position[0], 4.0f, 1e-4);
  EXPECT_NEAR(track.SampleTimeline(99.0f).position[0], 4.0f, 1e-4);
}

TEST(CamTrackTimeline, TheCutIsInstantaneous) {
  const blunted::CamTrack track = LoadMontage();
  EXPECT_NEAR(track.SampleTimeline(99.9f).position[0], 4.0f, 1e-4);
  EXPECT_NEAR(track.SampleTimeline(100.0f).position[0], 50.0f, 1e-4);
}

TEST(CamTrackTimeline, ATrackWithOneCutIsJustThatTrack) {
  std::istringstream in(
      "0,0,0,1,0,0,0,1,35,0.5,400\n"
      "1,1,0,1,0,0,0,1,35,0.5,400\n"
      "2,2,0,1,0,0,0,1,35,0.5,400\n");
  blunted::CamTrack track;
  ASSERT_TRUE(track.Load(in));
  EXPECT_NEAR(track.SampleTimeline(1.5f).position[0], 1.5f, 1e-4);
  EXPECT_EQ(track.GetTimelineFrameCount(), 3);
}

TEST(CamTrackTimeline, TheTimelineIsAsLongAsItsLastCutRunsFor) {
  const blunted::CamTrack track = LoadMontage();
  EXPECT_EQ(track.GetTimelineFrameCount(), 105);
}

// --- staging: PES authors its goal camerawork in the celebration's own space ---
//
// Measured over the 516 imported goal tracks: 448 of them aim within ten degrees of
// the local origin, from a median 12.6 m away, with a median 9-degree lens - which
// frames 1.85 m of subject at that distance, i.e. a man. The scorer stands at the
// origin and the camera is placed around him; _Z_fromL sits at +40 m on X and
// _Z_fromR at -39, and the 355 numbered celebration cameras sit dead in front at
// y = -11.6.
//
// So a goal frame is not a world position to be re-aimed but a composition to be put
// down: stage it at the scorer, turned the way he is turned, and PES's distance, lens
// and camera move all come with it.

TEST(CamTrackStage, PutsTheAuthoredOffsetDownAtTheSubject) {
  blunted::CamTrackFrame frame;
  frame.position = {0.0f, -12.0f, 0.7f};  // authored: in front of him, low
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  auto out = blunted::StageCamTrackFrame(frame, {30.0f, -20.0f, 0.0f}, 0.0f);
  EXPECT_NEAR(out.position[0], 30.0f, 1e-4);
  EXPECT_NEAR(out.position[1], -32.0f, 1e-4);
  EXPECT_NEAR(out.position[2], 0.7f, 1e-4);   // the subject stands on the ground
}

TEST(CamTrackStage, KeepsTheLensPesChose) {
  // the whole point: a 0.9-degree lens is right because the camera is 100 m out,
  // and staging keeps them together
  blunted::CamTrackFrame frame;
  frame.position = {0.0f, -99.0f, 1.5f};
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  frame.fov = 0.91f;
  frame.nearPlane = 35.0f;
  frame.farPlane = 400.0f;
  auto out = blunted::StageCamTrackFrame(frame, {10.0f, 10.0f, 0.0f}, 0.0f);
  EXPECT_FLOAT_EQ(out.fov, 0.91f);
  EXPECT_FLOAT_EQ(out.nearPlane, 35.0f);
  EXPECT_FLOAT_EQ(out.farPlane, 400.0f);
}

TEST(CamTrackStage, TurnsWithTheSubject) {
  // +90 degrees about Z: the camera authored in front of him swings round to his side
  blunted::CamTrackFrame frame;
  frame.position = {0.0f, -12.0f, 0.7f};
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  const float halfPi = 1.57079633f;
  auto out = blunted::StageCamTrackFrame(frame, {0.0f, 0.0f, 0.0f}, halfPi);
  EXPECT_NEAR(out.position[0], 12.0f, 1e-3);
  EXPECT_NEAR(out.position[1], 0.0f, 1e-3);
  EXPECT_NEAR(out.position[2], 0.7f, 1e-4);
}

TEST(CamTrackStage, TheShotStillFramesHimAfterTurning) {
  // whatever the yaw, a camera authored looking at the origin looks at the scorer
  blunted::CamTrackFrame frame;
  frame.position = {0.0f, -12.0f, 1.0f};
  frame.rotation = {0.70710678f, 0.0f, 0.0f, 0.70710678f};  // +90 about X: looks +Y
  for (float yaw = -3.0f; yaw < 3.0f; yaw += 0.7f) {
    auto out = blunted::StageCamTrackFrame(frame, {25.0f, -8.0f, 0.0f}, yaw);
    auto fwd = blunted::CamTrackForward(out.rotation);
    // the aim line from the staged camera to the scorer's chest
    const float aim[3] = {25.0f - out.position[0], -8.0f - out.position[1],
                          1.0f - out.position[2]};
    const float len = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    const float dot =
        (fwd[0] * aim[0] + fwd[1] * aim[1] + fwd[2] * aim[2]) / len;
    EXPECT_NEAR(dot, 1.0f, 1e-3) << "yaw " << yaw;
  }
}

TEST(CamTrackStage, NoTurnLeavesTheAuthoredRotationAlone) {
  blunted::CamTrackFrame frame;
  frame.rotation = {0.1f, 0.2f, 0.3f, 0.927362f};
  auto out = blunted::StageCamTrackFrame(frame, {0.0f, 0.0f, 0.0f}, 0.0f);
  for (int c = 0; c < 4; c++) EXPECT_NEAR(out.rotation[c], frame.rotation[c], 1e-5);
}

// Staging alone is not enough, because PES pans its goal cameras off the origin as
// the shot develops: on goal_celebrate_0303_mayaL0x the aim is 3 degrees off the
// local origin at frame 0, 31 by frame 70 and 60 by frame 105, and the lens opens
// from 11 to 25 degrees with it. PES's actor arrives into that shot. Ours celebrates
// in place - every one of the 266 installed celebration clips has a root that moves
// at most 6 mm - so replaying the pan literally ends up filming the sky over the
// stand, which is exactly what the first staged capture did.
//
// So the position is staged and the aim then follows the scorer. That is not the old
// re-aim-a-world-position: the camera is already at PES's authored distance, so the
// authored lens is wide enough to frame him on 77.5% of the library's 472 077 frames
// and is kept untouched there.

TEST(CamTrackStageThenAim, TheShotFollowsTheScorerWhenPesPansAway) {
  blunted::CamTrackFrame frame;
  frame.position = {3.75f, -6.89f, 0.61f};    // goal_celebrate_0303, frame 105
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};  // authored: 60 deg off the subject
  frame.fov = 25.47f;
  const std::array<float, 3> subject = {25.0f, -8.0f, 0.0f};
  auto out = blunted::StageCamTrackFrame(frame, subject, 0.0f);
  out = blunted::RetargetCamTrackFrame(
      out, {subject[0], subject[1], subject[2] + 1.0f}, 1.5f, 0.75f);
  auto fwd = blunted::CamTrackForward(out.rotation);
  const float aim[3] = {subject[0] - out.position[0], subject[1] - out.position[1],
                        subject[2] + 1.0f - out.position[2]};
  const float len = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
  EXPECT_NEAR((fwd[0] * aim[0] + fwd[1] * aim[1] + fwd[2] * aim[2]) / len, 1.0f, 1e-3);
  // and it is still where PES put it, 7.9 m out rather than at the centre spot
  EXPECT_NEAR(len, 7.90f, 0.05f);
}

TEST(CamTrackStageThenAim, TheAuthoredLensSurvivesAtTheAuthoredDistance) {
  blunted::CamTrackFrame frame;
  frame.position = {-1.51f, -9.09f, 0.52f};   // goal_celebrate_0303, frame 0
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  frame.fov = 11.42f;                          // frames 1.85 m at 9.2 m: a man
  const std::array<float, 3> subject = {0.0f, 0.0f, 0.0f};
  auto out = blunted::StageCamTrackFrame(frame, subject, 0.0f);
  out = blunted::RetargetCamTrackFrame(out, {0.0f, 0.0f, 1.0f}, 1.5f, 0.75f);
  EXPECT_FLOAT_EQ(out.fov, 11.42f);
}

TEST(CamTrackStageThenAim, StagingAloneWouldFilmTheStandBehindHim) {
  // the failure this composition exists to prevent, kept as a test so it cannot
  // come back: PES's frame 105 aim, staged faithfully, points 60 degrees past him
  blunted::CamTrackFrame frame;
  frame.position = {3.75f, -6.89f, 0.61f};
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  const std::array<float, 3> subject = {25.0f, -8.0f, 0.0f};
  auto staged = blunted::StageCamTrackFrame(frame, subject, 0.0f);
  auto fwd = blunted::CamTrackForward(staged.rotation);
  const float aim[3] = {subject[0] - staged.position[0], subject[1] - staged.position[1],
                        subject[2] + 1.0f - staged.position[2]};
  const float len = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
  const float dot = (fwd[0] * aim[0] + fwd[1] * aim[1] + fwd[2] * aim[2]) / len;
  EXPECT_LT(dot, 0.6f);  // more than 50 degrees off him
}

// How much framing guard is allowed to override PES. The guard exists so a shot
// cannot end up too tight to show anything, but once the camera is at PES's own
// distance it is PES's lens that is right - and a guard sized to a whole player
// fights it. goal_celebrate_0312_mayaL0x opens at 2.57 degrees from 17.2 m, which
// frames 0.77 m: a head and shoulders, deliberately. A 0.75 m half-height forces
// that to 5 degrees and a full-body shot; 0.15 m leaves it alone.
//
// Measured over the library's 472,077 goal frames, the guard leaves PES's lens
// untouched on 77.5% at 0.75 m, 92.5% at 0.40, 98.8% at 0.25 and 99.6% at 0.15.
TEST(CamTrackStageThenAim, PesOwnTightCloseUpIsNotOpenedUp) {
  blunted::CamTrackFrame frame;
  frame.position = {12.37f, -11.90f, 0.65f};   // goal_celebrate_0312, frame 0
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  frame.fov = 2.57f;
  auto out = blunted::StageCamTrackFrame(frame, {0.0f, 0.0f, 0.0f}, 0.0f);
  out = blunted::RetargetCamTrackFrame(out, {0.0f, 0.0f, 1.0f}, 1.5f, 0.15f);
  EXPECT_FLOAT_EQ(out.fov, 2.57f);
}

TEST(CamTrackStageThenAim, AShotTooTightToShowAnythingIsStillOpenedUp) {
  // the guard still earns its keep: a camera half a metre from his face
  blunted::CamTrackFrame frame;
  frame.position = {0.0f, -0.5f, 1.0f};
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  frame.fov = 2.0f;
  auto out = blunted::StageCamTrackFrame(frame, {0.0f, 0.0f, 0.0f}, 0.0f);
  out = blunted::RetargetCamTrackFrame(out, {0.0f, 0.0f, 1.0f}, 1.5f, 0.15f);
  EXPECT_GT(out.fov, 2.0f);
}

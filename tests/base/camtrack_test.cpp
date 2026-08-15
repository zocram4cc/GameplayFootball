// .camtrack loader: imported PES camera cuts (canm_to_camtrack.py output).

#include <gtest/gtest.h>

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
  EXPECT_FLOAT_EQ(f0.near, 0.5f);
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
  frame.near = 35.0f;                         // authored: subject past 35m
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
  EXPECT_LT(out.near, 10.0f);
  EXPECT_GE(out.near, 0.1f);
}

TEST(CamTrackRetarget, KeepsAuthoredWideLens) {
  blunted::CamTrackFrame frame;
  frame.position = {0.0f, -10.0f, 2.0f};
  frame.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  frame.fov = 40.0f;
  frame.near = 0.5f;
  auto out = blunted::RetargetCamTrackFrame(frame, {0.0f, 0.0f, 2.0f}, 1.5f,
                                            0.75f);
  EXPECT_NEAR(out.fov, 40.0f, 1e-4);   // wide authored lens is kept
  EXPECT_NEAR(out.near, 0.5f, 1e-4);   // sane authored near is kept
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

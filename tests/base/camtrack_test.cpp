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

}  // namespace

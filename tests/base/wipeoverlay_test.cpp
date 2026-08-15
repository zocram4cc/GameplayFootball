#include "utils/wipeoverlay.hpp"

#include <sstream>

#include <gtest/gtest.h>

using blunted::GetWipeFrameFilename;
using blunted::GetWipeFrameIndex;
using blunted::LoadWipeManifest;
using blunted::WipeManifest;

namespace {

WipeManifest Parse(const std::string& text) {
  std::istringstream in(text);
  WipeManifest manifest;
  EXPECT_TRUE(LoadWipeManifest(in, manifest));
  return manifest;
}

TEST(WipeOverlay, ReadsAManifest) {
  const WipeManifest manifest = Parse("fps 30\nframes wipe_%03d.png\ncount 48\n");
  EXPECT_FLOAT_EQ(30.0f, manifest.fps);
  EXPECT_EQ("wipe_%03d.png", manifest.framePattern);
  EXPECT_EQ(48, manifest.frameCount);
  EXPECT_TRUE(manifest.IsValid());
  EXPECT_FLOAT_EQ(1.6f, manifest.GetDurationSeconds());
}

TEST(WipeOverlay, IgnoresCommentsBlankLinesAndUnknownKeys) {
  const WipeManifest manifest = Parse(
      "# the replay stinger\n\nfps 25\nframes s_%d.png\ncount 10\nauthor someone\n");
  EXPECT_FLOAT_EQ(25.0f, manifest.fps);
  EXPECT_EQ(10, manifest.frameCount);
}

TEST(WipeOverlay, RejectsAManifestWithoutFrames) {
  std::istringstream in("fps 30\n");
  WipeManifest manifest;
  EXPECT_FALSE(LoadWipeManifest(in, manifest));
}

TEST(WipeOverlay, PicksTheFrameForTheElapsedTime) {
  const WipeManifest manifest = Parse("fps 10\nframes w_%02d.png\ncount 5\n");
  EXPECT_EQ(0, GetWipeFrameIndex(manifest, 0.0f));
  EXPECT_EQ(2, GetWipeFrameIndex(manifest, 0.25f));
  EXPECT_EQ(4, GetWipeFrameIndex(manifest, 0.49f));
}

TEST(WipeOverlay, ReportsTheEndOfTheWipe) {
  const WipeManifest manifest = Parse("fps 10\nframes w_%02d.png\ncount 5\n");
  EXPECT_EQ(-1, GetWipeFrameIndex(manifest, 0.5f));
  EXPECT_EQ(-1, GetWipeFrameIndex(manifest, 3.0f));
}

TEST(WipeOverlay, NumbersFramesFromTheFirstFrameOffset) {
  const WipeManifest manifest =
      Parse("fps 30\nframes wipe_%03d.png\ncount 3\nfirst 1\n");
  EXPECT_EQ("wipe_001.png", GetWipeFrameFilename(manifest, 0));
  EXPECT_EQ("wipe_003.png", GetWipeFrameFilename(manifest, 2));
}

}  // namespace

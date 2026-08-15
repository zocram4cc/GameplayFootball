#include "onthepitch/modelviewer.hpp"

#include <cmath>

#include <gtest/gtest.h>

using blunted::ModelViewerAccepts;
using blunted::ModelViewerCameraPosition;
using blunted::ModelViewerClipFrame;
using blunted::ModelViewerClipIndex;
using blunted::ModelViewerIsRunning;
using blunted::ModelViewerSettings;
using blunted::Vector3;

namespace {

ModelViewerSettings Bench() {
  ModelViewerSettings settings;
  settings.seconds = 30.0f;
  settings.clipSeconds = 3.0f;
  settings.radius = 4.0f;
  return settings;
}

TEST(ModelViewer, IsOffUntilAskedFor) {
  ModelViewerSettings settings;
  EXPECT_FALSE(settings.IsEnabled());
  EXPECT_FALSE(ModelViewerIsRunning(settings, 0));
}

TEST(ModelViewer, RunsForItsConfiguredTime) {
  const ModelViewerSettings settings = Bench();
  EXPECT_TRUE(ModelViewerIsRunning(settings, 0));
  EXPECT_TRUE(ModelViewerIsRunning(settings, 29000));
  EXPECT_FALSE(ModelViewerIsRunning(settings, 30000));
}

TEST(ModelViewer, OrbitsTheSubjectAtTheGivenRadius) {
  const ModelViewerSettings settings = Bench();
  const Vector3 centre(1.0f, 2.0f, 0.95f);
  const Vector3 camera = ModelViewerCameraPosition(settings, centre, 0);
  const float dx = camera.coords[0] - centre.coords[0];
  const float dy = camera.coords[1] - centre.coords[1];
  EXPECT_NEAR(settings.radius, std::sqrt(dx * dx + dy * dy), 0.01f);
  // and it keeps the radius as time passes
  const Vector3 later = ModelViewerCameraPosition(settings, centre, 4000);
  const float lx = later.coords[0] - centre.coords[0];
  const float ly = later.coords[1] - centre.coords[1];
  EXPECT_NEAR(settings.radius, std::sqrt(lx * lx + ly * ly), 0.01f);
  EXPECT_GT((later - camera).GetLength(), 0.1f);  // it actually moved
}

TEST(ModelViewer, WalksThroughThePlaylistOneClipAtATime) {
  const ModelViewerSettings settings = Bench();
  EXPECT_EQ(0, ModelViewerClipIndex(settings, 0, 4));
  EXPECT_EQ(0, ModelViewerClipIndex(settings, 2900, 4));
  EXPECT_EQ(1, ModelViewerClipIndex(settings, 3100, 4));
  EXPECT_EQ(3, ModelViewerClipIndex(settings, 9500, 4));
  EXPECT_EQ(0, ModelViewerClipIndex(settings, 12500, 4));  // wraps
}

TEST(ModelViewer, ReportsNoClipForAnEmptyPlaylist) {
  EXPECT_EQ(-1, ModelViewerClipIndex(Bench(), 1000, 0));
}

TEST(ModelViewer, PlaysTheClipOnTheTenMillisecondGrid) {
  const ModelViewerSettings settings = Bench();
  EXPECT_EQ(0, ModelViewerClipFrame(settings, 0, 50));
  EXPECT_EQ(10, ModelViewerClipFrame(settings, 100, 50));
  EXPECT_EQ(0, ModelViewerClipFrame(settings, 3000, 50));  // next clip restarts
  EXPECT_EQ(0, ModelViewerClipFrame(settings, 500, 0));    // no frames, no crash
}

TEST(ModelViewer, FiltersClipsByName) {
  EXPECT_TRUE(ModelViewerAccepts("", "anything"));
  EXPECT_TRUE(ModelViewerAccepts("pes_", "pes_slide_01"));
  EXPECT_FALSE(ModelViewerAccepts("pes_", "walk_01"));
}

}  // namespace

// Where the many copies of one model stand.
//
// PES authors its crowd as one spectator and its 3D turf as one tuft, and places
// them thousands of times: each pack's audi/audiarea.bin gives the stands and the
// row spacing, and st041 alone works out to about 13,800 seats. Merged into static
// geometry that is 1.8 million vertices in a text ASE, which the loader would
// spend minutes on; drawing one mesh many times costs the GPU almost nothing.
//
// So the model stays a normal .ase and the placements sit beside it as a list, one
// line each: x, y, z and the yaw it faces. Plain text, like every other format in
// the pack, so a stand can be edited by hand.

#include <gtest/gtest.h>

#include "utils/instancelist.hpp"

TEST(InstanceList, OnePlacementPerLine) {
  const std::vector<InstanceList::Placement> places =
      InstanceList::Parse("-12.5 34.0 6.25 1.5708\n13.0 34.0 6.25 0\n");
  ASSERT_EQ(places.size(), 2u);
  EXPECT_NEAR(places[0].x, -12.5f, 0.001f);
  EXPECT_NEAR(places[0].y, 34.0f, 0.001f);
  EXPECT_NEAR(places[0].z, 6.25f, 0.001f);
  EXPECT_NEAR(places[0].yaw, 1.5708f, 0.001f);
  EXPECT_NEAR(places[1].yaw, 0.0f, 0.001f);
}

TEST(InstanceList, CommentsAndBlankLinesAreIgnored) {
  const std::vector<InstanceList::Placement> places = InstanceList::Parse(
      "# the north stand, 1.9 m rows\n\n0 0 0 0\n\n# and the south\n1 2 3 4\n");
  EXPECT_EQ(places.size(), 2u);
}

TEST(InstanceList, AYawIsOptionalBecauseATuftOfGrassHasNone) {
  const std::vector<InstanceList::Placement> places = InstanceList::Parse("1 2 3\n");
  ASSERT_EQ(places.size(), 1u);
  EXPECT_NEAR(places[0].yaw, 0.0f, 0.001f);
}

TEST(InstanceList, ARubbishLineIsSkippedRatherThanPlacedAtTheOrigin) {
  // a stand of 500 spectators is not worth losing to one bad line, and a
  // placement at 0,0,0 would put a spectator on the centre spot
  const std::vector<InstanceList::Placement> places =
      InstanceList::Parse("1 2 3 0\nover there\n4 5 6 0\n");
  ASSERT_EQ(places.size(), 2u);
  EXPECT_NEAR(places[1].x, 4.0f, 0.001f);
}

TEST(InstanceList, TooFewNumbersIsNotAPlacement) {
  EXPECT_TRUE(InstanceList::Parse("1 2\n").empty());
  EXPECT_TRUE(InstanceList::Parse("\n").empty());
  EXPECT_TRUE(InstanceList::Parse("").empty());
}

TEST(InstanceList, TheOrderIsKept) {
  // it is what decides which spectator variant stands where
  const std::vector<InstanceList::Placement> places =
      InstanceList::Parse("1 0 0 0\n2 0 0 0\n3 0 0 0\n");
  ASSERT_EQ(places.size(), 3u);
  EXPECT_NEAR(places[0].x, 1.0f, 0.001f);
  EXPECT_NEAR(places[2].x, 3.0f, 0.001f);
}

TEST(InstanceList, TheExtentCoversEveryPlacementSoNothingIsCulledEarly) {
  // the drawn geometry reaches a model's own bounds around each placement, so
  // culling has to be told how far the copies spread
  const InstanceList::Bounds bounds =
      InstanceList::Extent(InstanceList::Parse("-30 -40 2 0\n30 40 8 0\n"));
  ASSERT_TRUE(bounds.valid);
  EXPECT_NEAR(bounds.low[0], -30.0f, 0.001f);
  EXPECT_NEAR(bounds.low[1], -40.0f, 0.001f);
  EXPECT_NEAR(bounds.low[2], 2.0f, 0.001f);
  EXPECT_NEAR(bounds.high[0], 30.0f, 0.001f);
  EXPECT_NEAR(bounds.high[2], 8.0f, 0.001f);
}

TEST(InstanceList, AnEmptyListHasNoExtent) {
  const InstanceList::Bounds bounds = InstanceList::Extent(std::vector<InstanceList::Placement>());
  EXPECT_FALSE(bounds.valid);
}

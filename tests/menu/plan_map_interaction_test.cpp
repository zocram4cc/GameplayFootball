// The game plan pitch's pure logic: mapping a tactical position onto the
// portrait pitch schematic and back, and picking a card by keyboard/gamepad
// input in the absence of a mouse (docs/AGENTS.md notes gui2 has no pointer
// device - grab/move/drop is driven by directional input and an activate
// button, the same primitives Gui2WindowManager already routes to every
// other widget).
//
// Gui2PlanMap drew all eleven cards from FormationEntry::databasePosition
// directly; a team with a missing formation (see formation_fallback_test.cpp)
// piled every card on the centre spot. This module is what a rebuilt,
// drag-editable pitch needs: a coordinate mapping shared between display and
// hit-testing (so a card can never be shown in one place and picked up from
// another), and pure selection/drop math the widget can call without any
// Gui2/SDL dependency.

#include "menu/widgets/planmapinteraction.hpp"

#include <cmath>

#include <gtest/gtest.h>

using PlanMapInteraction::ClampToPitch;
using PlanMapInteraction::DatabaseToPitch;
using PlanMapInteraction::NearestCardWithinRadius;
using PlanMapInteraction::NextSelectionInDirection;
using PlanMapInteraction::PitchPoint;
using PlanMapInteraction::PitchToDatabase;
using blunted::Vector3;

namespace {

bool NearlyEqual(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

}  // namespace

TEST(PlanMapInteractionTest, KeeperMapsNearTheBottomOfThePitch) {
  const PitchPoint p = DatabaseToPitch(Vector3(-1.0f, 0.0f, 0.0f), e_PlayerRole_GK);
  EXPECT_GT(p.yPercent, 85.0f);
  EXPECT_NEAR(p.xPercent, 50.0f, 1.0f);
}

TEST(PlanMapInteractionTest, AForwardMapsNearTheTopOfThePitch) {
  const PitchPoint p = DatabaseToPitch(Vector3(1.0f, 0.0f, 0.0f), e_PlayerRole_CF);
  EXPECT_LT(p.yPercent, 15.0f);
}

TEST(PlanMapInteractionTest, WidthAxisRunsCornerToCorner) {
  const PitchPoint left = DatabaseToPitch(Vector3(0.0f, -1.0f, 0.0f), e_PlayerRole_LB);
  const PitchPoint right = DatabaseToPitch(Vector3(0.0f, 1.0f, 0.0f), e_PlayerRole_RB);
  EXPECT_LT(left.xPercent, right.xPercent);
}

// The pitch-to-database round trip is what a drop actually writes back to
// TeamData::SetFormationEntry; it must recover the exact spot the card was
// dragged to; a lossy round trip would make every second drag silently
// disagree with where the card visibly landed.
TEST(PlanMapInteractionTest, DatabaseToPitchAndBackRoundTrips) {
  for (e_PlayerRole role : {e_PlayerRole_GK, e_PlayerRole_CB, e_PlayerRole_CF}) {
    const Vector3 original(0.4f, -0.6f, 0.0f);
    const PitchPoint mapped = DatabaseToPitch(original, role);
    const Vector3 back = PitchToDatabase(mapped, role);
    EXPECT_TRUE(NearlyEqual(back.coords[0], original.coords[0]))
        << "role " << role << " x " << back.coords[0];
    EXPECT_TRUE(NearlyEqual(back.coords[1], original.coords[1]))
        << "role " << role << " y " << back.coords[1];
  }
}

TEST(PlanMapInteractionTest, ClampKeepsAPointAwayFromTheEdges) {
  const PitchPoint clamped = ClampToPitch(PitchPoint{-10.0f, 130.0f}, 5.0f);
  EXPECT_GE(clamped.xPercent, 5.0f);
  EXPECT_LE(clamped.yPercent, 95.0f);
}

TEST(PlanMapInteractionTest, ClampLeavesAnInteriorPointAlone) {
  const PitchPoint clamped = ClampToPitch(PitchPoint{50.0f, 50.0f}, 5.0f);
  EXPECT_FLOAT_EQ(clamped.xPercent, 50.0f);
  EXPECT_FLOAT_EQ(clamped.yPercent, 50.0f);
}

TEST(PlanMapInteractionTest, NearestCardFindsTheClosestOtherWithinRadius) {
  const std::vector<PitchPoint> cards = {
      {50.0f, 90.0f},  // 0: keeper
      {30.0f, 60.0f},  // 1
      {70.0f, 60.0f},  // 2
      {50.0f, 10.0f},  // 3: forward
  };
  // Dropped right on top of card 1.
  EXPECT_EQ(NearestCardWithinRadius(PitchPoint{31.0f, 61.0f}, cards, 3, 8.0f), 1);
}

TEST(PlanMapInteractionTest, NearestCardIgnoresTheCardBeingDragged) {
  const std::vector<PitchPoint> cards = {
      {50.0f, 50.0f},
      {90.0f, 90.0f},
  };
  // Card 0 is being dragged and lands almost exactly where it started, with
  // no other card nearby; it must never report itself as the drop target.
  EXPECT_EQ(NearestCardWithinRadius(PitchPoint{50.2f, 50.2f}, cards, 0, 8.0f), -1);
}

TEST(PlanMapInteractionTest, NearestCardReturnsNoneOutsideTheRadius) {
  const std::vector<PitchPoint> cards = {{50.0f, 50.0f}, {90.0f, 90.0f}};
  EXPECT_EQ(NearestCardWithinRadius(PitchPoint{10.0f, 10.0f}, cards, -1, 5.0f), -1);
}

TEST(PlanMapInteractionTest, DirectionalSelectionMovesToTheRightNeighbour) {
  // A back four, roughly in a row.
  const std::vector<PitchPoint> cards = {
      {20.0f, 80.0f}, {40.0f, 80.0f}, {60.0f, 80.0f}, {80.0f, 80.0f},
  };
  EXPECT_EQ(NextSelectionInDirection(cards, 0, Vector3(1, 0, 0)), 1);
  EXPECT_EQ(NextSelectionInDirection(cards, 3, Vector3(-1, 0, 0)), 2);
}

TEST(PlanMapInteractionTest, DirectionalSelectionMovesUpTheFormationLines) {
  const std::vector<PitchPoint> cards = {
      {50.0f, 90.0f},  // GK
      {50.0f, 50.0f},  // midfield
      {50.0f, 10.0f},  // forward
  };
  EXPECT_EQ(NextSelectionInDirection(cards, 0, Vector3(0, -1, 0)), 1);
  EXPECT_EQ(NextSelectionInDirection(cards, 1, Vector3(0, -1, 0)), 2);
}

TEST(PlanMapInteractionTest, DirectionalSelectionStaysPutWithNoCandidate) {
  const std::vector<PitchPoint> cards = {{50.0f, 90.0f}, {50.0f, 50.0f}};
  // Nothing sits further down than the keeper.
  EXPECT_EQ(NextSelectionInDirection(cards, 0, Vector3(0, 1, 0)), 0);
}

// --- the preview follows the formation --------------------------------------
//
// "When I select a formation via the menu, the shape of the formation on the
// preview should change accordingly" (owner, 04-09). GamePlanPage::
// ApplyFormationShape writes Formations' layout into TeamData and refreshes the
// map, which reads every card's place through DatabaseToPitch - so the contract
// is that two different shapes map to two different sets of pitch points, with
// the right number of cards on each line.

#include "data/formations.hpp"

namespace {

// How many cards land on each third of the schematic, back to front.
std::array<int, 3> LinesOf(const Formations::Shape& shape) {
  std::array<int, 3> lines = {0, 0, 0};
  for (const Formations::Slot& slot : Formations::GetLayoutForShape(shape)) {
    if (slot.role == e_PlayerRole_GK) continue;
    const PlanMapInteraction::PitchPoint point =
        PlanMapInteraction::DatabaseToPitch(slot.position, slot.role);
    // The schematic is portrait with the goal at the bottom: a defender's y is
    // large, a forward's small.
    if (point.yPercent > 62.0f)
      lines[0]++;
    else if (point.yPercent > 42.0f)
      lines[1]++;
    else
      lines[2]++;
  }
  return lines;
}

}  // namespace

TEST(PlanMapPreviewTest, ADifferentShapePutsADifferentNumberOfCardsOnEachLine) {
  const std::array<int, 3> flat442 = LinesOf(Formations::MakeShapeClamped(4, 4, 2));
  const std::array<int, 3> three52 = LinesOf(Formations::MakeShapeClamped(3, 5, 2));
  EXPECT_EQ(flat442[0], 4);
  EXPECT_EQ(three52[0], 3);
  EXPECT_NE(flat442, three52) << "the preview would look identical for both shapes";
}

TEST(PlanMapPreviewTest, EveryOutfieldCardMovesWhenTheLineChanges) {
  // Not just the count: the back line's cards sit at different depths, which is
  // what makes the change visible rather than a relabelling.
  const std::vector<Formations::Slot> back4 =
      Formations::GetLayoutForShape(Formations::MakeShapeClamped(4, 4, 2));
  const std::vector<Formations::Slot> back3 =
      Formations::GetLayoutForShape(Formations::MakeShapeClamped(3, 4, 3));
  ASSERT_EQ(back4.size(), back3.size());
  int moved = 0;
  for (size_t i = 0; i < back4.size(); i++) {
    const PlanMapInteraction::PitchPoint a =
        PlanMapInteraction::DatabaseToPitch(back4.at(i).position, back4.at(i).role);
    const PlanMapInteraction::PitchPoint b =
        PlanMapInteraction::DatabaseToPitch(back3.at(i).position, back3.at(i).role);
    if (std::fabs(a.xPercent - b.xPercent) > 0.5f || std::fabs(a.yPercent - b.yPercent) > 0.5f)
      moved++;
  }
  EXPECT_GE(moved, 4) << "only " << moved << " card(s) moved between 4-4-2 and 3-4-3";
}

// What a hanging surface has to do.
//
// The stadium's cloth was authored rigid and moved by hand: the netting was pulled
// at the ball on the frame it touched and put back on the frame it stopped, and the
// corner flags and banners never moved at all. These are the properties that make
// the difference - weight, a length that holds, and motion that dies down.

#include <cmath>

#include <gtest/gtest.h>

#include "onthepitch/ballphysics.hpp"
#include "utils/cloth.hpp"

using blunted::Cloth;
using blunted::Vector3;

namespace {

// A pin with one free point hanging a metre below it. This one is already at
// equilibrium - the link is vertical and at its rest length - so it is what to use
// when the question is whether something stays still.
Cloth Pendulum(float length = 1.0f) {
  Cloth cloth;
  std::vector<Vector3> rest = {Vector3(0, 0, 2), Vector3(0, 0, 2 - length)};
  std::vector<bool> fixed = {true, false};
  cloth.Build(rest, fixed, {{0, 1}});
  return cloth;
}

// The same pin, with the point held out sideways: released, it has somewhere to go.
Cloth RaisedPendulum() {
  Cloth cloth;
  cloth.Build({Vector3(0, 0, 2), Vector3(1, 0, 2)}, {true, false}, {{0, 1}});
  return cloth;
}

const Vector3 kGravity(0, 0, -9.81f);

void Settle(Cloth& cloth, int steps, float dt = 0.02f, float damping = 0.9f) {
  for (int i = 0; i < steps; i++) cloth.Step(dt, kGravity, damping, 4);
}

}  // namespace

TEST(Cloth, AFixedPointNeverMoves) {
  Cloth cloth = Pendulum();
  Settle(cloth, 200);
  EXPECT_FLOAT_EQ(cloth.Positions()[0].coords[2], 2.0f);
}

TEST(Cloth, AFreePointFalls) {
  Cloth cloth;
  cloth.Build({Vector3(0, 0, 2)}, {false}, {});
  cloth.Step(0.02f, kGravity, 1.0f, 1);
  EXPECT_LT(cloth.Positions()[0].coords[2], 2.0f);
}

TEST(Cloth, ALinkHoldsItsLength) {
  Cloth cloth = Pendulum();
  Settle(cloth, 400);
  const float length = (cloth.Positions()[1] - cloth.Positions()[0]).GetLength();
  EXPECT_NEAR(length, 1.0f, 0.02f);
}

TEST(Cloth, ItHangsBelowItsPin) {
  // Started out sideways, it should come to rest under the pin rather than stay put.
  Cloth cloth;
  cloth.Build({Vector3(0, 0, 2), Vector3(1, 0, 2)}, {true, false}, {{0, 1}});
  Settle(cloth, 1200);
  EXPECT_NEAR(cloth.Positions()[1].coords[2], 1.0f, 0.1f);
  EXPECT_NEAR(cloth.Positions()[1].coords[0], 0.0f, 0.15f);
}

TEST(Cloth, MotionDiesDown) {
  Cloth cloth = RaisedPendulum();
  Settle(cloth, 5);
  const Vector3 a = cloth.Positions()[1];
  cloth.Step(0.02f, kGravity, 0.9f, 4);
  const float speedEarly = (cloth.Positions()[1] - a).GetLength();
  ASSERT_GT(speedEarly, 0.0f);
  Settle(cloth, 2000);
  const Vector3 b = cloth.Positions()[1];
  cloth.Step(0.02f, kGravity, 0.9f, 4);
  const float speedLate = (cloth.Positions()[1] - b).GetLength();
  EXPECT_LT(speedLate, speedEarly);
}

TEST(Cloth, APendulumAtEquilibriumStaysPut) {
  // Hanging straight down at its rest length, there is nowhere for it to go, and a
  // solver that jitters it would have every flag in the stadium shivering.
  Cloth cloth = Pendulum();
  Settle(cloth, 200);
  EXPECT_FLOAT_EQ(cloth.Displacement(), 0.0f);
}

TEST(Cloth, AWholeSheetStaysFinite) {
  // A 15x15 panel pinned all round, run for twenty seconds. The netting is this
  // shape, and an unstable solver on it puts geometry through the stands.
  std::vector<Vector3> rest;
  std::vector<bool> fixed;
  std::vector<int> indices;
  const int n = 15;
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      rest.push_back(Vector3(x * 0.2f, 0, 3.0f - y * 0.2f));
      fixed.push_back(x == 0 || y == 0 || x == n - 1 || y == n - 1);
    }
  }
  for (int y = 0; y < n - 1; y++) {
    for (int x = 0; x < n - 1; x++) {
      const int i = y * n + x;
      indices.insert(indices.end(), {i, i + 1, i + n});
      indices.insert(indices.end(), {i + 1, i + n + 1, i + n});
    }
  }
  Cloth cloth;
  cloth.Build(rest, fixed, blunted::LinksFromTriangles(indices));
  for (int i = 0; i < 1000; i++) cloth.Step(0.02f, kGravity, 0.95f, 3);
  for (const Vector3& p : cloth.Positions()) {
    ASSERT_TRUE(std::isfinite(p.coords[0]));
    ASSERT_TRUE(std::isfinite(p.coords[1]));
    ASSERT_TRUE(std::isfinite(p.coords[2]));
    EXPECT_LT(std::fabs(p.coords[0]), 20.0f);
    EXPECT_LT(std::fabs(p.coords[2]), 20.0f);
  }
  // And it sagged: a sheet pinned at its border bows away from its rest plane.
  EXPECT_GT(cloth.Displacement(), 0.005f);
}

TEST(Cloth, AHugeStepIsClamped) {
  // A pause or a stall hands over a dt of whole seconds. Verlet squares it.
  Cloth a = Pendulum();
  Cloth b = Pendulum();
  a.Step(2.0f, kGravity, 0.9f, 4);
  b.Step(Cloth::kMaxStep_s, kGravity, 0.9f, 4);
  EXPECT_NEAR(a.Positions()[1].coords[2], b.Positions()[1].coords[2], 1e-5f);
}

TEST(Cloth, PushMovesPointsOutOfTheBall) {
  Cloth cloth;
  cloth.Build({Vector3(0, 0, 1)}, {false}, {});
  cloth.Push(Vector3(0, 0.1f, 1), 0.5f);
  const Vector3& p = cloth.Positions()[0];
  EXPECT_NEAR((p - Vector3(0, 0.1f, 1)).GetLength(), 0.5f, 1e-4f);
  // Pushed away from the centre, not through it.
  EXPECT_LT(p.coords[1], 0.1f);
}

// A ball centred exactly between two of the imported net's own points misses
// both of them at the old push radius (the ball's own physical size, 0.11m),
// which is what made contact look like it did nothing to the net: the mesh
// point nearest the ball was routinely farther away than that. The imported
// net settles to about one point every 0.35m (PrepareGoalNetting logs "553
// net point(s)" per goal over its ~63 sq. m of side/rear/top netting), so the
// worst-case gap from a ball sitting between two of them is half that, 0.175m.
TEST(Cloth, TheOldBallRadiusMissesARealisticallySpacedNetPoint) {
  Cloth cloth;
  cloth.Build({Vector3(0, -0.175f, 0), Vector3(0, 0.175f, 0)}, {false, false}, {});
  cloth.Push(Vector3(0, 0, 0), 0.11f);
  EXPECT_FLOAT_EQ(cloth.Positions()[0].coords[1], -0.175f);
  EXPECT_FLOAT_EQ(cloth.Positions()[1].coords[1], 0.175f);
}

TEST(Cloth, TheFixedPushRadiusReachesARealisticallySpacedNetPoint) {
  Cloth cloth;
  cloth.Build({Vector3(0, -0.175f, 0), Vector3(0, 0.175f, 0)}, {false, false}, {});
  cloth.Push(Vector3(0, 0, 0), kNettingPushRadius_m);
  EXPECT_NE(cloth.Positions()[0].coords[1], -0.175f);
  EXPECT_NE(cloth.Positions()[1].coords[1], 0.175f);
}

TEST(Cloth, PushLeavesFixedPointsAlone) {
  Cloth cloth;
  cloth.Build({Vector3(0, 0, 1)}, {true}, {});
  cloth.Push(Vector3(0, 0, 1), 0.5f);
  EXPECT_FLOAT_EQ(cloth.Positions()[0].coords[2], 1.0f);
}

TEST(Cloth, PushIgnoresPointsOutsideTheBall) {
  Cloth cloth;
  cloth.Build({Vector3(0, 0, 1)}, {false}, {});
  cloth.Push(Vector3(0, 4, 1), 0.5f);
  EXPECT_FLOAT_EQ(cloth.Positions()[0].coords[1], 0.0f);
}

TEST(Cloth, ResetPutsItBack) {
  Cloth cloth = RaisedPendulum();
  Settle(cloth, 100);
  ASSERT_GT(cloth.Displacement(), 0.0f);
  cloth.Reset();
  EXPECT_FLOAT_EQ(cloth.Displacement(), 0.0f);
  EXPECT_FLOAT_EQ(cloth.Positions()[1].coords[0], 1.0f);
  EXPECT_FLOAT_EQ(cloth.Positions()[1].coords[2], 2.0f);
}

TEST(Cloth, SpeedFallsToNothing) {
  // What lets a settled surface sleep. A net that has taken up its sag stays far
  // from its authored pose, so only its speed can say it has stopped.
  Cloth cloth = RaisedPendulum();
  Settle(cloth, 5);
  ASSERT_GT(cloth.Speed(), 0.001f);
  Settle(cloth, 4000);
  EXPECT_LT(cloth.Speed(), 0.0005f);
  EXPECT_GT(cloth.Displacement(), 0.5f);
}

TEST(Cloth, SpeedIgnoresFixedPoints) {
  Cloth cloth;
  cloth.Build({Vector3(0, 0, 2)}, {true}, {});
  cloth.Step(0.02f, kGravity, 0.9f, 1);
  EXPECT_FLOAT_EQ(cloth.Speed(), 0.0f);
}

TEST(Cloth, LinksComeFromEveryEdgeOnce) {
  // Two triangles sharing an edge: five distinct edges, not six.
  const std::vector<int> indices = {0, 1, 2, 1, 3, 2};
  EXPECT_EQ(blunted::LinksFromTriangles(indices).size(), 5u);
}

TEST(Cloth, DegenerateLinksAreDropped) {
  Cloth cloth;
  // A zero-length link has no direction to correct along.
  cloth.Build({Vector3(0, 0, 1), Vector3(0, 0, 1)}, {true, false}, {{0, 1}, {1, 1}});
  cloth.Step(0.02f, kGravity, 0.9f, 4);
  EXPECT_TRUE(std::isfinite(cloth.Positions()[1].coords[2]));
}

TEST(Cloth, TheBorderIsWhatOneTriangleOwns) {
  // Two triangles making a square: every vertex is on the border.
  const std::vector<Vector3> rest = {Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(0, 0, 1),
                                     Vector3(1, 0, 1)};
  const std::vector<int> indices = {0, 1, 2, 1, 3, 2};
  const std::vector<bool> border = blunted::BorderVertices(rest, indices);
  for (bool b : border) EXPECT_TRUE(b);
}

TEST(Cloth, AnInteriorVertexIsNotOnTheBorder) {
  // A fan of four triangles around a centre point: the centre is interior.
  const std::vector<Vector3> rest = {Vector3(0, 0, 0),   Vector3(-1, 0, -1), Vector3(1, 0, -1),
                                     Vector3(1, 0, 1),   Vector3(-1, 0, 1)};
  const std::vector<int> indices = {0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1};
  const std::vector<bool> border = blunted::BorderVertices(rest, indices);
  EXPECT_FALSE(border[0]);
  EXPECT_TRUE(border[1]);
}

TEST(Cloth, WeldedVerticesShareABorder) {
  // Imported meshes are unwelded at their UV seams: the same position appears as
  // several vertices, and an edge shared in space then looks unshared per index.
  // Read that way a net panel is border all the way through and cannot move at all.
  //
  // The fan of four triangles again, with its centre split into one copy per
  // triangle - which is what a seam through the middle of a panel looks like.
  const std::vector<Vector3> rest = {
      Vector3(0, 0, 0),  Vector3(0, 0, 0), Vector3(0, 0, 0),  Vector3(0, 0, 0),
      Vector3(-1, 0, -1), Vector3(1, 0, -1), Vector3(1, 0, 1), Vector3(-1, 0, 1)};
  const std::vector<int> indices = {0, 4, 5, 1, 5, 6, 2, 6, 7, 3, 7, 4};
  const std::vector<bool> border = blunted::BorderVertices(rest, indices);
  // Every copy of the centre is interior, because every edge it is on is shared
  // with the neighbouring triangle once welded.
  EXPECT_FALSE(border[0]);
  EXPECT_FALSE(border[1]);
  EXPECT_FALSE(border[2]);
  EXPECT_FALSE(border[3]);
  // The ring is the outside edge.
  EXPECT_TRUE(border[4]);
  EXPECT_TRUE(border[7]);
}

TEST(Cloth, PlanesPickTheirOwnAxis) {
  const std::vector<Vector3> rest = {Vector3(-55.05f, 0, 1), Vector3(-57.55f, 0, 1),
                                     Vector3(-55.06f, 0, 0)};
  const std::vector<bool> mouth = blunted::VerticesOnPlane(rest, 0, -55.05f, 0.02f);
  EXPECT_TRUE(mouth[0]);
  EXPECT_FALSE(mouth[1]);
  EXPECT_TRUE(mouth[2]);
  const std::vector<bool> ground = blunted::VerticesOnPlane(rest, 2, 0.0f, 0.02f);
  EXPECT_FALSE(ground[0]);
  EXPECT_TRUE(ground[2]);
}

TEST(Cloth, APlaneOnNoAxisHoldsNothing) {
  const std::vector<Vector3> rest = {Vector3(0, 0, 0)};
  EXPECT_FALSE(blunted::VerticesOnPlane(rest, 7, 0.0f, 1.0f)[0]);
}

TEST(Cloth, AttachmentsAddUp) {
  std::vector<bool> a = {true, false, false};
  blunted::UnionInto(a, {false, true, false});
  EXPECT_TRUE(a[0]);
  EXPECT_TRUE(a[1]);
  EXPECT_FALSE(a[2]);
}

TEST(Cloth, AnEdgeIsWhereTwoPlanesMeet) {
  // The net's rear top edge: on the back plane and on the top one, not just either.
  const std::vector<bool> back = {true, true, false};
  const std::vector<bool> top = {true, false, true};
  const std::vector<bool> edge = blunted::Both(back, top);
  EXPECT_TRUE(edge[0]);
  EXPECT_FALSE(edge[1]);
  EXPECT_FALSE(edge[2]);
}

TEST(Cloth, VerticesNearAnAxisAreHeld) {
  const std::vector<Vector3> rest = {Vector3(0, 0, 0.5f), Vector3(0.01f, 0, 1.5f),
                                     Vector3(0.6f, 0, 1.4f)};
  const std::vector<bool> held =
      blunted::VerticesNearAxis(rest, Vector3(0, 0, 0), Vector3(0, 0, 1), 0.05f);
  EXPECT_TRUE(held[0]);
  EXPECT_TRUE(held[1]);
  EXPECT_FALSE(held[2]);
}

TEST(Cloth, AFlagHangsFromItsPole) {
  // A corner flag is one prop: a pole on the axis, a disc round its foot and two
  // panels hanging off one side. Pinning what is near the axis holds the pole and
  // the disc rigid and leaves the cloth free, without knowing which mesh is which.
  std::vector<Vector3> rest;
  std::vector<bool> fixed;
  // pole: a column of points on the axis
  for (int i = 0; i <= 8; i++) rest.push_back(Vector3(0, 0, i * 0.2f));
  // the panel: a 3x3 grid hanging out to -x from the top of the pole
  for (int row = 0; row < 3; row++)
    for (int col = 0; col < 3; col++)
      rest.push_back(Vector3(-col * 0.2f, 0, 1.6f - row * 0.15f));
  fixed = blunted::VerticesNearAxis(rest, Vector3(0, 0, 0), Vector3(0, 0, 1), 0.12f);
  // The pole is held, and so is the panel's attached edge; the outer corners are not.
  for (int i = 0; i <= 8; i++) EXPECT_TRUE(fixed[i]) << "pole point " << i;
  EXPECT_TRUE(fixed[9]);
  EXPECT_FALSE(fixed[11]);

  std::vector<std::pair<int, int>> links;
  for (int i = 0; i < 8; i++) links.push_back({i, i + 1});
  for (int row = 0; row < 3; row++)
    for (int col = 0; col < 3; col++) {
      const int at = 9 + row * 3 + col;
      if (col < 2) links.push_back({at, at + 1});
      if (row < 2) links.push_back({at, at + 3});
    }
  Cloth cloth;
  cloth.Build(rest, fixed, links);
  const Vector3 tipWas = cloth.Positions()[9 + 2];
  for (int i = 0; i < 400; i++) cloth.Step(0.02f, kGravity, 0.94f, 3);
  // The pole did not move and the free corner of the flag dropped.
  EXPECT_FLOAT_EQ(cloth.Positions()[8].coords[2], 1.6f);
  EXPECT_LT(cloth.Positions()[9 + 2].coords[2], tipWas.coords[2]);
}

TEST(Cloth, WindPushesAFlagOut) {
  Cloth still;
  Cloth blown;
  const std::vector<Vector3> rest = {Vector3(0, 0, 1.6f), Vector3(-0.3f, 0, 1.6f)};
  const std::vector<bool> fixed = {true, false};
  still.Build(rest, fixed, {{0, 1}});
  blown.Build(rest, fixed, {{0, 1}});
  for (int i = 0; i < 200; i++) {
    still.Step(0.02f, kGravity, 0.94f, 3);
    blown.Step(0.02f, kGravity + Vector3(0, 14.0f, 0), 0.94f, 3);
  }
  // Blown sideways, the free end swings out of the plane it hung in.
  EXPECT_NEAR(still.Positions()[1].coords[1], 0.0f, 1e-3f);
  EXPECT_GT(blown.Positions()[1].coords[1], 0.05f);
}

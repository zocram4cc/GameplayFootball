// The coarse body copy: vertex clustering must keep what spans cells, drop what
// collapses inside one, and hand every kept vertex its own source so the skin
// weights follow it.

#include <gtest/gtest.h>

#include <vector>

#include "onthepitch/player/humanoid/skinning.hpp"

namespace {

// `count` vertices along x, `spacing` apart, five elements: position, then a
// normal/uv/tangent/bitangent that each carry the vertex index so a kept vertex
// can be traced back.
std::vector<float> Row(int count, float spacing) {
  std::vector<float> v(count * 3 * 5, 0.0f);
  for (int i = 0; i < count; i++) {
    v[i * 3] = i * spacing;
    for (int e = 1; e < 5; e++) v[e * count * 3 + i * 3] = 100.0f * e + i;
  }
  return v;
}

}  // namespace

TEST(ClusterDecimate, KeepsATriangleThatSpansThreeCells) {
  // Three vertices a metre apart on a 2 cm grid: nothing merges.
  std::vector<float> v = Row(3, 1.0f);
  std::vector<unsigned int> tri = {0, 1, 2};
  Skinning::ClusteredMesh out = Skinning::ClusterDecimate(v.data(), 3, 5, tri, 0.02f);
  EXPECT_EQ(out.vertexCount(), 3);
  ASSERT_EQ(out.indices.size(), 3u);
  // every attribute of every kept vertex is the source's, in the same layout
  for (int i = 0; i < 3; i++) {
    const int src = out.sourceVertex[i];
    EXPECT_FLOAT_EQ(out.vertices[i * 3], v[src * 3]);
    for (int e = 1; e < 5; e++)
      EXPECT_FLOAT_EQ(out.vertices[e * 3 * 3 + i * 3], v[e * 3 * 3 + src * 3]);
  }
}

TEST(ClusterDecimate, DropsATriangleInsideOneCell) {
  // Three vertices a millimetre apart collapse into one cell: no triangle, and
  // therefore no vertex either.
  std::vector<float> v = Row(3, 0.001f);
  std::vector<unsigned int> tri = {0, 1, 2};
  Skinning::ClusteredMesh out = Skinning::ClusterDecimate(v.data(), 3, 5, tri, 0.02f);
  EXPECT_EQ(out.vertexCount(), 0);
  EXPECT_TRUE(out.indices.empty());
  EXPECT_TRUE(out.vertices.empty());
}

TEST(ClusterDecimate, MergesNeighboursAndRemapsIndices) {
  // Six vertices at 0, 1 mm, 1 m, 1.001 m, 2 m, 2.001 m: three cells. Two
  // triangles that each touch all three cells survive on three shared vertices.
  std::vector<float> v(6 * 3 * 5, 0.0f);
  const float x[6] = {0.0f, 0.001f, 1.0f, 1.001f, 2.0f, 2.001f};
  for (int i = 0; i < 6; i++) v[i * 3] = x[i];
  std::vector<unsigned int> tris = {0, 2, 4, 1, 3, 5};
  Skinning::ClusteredMesh out = Skinning::ClusterDecimate(v.data(), 6, 5, tris, 0.02f);
  EXPECT_EQ(out.vertexCount(), 3);
  ASSERT_EQ(out.indices.size(), 6u);
  for (unsigned int index : out.indices) EXPECT_LT(index, 3u);
  // the first vertex seen in a cell stands for it
  EXPECT_EQ(out.sourceVertex[0], 0);
  EXPECT_EQ(out.sourceVertex[1], 2);
  EXPECT_EQ(out.sourceVertex[2], 4);
  // and the second triangle is the same three cells, in the same order
  EXPECT_EQ(out.indices[3], out.indices[0]);
  EXPECT_EQ(out.indices[4], out.indices[1]);
  EXPECT_EQ(out.indices[5], out.indices[2]);
}

TEST(ClusterDecimate, NegativeCoordinatesGetTheirOwnCells) {
  // -1 cm and +1 cm are different cells; a floor that truncated toward zero
  // would put them in the same one.
  std::vector<float> v(3 * 3 * 5, 0.0f);
  v[0] = -0.01f;
  v[3] = 0.01f;
  v[6] = 1.0f;
  std::vector<unsigned int> tri = {0, 1, 2};
  Skinning::ClusteredMesh out = Skinning::ClusterDecimate(v.data(), 3, 5, tri, 0.02f);
  EXPECT_EQ(out.vertexCount(), 3);
}

TEST(UseBodyLod, ThresholdWithHysteresis) {
  EXPECT_FALSE(Skinning::UseBodyLod(29.0f, 30.0f, false));
  EXPECT_TRUE(Skinning::UseBodyLod(31.0f, 30.0f, false));
  // once coarse, it stays coarse until two metres nearer
  EXPECT_TRUE(Skinning::UseBodyLod(29.0f, 30.0f, true));
  EXPECT_FALSE(Skinning::UseBodyLod(27.0f, 30.0f, true));
  // zero disables it whatever the distance
  EXPECT_FALSE(Skinning::UseBodyLod(500.0f, 0.0f, false));
}

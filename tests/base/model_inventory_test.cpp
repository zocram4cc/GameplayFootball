// What a model is made of, so a viewer can show it and an import can be judged.
//
// Troubleshooting a model through a running match is the wrong tool: it loads a
// stadium, a crowd, 22 players and a presentation before you can look at one mesh.
// This is the part a standalone viewer needs and a match cannot give - a per-mesh
// account of what arrived.
//
// It exists because a ton of imported models are missing visible geometry.
// strip_stretched_tris uses an absolute --max-edge, 0.15 m by default, and over the
// 90 imported player models 44 have their longest surviving edge sitting exactly on
// that cut, nine of them coarse meshes where 0.15 m is only 1.6x to 3.6x their median
// edge. lcg_2715's median edge is 9.5 cm. The shards the cut was written for were
// 1.25 m against a 1.9 cm median - 65x it.
//
// So the inventory reports, per mesh: how much geometry it has, whether anything is
// orphaned, how its edge lengths sit against a threshold, and whether it duplicates
// another mesh - a stray shell.

#include <gtest/gtest.h>

#include "utils/modelinventory.hpp"

namespace ModelInventory = blunted::ModelInventory;

namespace {

ModelInventory::Mesh Quad(float size, const std::string& name) {
  ModelInventory::Mesh mesh;
  mesh.name = name;
  mesh.vertices = {{0, 0, 0}, {size, 0, 0}, {size, 0, size}, {0, 0, size}};
  mesh.faces = {{0, 1, 2}, {0, 2, 3}};
  return mesh;
}

}  // namespace

TEST(ModelInventory, ItCountsWhatAMeshHas) {
  const auto report = ModelInventory::Describe({Quad(0.1f, "body")});
  ASSERT_EQ(report.meshes.size(), 1u);
  EXPECT_EQ(report.meshes[0].vertices, 4);
  EXPECT_EQ(report.meshes[0].faces, 2);
  EXPECT_EQ(report.meshes[0].name, "body");
}

TEST(ModelInventory, AnEmptyMeshIsWorthSayingOutLoud) {
  // a mesh that arrived with no faces is geometry that went missing, not a mesh
  ModelInventory::Mesh hollow;
  hollow.name = "cape";
  hollow.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}};
  const auto report = ModelInventory::Describe({hollow});
  EXPECT_TRUE(report.meshes[0].empty);
  EXPECT_EQ(report.emptyMeshes, 1);
}

TEST(ModelInventory, AMeshWithFacesIsNotEmpty) {
  const auto report = ModelInventory::Describe({Quad(0.1f, "body")});
  EXPECT_FALSE(report.meshes[0].empty);
  EXPECT_EQ(report.emptyMeshes, 0);
}

TEST(ModelInventory, ItFindsTheVerticesNoFaceUses) {
  ModelInventory::Mesh mesh = Quad(0.1f, "body");
  mesh.vertices.push_back({9, 9, 9});  // nothing references it
  const auto report = ModelInventory::Describe({mesh});
  EXPECT_EQ(report.meshes[0].orphanVertices, 1);
}

// The measurement that matters for the missing geometry: where a mesh's own edges sit
// relative to the threshold that was applied to it.
TEST(ModelInventory, ItReportsTheMedianAndLongestEdge) {
  const auto report = ModelInventory::Describe({Quad(0.2f, "body")});
  // a 0.2 quad split into two triangles: edges are 0.2, 0.2 and the 0.283 diagonal
  EXPECT_NEAR(report.meshes[0].medianEdge, 0.2f, 1e-3);
  EXPECT_NEAR(report.meshes[0].longestEdge, 0.2f * 1.41421f, 1e-3);
}

TEST(ModelInventory, ACoarseMeshIsFlaggedAgainstAnAbsoluteCut) {
  // lcg_2715: a 9.5 cm median edge under a 15 cm cut, which is 1.6x it. Anything
  // under this ratio cannot lose only outliers.
  const auto report = ModelInventory::Describe({Quad(0.095f, "coarse")}, 0.15f);
  EXPECT_LT(report.meshes[0].cutRatio, 4.0f);
  EXPECT_TRUE(report.meshes[0].tooCoarseForCut);
}

TEST(ModelInventory, AFineMeshIsNotFlagged) {
  // the shards the cut was written for were 65x the median
  const auto report = ModelInventory::Describe({Quad(0.002f, "fine")}, 0.15f);
  EXPECT_GT(report.meshes[0].cutRatio, 20.0f);
  EXPECT_FALSE(report.meshes[0].tooCoarseForCut);
}

// Stray shells: the same geometry twice over, which a viewer has to be able to show
// and an import ought not to produce.
TEST(ModelInventory, ItSpotsADuplicatedShell) {
  const auto report = ModelInventory::Describe({Quad(0.1f, "body"), Quad(0.1f, "body_shell")});
  EXPECT_EQ(report.duplicateMeshes, 1);
  EXPECT_TRUE(report.meshes[1].duplicateOf == "body");
}

TEST(ModelInventory, DifferentGeometryIsNotADuplicate) {
  const auto report = ModelInventory::Describe({Quad(0.1f, "body"), Quad(0.3f, "cape")});
  EXPECT_EQ(report.duplicateMeshes, 0);
  EXPECT_TRUE(report.meshes[1].duplicateOf.empty());
}

TEST(ModelInventory, TheWholeModelIsSummedUp) {
  const auto report = ModelInventory::Describe({Quad(0.1f, "a"), Quad(0.2f, "b")});
  EXPECT_EQ(report.totalVertices, 8);
  EXPECT_EQ(report.totalFaces, 4);
  EXPECT_EQ(report.meshes.size(), 2u);
}

TEST(ModelInventory, NothingIsAnEmptyReportRatherThanACrash) {
  const auto report = ModelInventory::Describe({});
  EXPECT_EQ(report.totalVertices, 0);
  EXPECT_TRUE(report.meshes.empty());
}

// Reading positions back out of the engine's own vertex buffer.
//
// The buffer is element-major: every position first, then every normal, then the UVs
// and the tangent pair - GetTriangleMeshAABB walks it as vertices[t * 9 + v * 3 + i]
// and sizes it as verticesDataSize / GetTriangleMeshElementCount() / 3. The viewer
// instead strode the whole buffer by the element *count*, so every position after the
// first was read out of the middle of another attribute: hdg_2402 came back with a
// median edge of 1.49 m, on a body 1.6 m tall.
namespace {

// Two triangles' worth of positions followed by three more attribute blocks, so the
// element count is 4 and only the first quarter is geometry.
std::vector<float> ElementMajorBuffer() {
  std::vector<float> positions = {
      0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 1.0f,  0.0f, 1.0f, 1.0f};
  std::vector<float> buffer = positions;
  for (int block = 1; block < 4; block++)
    for (size_t i = 0; i < positions.size(); i++)
      buffer.push_back(-99.0f);  // anything but a position
  return buffer;
}

}  // namespace

TEST(ReadPositions, TakesOnlyTheFirstBlock) {
  const std::vector<float> buffer = ElementMajorBuffer();
  const std::vector<std::array<float, 3>> got =
      blunted::ModelInventory::ReadPositions(buffer.data(), (int)buffer.size(), 4);
  ASSERT_EQ(got.size(), 6u);
  EXPECT_FLOAT_EQ(got[1][0], 1.0f);
  EXPECT_FLOAT_EQ(got[5][2], 1.0f);
  for (const auto& position : got)
    for (int i = 0; i < 3; i++)
      EXPECT_GT(position[i], -1.0f) << "read past the position block";
}

TEST(ReadPositions, CountsVerticesNotFloats) {
  const std::vector<float> buffer = ElementMajorBuffer();
  EXPECT_EQ(blunted::ModelInventory::ReadPositions(buffer.data(), (int)buffer.size(), 4).size(),
            buffer.size() / 4 / 3);
}

TEST(ReadPositions, AnEmptyOrBrokenBufferReadsNothing) {
  const float one = 1.0f;
  EXPECT_TRUE(blunted::ModelInventory::ReadPositions(nullptr, 12, 4).empty());
  EXPECT_TRUE(blunted::ModelInventory::ReadPositions(&one, 0, 4).empty());
  EXPECT_TRUE(blunted::ModelInventory::ReadPositions(&one, 12, 0).empty());
}

TEST(ReadPositions, AMeshOfOneTriangleKeepsItsEdgeLengths) {
  // the whole point: a metre-wide triangle must not measure 1.49 m per edge
  const std::vector<float> buffer = ElementMajorBuffer();
  std::vector<blunted::ModelInventory::Mesh> meshes(1);
  meshes[0].name = "tri";
  meshes[0].vertices = blunted::ModelInventory::ReadPositions(buffer.data(), (int)buffer.size(), 4);
  meshes[0].faces = {{0, 1, 2}, {3, 4, 5}};
  const blunted::ModelInventory::Report report = blunted::ModelInventory::Describe(meshes, 0.0f);
  EXPECT_NEAR(report.meshes[0].medianEdge, 1.0f, 0.5f);
}

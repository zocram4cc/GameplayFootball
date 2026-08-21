#include "utils/modelinventory.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace blunted {
namespace ModelInventory {

namespace {

float Distance(const std::array<float, 3>& a, const std::array<float, 3>& b) {
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A mesh's shape, for spotting the same geometry twice: its vertex count, its face
// count and its bounding box. Cheap, and enough - a stray shell is a copy, not a
// coincidence.
std::string Signature(const Mesh& mesh) {
  if (mesh.vertices.empty()) return "";
  float lo[3] = {mesh.vertices[0][0], mesh.vertices[0][1], mesh.vertices[0][2]};
  float hi[3] = {lo[0], lo[1], lo[2]};
  for (const auto& v : mesh.vertices) {
    for (int c = 0; c < 3; c++) {
      lo[c] = std::min(lo[c], v[c]);
      hi[c] = std::max(hi[c], v[c]);
    }
  }
  char buffer[160];
  snprintf(buffer, sizeof buffer, "%zu:%zu:%.4f:%.4f:%.4f:%.4f:%.4f:%.4f",
           mesh.vertices.size(), mesh.faces.size(), lo[0], lo[1], lo[2], hi[0], hi[1],
           hi[2]);
  return buffer;
}

}  // namespace

Report Describe(const std::vector<Mesh>& meshes, float edgeCut) {
  Report report;
  std::vector<std::pair<std::string, std::string>> seen;  // signature -> first name

  for (const Mesh& mesh : meshes) {
    MeshReport out;
    out.name = mesh.name;
    out.vertices = static_cast<int>(mesh.vertices.size());
    out.faces = static_cast<int>(mesh.faces.size());
    out.empty = !mesh.vertices.empty() && mesh.faces.empty();

    std::set<int> used;
    std::vector<float> edges;
    for (const auto& face : mesh.faces) {
      bool valid = true;
      for (int c = 0; c < 3; c++)
        if (face[c] < 0 || face[c] >= out.vertices) valid = false;
      if (!valid) continue;
      for (int c = 0; c < 3; c++) used.insert(face[c]);
      for (int c = 0; c < 3; c++)
        edges.push_back(Distance(mesh.vertices[face[c]], mesh.vertices[face[(c + 1) % 3]]));
    }
    out.orphanVertices = out.vertices - static_cast<int>(used.size());

    if (!edges.empty()) {
      std::sort(edges.begin(), edges.end());
      out.medianEdge = edges[edges.size() / 2];
      out.longestEdge = edges.back();
      if (edgeCut > 0.0f && out.medianEdge > 0.0f) {
        out.cutRatio = edgeCut / out.medianEdge;
        out.tooCoarseForCut = out.cutRatio < kCoarseCutRatio;
      }
    }

    const std::string signature = Signature(mesh);
    if (!signature.empty()) {
      for (const auto& entry : seen) {
        if (entry.first == signature) {
          out.duplicateOf = entry.second;
          break;
        }
      }
      if (out.duplicateOf.empty()) seen.emplace_back(signature, mesh.name);
    }

    report.totalVertices += out.vertices;
    report.totalFaces += out.faces;
    if (out.empty) report.emptyMeshes++;
    if (!out.duplicateOf.empty()) report.duplicateMeshes++;
    report.meshes.push_back(out);
  }
  return report;
}

std::vector<std::array<float, 3>> ReadPositions(const float* data, int floats,
                                               int elementCount) {
  std::vector<std::array<float, 3>> out;
  if (!data || floats <= 0 || elementCount <= 0)
    return out;
  const int vertices = floats / elementCount / 3;
  out.reserve(vertices);
  for (int v = 0; v < vertices; v++)
    out.push_back({data[v * 3 + 0], data[v * 3 + 1], data[v * 3 + 2]});
  return out;
}

}  // namespace ModelInventory
}  // namespace blunted

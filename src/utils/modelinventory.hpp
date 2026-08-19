// What a model is made of: a per-mesh account, for a viewer and for judging an import.
//
// Troubleshooting a model through a running match is the wrong tool - it loads a
// stadium, a crowd, 22 players and a presentation before you can look at one mesh.
// This is the part a standalone viewer needs.
//
// It exists because a lot of imported models are missing visible geometry.
// strip_stretched_tris applies an absolute --max-edge, 0.15 m by default, and across
// the 90 imported player models 44 have their longest surviving edge sitting exactly
// on that cut - nine of them coarse meshes where 0.15 m is only 1.6x to 3.6x their
// median edge (lcg_2715's median is 9.5 cm). The shards the cut was written for were
// 1.25 m against a 1.9 cm median, 65x it. An absolute cut cannot serve both.

#ifndef _HPP_UTILS_MODELINVENTORY
#define _HPP_UTILS_MODELINVENTORY

#include <array>
#include <string>
#include <vector>

namespace blunted {
namespace ModelInventory {

// A mesh as the viewer reads it: positions and index triples, nothing else.
struct Mesh {
  std::string name;
  std::vector<std::array<float, 3>> vertices;
  std::vector<std::array<int, 3>> faces;
};

// What is worth knowing about one mesh.
struct MeshReport {
  std::string name;
  int vertices = 0;
  int faces = 0;
  int orphanVertices = 0;   // present but used by no face: the fingerprint of a drop
  bool empty = false;       // vertices but no faces at all
  float medianEdge = 0.0f;
  float longestEdge = 0.0f;
  // The cut that was applied, over this mesh's own median edge. Below about 4 the cut
  // cannot have removed only outliers.
  float cutRatio = 0.0f;
  bool tooCoarseForCut = false;
  std::string duplicateOf;  // the earlier mesh this one repeats, if any
};

struct Report {
  std::vector<MeshReport> meshes;
  int totalVertices = 0;
  int totalFaces = 0;
  int emptyMeshes = 0;
  int duplicateMeshes = 0;
};

// Below this many median edges, an absolute cut is removing ordinary geometry rather
// than outliers. lcg_2715 sat at 1.6, eight more between 2.6 and 3.6.
constexpr float kCoarseCutRatio = 4.0f;

// `edgeCut` is the threshold the model was imported under, for the ratio; pass 0 to
// skip that judgement.
Report Describe(const std::vector<Mesh>& meshes, float edgeCut = 0.0f);

}  // namespace ModelInventory
}  // namespace blunted

#endif

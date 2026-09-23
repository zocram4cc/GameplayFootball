#include "onthepitch/player/humanoid/skinning.hpp"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace Skinning {

JointTransform MakeJointTransform(const blunted::Quaternion& orientation,
                                  const blunted::Vector3& origPos,
                                  const blunted::Vector3& position, float zMultiplier) {
  // The rotation matrix of the same quaternion Vector3::Rotate applies. That
  // rotates by v + 2w(q x v) + 2q x (q x v), which for a unit quaternion is
  // exactly this matrix - so building it once a frame costs nothing in fidelity.
  const float x = orientation.elements[0];
  const float y = orientation.elements[1];
  const float z = orientation.elements[2];
  const float w = orientation.elements[3];
  const float xx = x * x, yy = y * y, zz = z * z;
  const float xy = x * y, xz = x * z, yz = y * z;
  const float wx = w * x, wy = w * y, wz = w * z;

  // Row-major rotation, then laid down as columns.
  const float rotation[9] = {1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz),        2.0f * (xz + wy),
                             2.0f * (xy + wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx),
                             2.0f * (xz - wy),        2.0f * (yz + wx),        1.0f - 2.0f * (xx + yy)};

  // R * (v - bind) + posed, gathered into a single translation.
  const float bind[3] = {(float)origPos.coords[0] * zMultiplier,
                         (float)origPos.coords[1] * zMultiplier,
                         (float)origPos.coords[2] * zMultiplier};
  JointTransform transform;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) transform.column[col][row] = rotation[row * 3 + col];
    transform.column[3][row] =
        (float)position.coords[row] * zMultiplier -
        (rotation[row * 3 + 0] * bind[0] + rotation[row * 3 + 1] * bind[1] +
         rotation[row * 3 + 2] * bind[2]);
  }
  for (int col = 0; col < 4; col++) transform.column[col][3] = 0.0f;
  return transform;
}

// Where the texture vertex sits among a mesh's elements: position, normal,
// texture, tangent, bitangent - the order ASELoader writes them in.
constexpr int kTextureElement = 2;

// How far apart two vertices' texture coordinates may be and still merge. A
// 2 cm cell on a 1.8 m body mapped over a 1024 px atlas spans about 12 px
// (0.012) within one UV island; neighbouring islands are further apart than
// that. 1/64 of the texture (16 px at 1024) merges within an island and never
// across a seam. Provisional: measured on dbg_2023's atlas, not a sweep.
constexpr float kSeamUvTolerance = 1.0f / 64.0f;

ClusteredMesh ClusterDecimate(const float* vertices, int vertexCount, int elementCount,
                              const std::vector<unsigned int>& indices, float cell) {
  ClusteredMesh out;
  if (vertexCount <= 0 || cell <= 0.0f) return out;
  const int elementStride = vertexCount * 3;
  const float inverseCell = 1.0f / cell;
  const float* uv = elementCount > kTextureElement ? &vertices[kTextureElement * elementStride]
                                                    : nullptr;
  auto sameTexel = [uv](int a, int b) {
    return !uv || (std::fabs(uv[a * 3] - uv[b * 3]) <= kSeamUvTolerance &&
                   std::fabs(uv[a * 3 + 1] - uv[b * 3 + 1]) <= kSeamUvTolerance);
  };

  // cell -> the vertices standing for it, one per part of the texture it holds.
  // Position alone is not enough: at a UV seam the same point belongs to two
  // islands of the atlas, and merging them handed one island's triangles the
  // other's texture coordinates - Caulifla's hair drew her skin past 30 m.
  std::unordered_map<uint64_t, std::vector<int>> representativesOfCell;
  representativesOfCell.reserve(vertexCount);
  std::vector<int> representative(vertexCount);
  for (int v = 0; v < vertexCount; v++) {
    const float* p = &vertices[v * 3];
    // 21 bits a coordinate, offset so a negative one packs: 2 million cells a side
    const uint64_t cx = (uint64_t)((int64_t)std::floor(p[0] * inverseCell) + (1 << 20)) & 0x1FFFFF;
    const uint64_t cy = (uint64_t)((int64_t)std::floor(p[1] * inverseCell) + (1 << 20)) & 0x1FFFFF;
    const uint64_t cz = (uint64_t)((int64_t)std::floor(p[2] * inverseCell) + (1 << 20)) & 0x1FFFFF;
    const uint64_t key = (cx << 42) | (cy << 21) | cz;
    std::vector<int>& standing = representativesOfCell[key];
    int chosen = -1;
    for (int r : standing)
      if (sameTexel(r, v)) {
        chosen = r;
        break;
      }
    if (chosen < 0) {
      standing.push_back(v);
      chosen = v;
    }
    representative[v] = chosen;
  }

  // Triangles whose corners spread over three cells, on compacted vertex ids.
  std::vector<int> compact(vertexCount, -1);
  out.indices.reserve(indices.size() / 4);
  for (size_t t = 0; t + 2 < indices.size(); t += 3) {
    int r[3];
    for (int c = 0; c < 3; c++) {
      const unsigned int source = indices[t + c];
      r[c] = source < (unsigned int)vertexCount ? representative[source] : -1;
    }
    if (r[0] < 0 || r[1] < 0 || r[2] < 0) continue;
    if (r[0] == r[1] || r[1] == r[2] || r[0] == r[2]) continue;
    for (int c = 0; c < 3; c++) {
      if (compact[r[c]] < 0) {
        compact[r[c]] = (int)out.sourceVertex.size();
        out.sourceVertex.push_back(r[c]);
      }
      out.indices.push_back((unsigned int)compact[r[c]]);
    }
  }

  const int outCount = out.vertexCount();
  out.vertices.resize((size_t)outCount * 3 * elementCount);
  for (int e = 0; e < elementCount; e++) {
    const float* sourceElement = &vertices[(size_t)e * elementStride];
    float* outElement = &out.vertices[(size_t)e * outCount * 3];
    for (int v = 0; v < outCount; v++) {
      const float* p = &sourceElement[out.sourceVertex[v] * 3];
      outElement[v * 3 + 0] = p[0];
      outElement[v * 3 + 1] = p[1];
      outElement[v * 3 + 2] = p[2];
    }
  }
  return out;
}

bool UseBodyLod(float distanceToCamera, float lodDistance, bool currentlyLod) {
  if (lodDistance <= 0.0f) return false;
  // Leave the coarse copy two metres nearer than it was taken up.
  return distanceToCamera > (currentlyLod ? lodDistance - 2.0f : lodDistance);
}

int BatchSize(int bodyCount, int workerCount) {
  if (workerCount < 1) return bodyCount > 0 ? bodyCount : 1;  // empty pool: one inline batch
  if (bodyCount < 1) return 1;                                // never a batch of zero to loop on
  return (bodyCount + workerCount - 1) / workerCount;         // round up, so nothing is left over
}

bool BodyNeedsSkinning(bool distantFromAction, bool halveDistantRate, int phase,
                       int phaseOffset) {
  if (!halveDistantRate || !distantFromAction) return true;
  // The offset is per body, so half the squad takes each frame rather than all
  // the distant bodies landing on the same one.
  return phase == 1 - phaseOffset;
}

}  // namespace Skinning

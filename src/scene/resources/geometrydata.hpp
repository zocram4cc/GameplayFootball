// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_OBJECT_GEOMETRYDATA
#define _HPP_OBJECT_GEOMETRYDATA

#include "base/math/vector3.hpp"
#include "defines.hpp"
#include "scene/object.hpp"
#include "surface.hpp"
#include "types/interpreter.hpp"
#include "types/material.hpp"
#include "types/resource.hpp"

namespace blunted {

struct MaterializedTriangleMesh {
  Material material;

  float* vertices;       // was: triangleMesh
  int verticesDataSize;  // was: triangleMeshSize

  /* contents:
  float vertices[verticesDataSize * 3];
  float normals[verticesDataSize * 3];
  float texturevertices[verticesDataSize * 3];
  float tangents[verticesDataSize * 3];
  float bitangents[verticesDataSize * 3];
  */

  std::vector<unsigned int> indices;
};

class GeometryData {
public:
  GeometryData();
  virtual ~GeometryData();
  GeometryData(const GeometryData& src);

  void DeleteTriangleMeshes();
  // todo: what about resource acquisition is ownership?
  void SetTriangleMesh(Material material, float* vertices, int verticesDataSize,
                       const std::vector<unsigned int>& indices);
  void AddTriangleMesh(Material material, float* vertices, int verticesDataSize,
                       const std::vector<unsigned int>& indices);
  std::vector<MaterializedTriangleMesh> GetTriangleMeshes();
  std::vector<MaterializedTriangleMesh>& GetTriangleMeshesRef();
  void SetDynamic(bool dynamic) { isDynamic = dynamic; }
  bool IsDynamic() { return isDynamic; }

  // Which attribute arrays a dynamic mesh actually rewrites, as a bitmask over
  // the element order (0 position, 1 normal, 2 texture vertex, 3 tangent, 4
  // bitangent). Default: assume all of them, which is what any caller that has
  // not thought about it needs.
  //
  // A skinned body rewrites positions and normals every frame, its tangent
  // frame only when a normal map is bound, and its texture vertices never - but
  // the graphics interpreter was copying all five arrays into the vertex buffer
  // regardless. Measured over a match: 2.7 GB/s of vertex data moved for 28
  // bodies a frame, 60% of it bytes that had not changed.
  void SetDynamicElements(unsigned int mask) { dynamicElementMask = mask; }
  unsigned int GetDynamicElements() const { return dynamicElementMask; }
  static constexpr unsigned int kAllElements = 0xFFFFFFFFu;

  // The graphics system concatenates every submesh into one element-major array
  // before uploading it: all positions, then all normals, and so on, with submesh
  // i's vertices starting `submeshOffset[i]` floats into each element. Skinning
  // wrote each submesh's own array and the interpreter then copied the changed
  // elements into that one - a copy that only existed to concatenate, about
  // 17 MB a frame over a squad. The interpreter publishes its array here so a
  // writer can put its output straight into it; a writer that does says so with
  // SetWritesUploadTarget, and the interpreter stops copying the dynamic
  // elements for it. Whoever owns the array clears this before freeing it.
  struct UploadTarget {
    float* data = nullptr;
    int elementStride = 0;
    std::vector<int> submeshOffset;
  };
  void SetUploadTarget(float* data, int elementStride, std::vector<int> submeshOffset) {
    uploadTarget.data = data;
    uploadTarget.elementStride = elementStride;
    uploadTarget.submeshOffset = std::move(submeshOffset);
  }
  void ClearUploadTarget() { uploadTarget = UploadTarget(); }
  const UploadTarget& GetUploadTarget() const { return uploadTarget; }
  void SetWritesUploadTarget(bool writes) { writesUploadTarget = writes; }
  bool WritesUploadTarget() const { return writesUploadTarget; }

  AABB GetAABB() const;

protected:
  bool isDynamic;
  unsigned int dynamicElementMask = kAllElements;
  UploadTarget uploadTarget;
  bool writesUploadTarget = false;
  std::vector<MaterializedTriangleMesh> triangleMeshes;

  mutable AABBCache aabb;
};

}  // namespace blunted

#endif

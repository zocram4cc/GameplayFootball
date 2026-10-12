// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_OBJECT_GEOMETRYDATA
#define _HPP_OBJECT_GEOMETRYDATA

#include "base/geometry/aabb.hpp"
#include "defines.hpp"

namespace blunted {

struct TriangleMesh {
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
  GeometryData(const GeometryData& src) = delete;
  GeometryData& operator=(const GeometryData& src) = delete;

  // Takes ownership of `vertices` (new[]).
  void AddTriangleMesh(float* vertices, int verticesDataSize,
                       const std::vector<unsigned int>& indices);

  AABB GetAABB() const;

protected:
  std::vector<TriangleMesh> triangleMeshes;

  mutable AABBCache aabb;
};

}  // namespace blunted

#endif

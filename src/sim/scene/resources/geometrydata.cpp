// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "geometrydata.hpp"

#include "base/geometry/trianglemeshutils.hpp"

namespace blunted {

GeometryData::GeometryData() {
  aabb.aabb.Reset();
  aabb.dirty = false;
}

GeometryData::~GeometryData() {
  for (unsigned int i = 0; i < triangleMeshes.size(); i++) {
    delete[] triangleMeshes.at(i).vertices;
  }
}

void GeometryData::AddTriangleMesh(float* vertices, int verticesDataSize,
                                   const std::vector<unsigned int>& indices) {
  assert(indices.size() % 3 == 0);
  TriangleMesh mesh;
  mesh.vertices = vertices;
  mesh.verticesDataSize = verticesDataSize;
  mesh.indices = indices;

  triangleMeshes.push_back(mesh);
  aabb.aabb.Reset();
  aabb.dirty = true;
}

AABB GeometryData::GetAABB() const {
  if (aabb.dirty) {
    aabb.aabb.Reset();
    for (int i = 0; i < (signed int)triangleMeshes.size(); i++) {
      aabb.aabb +=
          GetTriangleMeshAABB(triangleMeshes.at(i).vertices, triangleMeshes.at(i).verticesDataSize,
                              triangleMeshes.at(i).indices);
    }
    aabb.dirty = false;
  }
  return aabb.aabb;
}

}  // namespace blunted

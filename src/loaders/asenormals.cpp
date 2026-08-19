#include "loaders/asenormals.hpp"

namespace blunted {
namespace AseNormals {

Vector3 FromWinding(const Vector3& a, const Vector3& b, const Vector3& c) {
  const Vector3 normal = Vector3(b - a).GetCrossProduct(Vector3(c - a));
  const float length = normal.GetLength();
  if (length < 1e-9f) return Vector3(0, 0, 0);
  return normal / length;
}

}  // namespace AseNormals
}  // namespace blunted

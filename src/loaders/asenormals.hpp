// Deriving a triangle's normal from its winding, so an ASE need not store one.
//
// Normals are 45% of the bytes in an imported stadium: of pes_st002.ase's 1,132 MB,
// MESH_VERTEXNORMAL accounts for 386.6 MB and MESH_FACENORMAL for 121.2 MB. They are
// also redundant there - over 200,000 sampled faces of pes_st011.ase, every single
// one has its three vertex normals identical, so the file writes a flat normal three
// times and carries no smoothing at all.
//
// Not everywhere: props.ase is 7.0% flat and entrance.ase 1.8%, because the
// paramedics and the flag bearers really are smooth-shaded. So a mesh that ships
// normals keeps them, and only a mesh that ships none is given these.
//
// The convention is measured rather than assumed. Against the normals already in
// adboards.ase, (b - a) x (c - a) reproduces them exactly, and (c - a) x (b - a)
// gives the opposite sign every time.

#ifndef _HPP_LOADERS_ASENORMALS
#define _HPP_LOADERS_ASENORMALS

#include "base/math/vector3.hpp"

namespace blunted {
namespace AseNormals {

// The flat normal of the triangle a, b, c, normalised. A degenerate triangle -
// collinear corners, or two the same - has no direction to point in and comes back
// as the zero vector rather than dividing by nothing.
Vector3 FromWinding(const Vector3& a, const Vector3& b, const Vector3& c);

}  // namespace AseNormals
}  // namespace blunted

#endif

// Deriving a triangle's normal from its winding, so an ASE need not store it.
//
// Normals are 45% of the bytes in an imported stadium: of pes_st002.ase's 1,132 MB,
// MESH_VERTEXNORMAL is 386.6 MB and MESH_FACENORMAL 121.2 MB. They are also
// redundant there - over 200,000 sampled faces of pes_st011.ase, 100% have all three
// vertex normals identical, so the file stores a flat normal three times over and
// carries no smoothing at all.
//
// Not everywhere, though: props.ase is 7.0% flat and entrance.ase 1.8%, because the
// paramedics and the flag bearers are genuinely smooth-shaded. So this is what the
// loader uses when a mesh ships no normals, and a mesh that ships them keeps them.
//
// The convention is measured, not assumed. Comparing candidate cross products against
// the normals already in adboards.ase, (b - a) x (c - a) reproduces them exactly and
// (c - a) x (b - a) gives the opposite sign every time.

#include <gtest/gtest.h>

#include <cmath>

#include "loaders/asenormals.hpp"

using blunted::Vector3;

TEST(AseNormals, AFlatTriangleFacingUp) {
  // wound anticlockwise seen from +z
  const Vector3 n = blunted::AseNormals::FromWinding(
      Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(0, 1, 0));
  EXPECT_NEAR(n.coords[0], 0.0f, 1e-5);
  EXPECT_NEAR(n.coords[1], 0.0f, 1e-5);
  EXPECT_NEAR(n.coords[2], 1.0f, 1e-5);
}

TEST(AseNormals, ReversingTheWindingReversesIt) {
  const Vector3 n = blunted::AseNormals::FromWinding(
      Vector3(0, 0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0));
  EXPECT_NEAR(n.coords[2], -1.0f, 1e-5);
}

TEST(AseNormals, ItComesOutNormalised) {
  const Vector3 n = blunted::AseNormals::FromWinding(
      Vector3(0, 0, 0), Vector3(40, 0, 0), Vector3(0, 90, 0));
  EXPECT_NEAR(std::sqrt(n.coords[0] * n.coords[0] + n.coords[1] * n.coords[1] +
                        n.coords[2] * n.coords[2]),
              1.0f, 1e-5);
}

// A far-touchline hoarding facing the pitch, taken from the ring: its stored normal
// is -y, and the winding has to give that back.
TEST(AseNormals, AHoardingFacingThePitch) {
  const Vector3 n = blunted::AseNormals::FromWinding(
      Vector3(0.0f, 40.0f, 0.0f), Vector3(2.0f, 40.0f, 0.0f), Vector3(2.0f, 40.0f, 1.0f));
  EXPECT_NEAR(n.coords[0], 0.0f, 1e-5);
  EXPECT_NEAR(n.coords[1], -1.0f, 1e-5);
  EXPECT_NEAR(n.coords[2], 0.0f, 1e-5);
}

TEST(AseNormals, ADegenerateTriangleHasNoDirectionToPointIn) {
  // three collinear points, or two the same: a zero cross product. Returning a
  // zero vector says "no normal" rather than dividing by nothing.
  const Vector3 n = blunted::AseNormals::FromWinding(
      Vector3(0, 0, 0), Vector3(1, 1, 1), Vector3(2, 2, 2));
  EXPECT_NEAR(n.GetLength(), 0.0f, 1e-5);
}

TEST(AseNormals, TwoIdenticalCornersAreDegenerateToo) {
  const Vector3 n = blunted::AseNormals::FromWinding(
      Vector3(3, 4, 5), Vector3(3, 4, 5), Vector3(9, 9, 9));
  EXPECT_NEAR(n.GetLength(), 0.0f, 1e-5);
}

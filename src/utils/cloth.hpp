// A hanging surface that falls, swings and settles.
//
// The stadium is full of cloth that was authored rigid: the corner flags' two
// panels hang off their pole in the pose the artist left them in, the crowd's
// banners and the pennant ring are flat sheets, and the goal netting was dragged
// straight at the ball on the frame it touched and teleported back to its rest
// pose on the frame it stopped. None of it has any weight.
//
// This is the smallest thing that gives them some: Verlet points, one link per
// mesh edge, and a few relaxation passes. There is no bending stiffness, no
// self-collision and no aerodynamics - a flag modelled this way hangs and swings
// but will not fold along a crease. That is the trade: it costs two vectors per
// vertex and a handful of multiplies per link, so every net panel and flag in a
// stadium can run every frame.
//
// Fixed points are the caller's business, because what holds a surface up differs:
// netting is tied to the woodwork all the way round its border, a flag is held
// along the pole and free everywhere else, a banner hangs from its top edge.

#ifndef _HPP_UTILS_CLOTH
#define _HPP_UTILS_CLOTH

#include <utility>
#include <vector>

#include "base/math/vector3.hpp"

namespace blunted {

class Cloth {
 public:
  // Links are index pairs into `rest`; their rest length is the distance between
  // the two rest positions. Duplicates and degenerate (zero length) links are
  // dropped, so a caller can hand over every triangle edge of a mesh without
  // deduplicating them first.
  void Build(const std::vector<Vector3>& rest, const std::vector<bool>& fixed,
             const std::vector<std::pair<int, int>>& links);

  // One step. `damping` is the fraction of velocity kept between steps, so 1 is
  // frictionless and 0 kills all motion; `iterations` relaxation passes run over
  // the links afterwards. Steps of an unbounded size are clamped: a stall or a
  // pause hands over a dt of whole seconds, and a Verlet integrator handed one of
  // those throws the cloth off the pitch.
  void Step(float dt, const Vector3& acceleration, float damping, int iterations);

  // Pushes points out of a sphere - the ball in the net. Applied to positions, so
  // the motion it causes is picked up by the next step's velocity.
  void Push(const Vector3& centre, float radius);

  // Back to the pose it was built in, at rest.
  void Reset();

  const std::vector<Vector3>& Positions() const { return positions; }
  bool Empty() const { return positions.empty(); }

  // The largest distance any point has moved from its rest position. What tells a
  // caller how far the surface has left the pose it was authored in.
  float Displacement() const;

  // The largest distance any point moved on the last step. This is what says
  // whether the surface is still doing anything: a net that has taken up its sag
  // sits far from its authored rest pose forever, so displacement never falls back
  // and would keep it awake for the whole match.
  float Speed() const;

  static constexpr float kMaxStep_s = 0.05f;

 private:
  void Relax(int iterations);

  std::vector<Vector3> rest;
  std::vector<Vector3> positions;
  std::vector<Vector3> previous;
  std::vector<bool> fixed;
  std::vector<std::pair<int, int>> links;
  std::vector<float> lengths;
};

// -> one link per distinct edge of a triangle list, `indices` being 3 per face.
std::vector<std::pair<int, int>> LinksFromTriangles(const std::vector<int>& indices);

// -> which vertices sit on the mesh's border: those on an edge that only one
// triangle uses. A net panel's border is where it is tied to the woodwork and the
// ground, so this is what holds netting up.
std::vector<bool> BorderVertices(const std::vector<Vector3>& rest,
                                 const std::vector<int>& indices);

// -> which vertices are within `radius` of the line through `point` along `axis`.
// A flag is held along its pole, a banner along its top edge.
std::vector<bool> VerticesNearAxis(const std::vector<Vector3>& rest, const Vector3& point,
                                   const Vector3& axis, float radius);

// -> which vertices lie within `tolerance` of the plane `axis == at`, axis being 0,
// 1 or 2. A goal net is tied to the woodwork on the goalmouth plane and pegged to
// the ground on z = 0.
std::vector<bool> VerticesOnPlane(const std::vector<Vector3>& rest, int axis, float at,
                                  float tolerance);

// `into` |= `other`, for adding one attachment to another.
void UnionInto(std::vector<bool>& into, const std::vector<bool>& other);

// -> `a` & `b`, for an attachment that is a line rather than a plane: the net's rear
// top edge is where the back plane meets the top one.
std::vector<bool> Both(const std::vector<bool>& a, const std::vector<bool>& b);

}  // namespace blunted

#endif

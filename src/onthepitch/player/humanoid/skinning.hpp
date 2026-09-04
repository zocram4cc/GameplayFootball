// Linear blend skinning expressed as one affine transform per joint.
//
// The engine's original path rotated every vertex - and its normal, tangent and
// bitangent - once per influence, so a vertex with two bones paid eight
// quaternion rotations, and the joint's rotation was recomputed for every vertex
// that referenced it. On the native PES body (20458 vertices) that put the put
// phase at 49.7 ms a frame, against 7.4 ms for the 453-vertex legacy body.
//
// Since each influence is affine in the vertex, the weighted sum of influences
// is itself an affine transform: blend the joints' transforms once per vertex,
// then apply that one transform to all four attributes. The joint matrices are
// built once per frame instead of once per vertex per attribute, and the result
// is arithmetically the same thing - see tests/onthepitch/skinning_transform_test.cpp.

#ifndef _HPP_ONTHEPITCH_PLAYER_HUMANOID_SKINNING
#define _HPP_ONTHEPITCH_PLAYER_HUMANOID_SKINNING

#include <vector>

#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

#include "base/math/quaternion.hpp"
#include "base/math/vector3.hpp"

namespace Skinning {

// The affine transform a single joint applies to bind-pose geometry:
//   positions   p' = M * p + t
//   directions  d' = M * d
// stored as four columns of four floats - the three columns of M, then t, each
// padded with a zero - so that every kernel below is a handful of 4-wide
// operations: blending a joint in is four multiply-adds, a point is three
// multiply-adds and an add. The scalar loops this replaced were 104 mulss and 79
// addss per vertex in the objdump of SkinInto, with SSE2 sitting unused.
struct alignas(16) JointTransform {
  float column[4][4];
};

// The transform for one influence, matching the engine's per-influence
// arithmetic: R * (v - origPos * zMultiplier) + position * zMultiplier.
JointTransform MakeJointTransform(const blunted::Quaternion& orientation,
                                  const blunted::Vector3& origPos,
                                  const blunted::Vector3& position, float zMultiplier);

// Blending: zero an accumulator, then add each influence's transform scaled by
// its weight. Weights are used as given - the caller has already normalised them.
// Defined here rather than in skinning.cpp on purpose. The build is -O3 without
// link-time optimisation, so a definition in another translation unit is a real
// call: objdump of UpdateFullbodyModel showed five to eight of them per vertex,
// with the blended transform spilled to the stack because its address escaped.
// A body carries 20k-100k vertices and there are 22 of them a frame.
//
// SSE2 is the x86-64 baseline, so this needs no -march and runs on every machine
// the game does; the scalar branch is the same arithmetic for anything else.
#if defined(__SSE2__) || defined(_M_X64)
#define SKINNING_SSE 1
#endif

inline void ZeroTransform(JointTransform& transform) {
#ifdef SKINNING_SSE
  const __m128 zero = _mm_setzero_ps();
  for (int c = 0; c < 4; c++) _mm_store_ps(transform.column[c], zero);
#else
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) transform.column[c][r] = 0.0f;
#endif
}

inline void AddWeighted(JointTransform& accumulator, const JointTransform& transform, float weight) {
#ifdef SKINNING_SSE
  const __m128 w = _mm_set1_ps(weight);
  for (int c = 0; c < 4; c++) {
    _mm_store_ps(accumulator.column[c],
                 _mm_add_ps(_mm_load_ps(accumulator.column[c]),
                            _mm_mul_ps(_mm_load_ps(transform.column[c]), w)));
  }
#else
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) accumulator.column[c][r] += transform.column[c][r] * weight;
#endif
}

#ifdef SKINNING_SSE
// M * (x, y, z) as columns: the caller adds the translation column or not.
inline __m128 Rotate(const JointTransform& transform, const float in[3]) {
  __m128 out = _mm_mul_ps(_mm_load_ps(transform.column[0]), _mm_set1_ps(in[0]));
  out = _mm_add_ps(out, _mm_mul_ps(_mm_load_ps(transform.column[1]), _mm_set1_ps(in[1])));
  return _mm_add_ps(out, _mm_mul_ps(_mm_load_ps(transform.column[2]), _mm_set1_ps(in[2])));
}
// Three of the four lanes: the vertex arrays are packed xyz, so a four-float
// store would tread on the next vertex - or past the end of the array.
inline void Store3(__m128 v, float out[3]) {
  _mm_storel_pi(reinterpret_cast<__m64*>(out), v);
  _mm_store_ss(out + 2, _mm_movehl_ps(v, v));
}
#endif

inline void TransformPoint(const JointTransform& transform, const float in[3], float out[3]) {
#ifdef SKINNING_SSE
  Store3(_mm_add_ps(Rotate(transform, in), _mm_load_ps(transform.column[3])), out);
#else
  for (int r = 0; r < 3; r++)
    out[r] = transform.column[0][r] * in[0] + transform.column[1][r] * in[1] +
             transform.column[2][r] * in[2] + transform.column[3][r];
#endif
}
inline void TransformDirection(const JointTransform& transform, const float in[3], float out[3]) {
#ifdef SKINNING_SSE
  Store3(Rotate(transform, in), out);
#else
  for (int r = 0; r < 3; r++)
    out[r] = transform.column[0][r] * in[0] + transform.column[1][r] * in[1] +
             transform.column[2][r] * in[2];
#endif
}

// How many bodies to hand one worker thread, so a squad spreads across the whole
// pool instead of a fixed few tasks. Zero workers means an empty pool, where the
// caller runs the work inline and splitting it would only add overhead.
int BatchSize(int bodyCount, int workerCount);

// Whether a body must be skinned on this frame. The engine used to skin any body
// away from the action on alternate frames only, so it animated at half the frame
// rate; that is off by default now that a body costs a quarter of what it did.
bool BodyNeedsSkinning(bool distantFromAction, bool halveDistantRate, int phase, int phaseOffset);

// A coarser copy of a skinned mesh for bodies far from the camera. The imported
// bodies carry 20k-180k vertices and no LOD at all, and past a few tens of metres a
// body is a few hundred pixels tall, so most of that is skinned for nothing.
//
// Vertex clustering: every vertex snaps to the grid cell of `cell` metres it sits
// in, the first vertex seen in a cell stands for the cell (its position, normal,
// texture vertex - and, through `sourceVertex`, its skin weights), and a triangle
// survives only if its three corners land in three different cells. O(n), no
// topology, and at distance the error is bounded by the cell: at 25 m and 1280 px
// wide a 2 cm cell is about one pixel.
//
// `vertices` is element-major over `vertexCount` vertices, like a
// MaterializedTriangleMesh; the result has the same layout and element count.
struct ClusteredMesh {
  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  std::vector<int> sourceVertex;  // result vertex -> the source vertex it stands for
  int vertexCount() const { return (int)sourceVertex.size(); }
};
ClusteredMesh ClusterDecimate(const float* vertices, int vertexCount, int elementCount,
                              const std::vector<unsigned int>& indices, float cell);

// Whether a body this far from the camera renders its coarse copy. With a band of
// hysteresis so a body loitering on the threshold does not flicker between the two.
bool UseBodyLod(float distanceToCamera, float lodDistance, bool currentlyLod);

}  // namespace Skinning

#endif

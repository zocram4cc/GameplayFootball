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

#include "base/math/quaternion.hpp"
#include "base/math/vector3.hpp"

namespace Skinning {

// The affine transform a single joint applies to bind-pose geometry:
//   positions   p' = rotation * p + translation
//   directions  d' = rotation * d
struct JointTransform {
  float rotation[9];  // row major
  float translation[3];
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
inline void ZeroTransform(JointTransform& transform) {
  for (int i = 0; i < 9; i++) transform.rotation[i] = 0.0f;
  for (int i = 0; i < 3; i++) transform.translation[i] = 0.0f;
}

inline void AddWeighted(JointTransform& accumulator, const JointTransform& transform, float weight) {
  for (int i = 0; i < 9; i++) accumulator.rotation[i] += transform.rotation[i] * weight;
  for (int i = 0; i < 3; i++) accumulator.translation[i] += transform.translation[i] * weight;
}

inline void TransformPoint(const JointTransform& transform, const float in[3], float out[3]) {
  out[0] = transform.rotation[0] * in[0] + transform.rotation[1] * in[1] +
           transform.rotation[2] * in[2] + transform.translation[0];
  out[1] = transform.rotation[3] * in[0] + transform.rotation[4] * in[1] +
           transform.rotation[5] * in[2] + transform.translation[1];
  out[2] = transform.rotation[6] * in[0] + transform.rotation[7] * in[1] +
           transform.rotation[8] * in[2] + transform.translation[2];
}
inline void TransformDirection(const JointTransform& transform, const float in[3], float out[3]) {
  out[0] = transform.rotation[0] * in[0] + transform.rotation[1] * in[1] +
           transform.rotation[2] * in[2];
  out[1] = transform.rotation[3] * in[0] + transform.rotation[4] * in[1] +
           transform.rotation[5] * in[2];
  out[2] = transform.rotation[6] * in[0] + transform.rotation[7] * in[1] +
           transform.rotation[8] * in[2];
}

// How many bodies to hand one worker thread, so a squad spreads across the whole
// pool instead of a fixed few tasks. Zero workers means an empty pool, where the
// caller runs the work inline and splitting it would only add overhead.
int BatchSize(int bodyCount, int workerCount);

// Whether a body must be skinned on this frame. The engine used to skin any body
// away from the action on alternate frames only, so it animated at half the frame
// rate; that is off by default now that a body costs a quarter of what it did.
bool BodyNeedsSkinning(bool distantFromAction, bool halveDistantRate, int phase, int phaseOffset);

}  // namespace Skinning

#endif

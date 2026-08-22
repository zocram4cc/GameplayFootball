// Skinning as an affine transform per joint, rather than a quaternion rotation
// per influence per attribute.
//
// HumanoidBase::UpdateFullbodyModel skins every vertex by rotating it - and its
// normal, tangent and bitangent - once per influence, so a two-bone vertex pays
// eight quaternion rotations. With the native PES body at 20458 vertices that
// put phase measured 49.7 ms a frame against 7.4 ms for the 453-vertex legacy
// body, which is the whole of the dropped-frame problem.
//
// The way out is that the per-influence transform is *affine in the vertex*:
//     f_b(v) = R_b * (v - o_b * z) + p_b * z
// so the weighted sum of the influences,
//     sum_b w_b * f_b(v)
// equals a single affine transform - the weighted sum of the transforms -
// applied once. That turns 4*bones rotations into bones matrix blends plus 4
// matrix applies, and it is not an approximation: these tests hold the new path
// against the engine's own quaternion arithmetic and demand it agree.
//
// Nothing here may relax that. A tolerance loose enough to hide a real
// difference would defeat the point, so the comparisons are at float precision.

#include <gtest/gtest.h>

#include <cmath>

#include "base/math/quaternion.hpp"
#include "base/math/vector3.hpp"
#include "gamedefines.hpp"
#include "onthepitch/player/humanoid/skinning.hpp"

using blunted::Quaternion;
using blunted::Vector3;
using Skinning::JointTransform;

namespace {

// Exactly what humanoidbase.cpp does for one influence, kept here as the
// reference so the test is anchored to the engine's arithmetic and not to a
// restatement of the new code.
Vector3 EngineSkinPoint(const Quaternion& orientation, const Vector3& origPos,
                        const Vector3& position, float zMultiplier, const Vector3& v) {
  Vector3 out = v;
  out -= origPos * zMultiplier;
  out.Rotate(orientation);
  out += position * zMultiplier;
  return out;
}

Vector3 EngineSkinDirection(const Quaternion& orientation, const Vector3& d) {
  Vector3 out = d;
  out.Rotate(orientation);
  return out;
}

Quaternion AxisAngle(const Vector3& axis, float angle) {
  Quaternion q;
  q.SetAngleAxis(angle, axis.GetNormalized());
  return q;
}

Vector3 Apply(const JointTransform& t, const Vector3& v, bool isPoint) {
  float out[3];
  if (isPoint)
    Skinning::TransformPoint(t, v.coords, out);
  else
    Skinning::TransformDirection(t, v.coords, out);
  return Vector3(out[0], out[1], out[2]);
}

void ExpectClose(const Vector3& a, const Vector3& b, float epsilon = 1e-5f) {
  EXPECT_NEAR(a.coords[0], b.coords[0], epsilon);
  EXPECT_NEAR(a.coords[1], b.coords[1], epsilon);
  EXPECT_NEAR(a.coords[2], b.coords[2], epsilon);
}

}  // namespace

TEST(SkinningTransform, AJointThatHasNotMovedLeavesThePointWhereItIs) {
  const Vector3 at(0.3f, 1.1f, -0.7f);
  const JointTransform t =
      Skinning::MakeJointTransform(Quaternion(), at, at, 1.0f);
  ExpectClose(Apply(t, Vector3(2.0f, -3.0f, 0.5f), true), Vector3(2.0f, -3.0f, 0.5f));
}

TEST(SkinningTransform, TransformingAPointMatchesTheEnginesQuaternionPath) {
  const Quaternion orientation = AxisAngle(Vector3(0.2f, 1.0f, -0.4f), 0.9f);
  const Vector3 bind(0.1f, 1.4f, 0.05f);
  const Vector3 posed(-0.6f, 1.2f, 3.2f);
  const float zMultiplier = 1.07f;  // the engine's height scaling, rarely 1
  const Vector3 vertex(0.22f, 1.55f, -0.13f);

  const JointTransform t =
      Skinning::MakeJointTransform(orientation, bind, posed, zMultiplier);
  ExpectClose(Apply(t, vertex, true),
              EngineSkinPoint(orientation, bind, posed, zMultiplier, vertex));
}

TEST(SkinningTransform, TransformingADirectionRotatesItAndIgnoresTheTranslation) {
  const Quaternion orientation = AxisAngle(Vector3(-0.7f, 0.3f, 0.6f), -1.4f);
  const JointTransform t = Skinning::MakeJointTransform(
      orientation, Vector3(0.4f, 1.0f, 0.2f), Vector3(9.0f, 1.3f, -4.0f), 1.1f);
  const Vector3 normal = Vector3(0.3f, 0.8f, -0.5f).GetNormalized();

  ExpectClose(Apply(t, normal, false), EngineSkinDirection(orientation, normal));
}

TEST(SkinningTransform, BlendingTwoInfluencesEqualsBlendingTheirResults) {
  const Quaternion firstRot = AxisAngle(Vector3(0.0f, 1.0f, 0.0f), 0.7f);
  const Quaternion secondRot = AxisAngle(Vector3(1.0f, 0.2f, 0.3f), -1.1f);
  const Vector3 firstBind(0.05f, 1.45f, 0.0f), firstPosed(2.0f, 1.5f, 1.0f);
  const Vector3 secondBind(0.2f, 1.2f, 0.1f), secondPosed(2.1f, 1.25f, 1.05f);
  const float zMultiplier = 0.96f;
  const Vector3 vertex(0.18f, 1.31f, 0.04f);
  const float weights[2] = {0.65f, 0.35f};

  const JointTransform first =
      Skinning::MakeJointTransform(firstRot, firstBind, firstPosed, zMultiplier);
  const JointTransform second =
      Skinning::MakeJointTransform(secondRot, secondBind, secondPosed, zMultiplier);

  JointTransform blended;
  Skinning::ZeroTransform(blended);
  Skinning::AddWeighted(blended, first, weights[0]);
  Skinning::AddWeighted(blended, second, weights[1]);

  const Vector3 perInfluence =
      EngineSkinPoint(firstRot, firstBind, firstPosed, zMultiplier, vertex) * weights[0] +
      EngineSkinPoint(secondRot, secondBind, secondPosed, zMultiplier, vertex) * weights[1];

  ExpectClose(Apply(blended, vertex, true), perInfluence);
}

TEST(SkinningTransform, BlendingDirectionsMatchesTheEngineBeforeRenormalising) {
  // The engine sums the rotated normals and only then normalises, so the blended
  // transform has to agree with the *unnormalised* sum for the normalise to be
  // the same operation on both paths.
  const Quaternion firstRot = AxisAngle(Vector3(0.1f, 1.0f, 0.0f), 1.2f);
  const Quaternion secondRot = AxisAngle(Vector3(0.4f, -0.2f, 1.0f), 0.5f);
  const JointTransform first = Skinning::MakeJointTransform(
      firstRot, Vector3(0.0f, 1.4f, 0.0f), Vector3(3.0f, 1.4f, 2.0f), 1.02f);
  const JointTransform second = Skinning::MakeJointTransform(
      secondRot, Vector3(0.1f, 1.1f, 0.0f), Vector3(3.1f, 1.1f, 2.1f), 1.02f);
  const Vector3 normal = Vector3(-0.2f, 0.5f, 0.84f).GetNormalized();

  JointTransform blended;
  Skinning::ZeroTransform(blended);
  Skinning::AddWeighted(blended, first, 0.5f);
  Skinning::AddWeighted(blended, second, 0.5f);

  ExpectClose(Apply(blended, normal, false),
              EngineSkinDirection(firstRot, normal) * 0.5f +
                  EngineSkinDirection(secondRot, normal) * 0.5f);
}

TEST(SkinningTransform, ThreeInfluencesTheRigsMaximumStillAgree) {
  const Quaternion rots[3] = {AxisAngle(Vector3(0, 1, 0), 0.4f),
                              AxisAngle(Vector3(1, 0, 0), -0.8f),
                              AxisAngle(Vector3(0.3f, 0.3f, 1.0f), 1.9f)};
  const Vector3 binds[3] = {Vector3(0.0f, 1.5f, 0.0f), Vector3(0.15f, 1.3f, 0.02f),
                            Vector3(0.3f, 1.1f, -0.05f)};
  const Vector3 posed[3] = {Vector3(5.0f, 1.55f, -2.0f), Vector3(5.2f, 1.32f, -1.9f),
                            Vector3(5.35f, 1.14f, -1.95f)};
  const float weights[3] = {0.5f, 0.3f, 0.2f};
  const float zMultiplier = 1.13f;
  const Vector3 vertex(0.21f, 1.28f, 0.01f);

  JointTransform blended;
  Skinning::ZeroTransform(blended);
  Vector3 perInfluence(0.0f);
  for (int i = 0; i < 3; i++) {
    Skinning::AddWeighted(
        blended, Skinning::MakeJointTransform(rots[i], binds[i], posed[i], zMultiplier),
        weights[i]);
    perInfluence +=
        EngineSkinPoint(rots[i], binds[i], posed[i], zMultiplier, vertex) * weights[i];
  }

  ExpectClose(Apply(blended, vertex, true), perInfluence);
}

// A player's hands are the same body as the rest of him.
//
// PES authors one hand and the import composites it onto every character, so the
// question is whether a short player gets a short player's hands. He does, and by the
// same arithmetic as everything else: humanoidbase multiplies every vertex of the
// fullbody geometry by zMultiplier = height / defaultPlayerHeight before skinning it,
// and the hand is meshes inside that geometry rather than a thing attached to it.
//
// Pinned here because it is the kind of property that a later optimisation - skinning
// the hands on their own, say - would quietly break.
TEST(SkinningTransform, AHandScalesWithThePlayer) {
  const Vector3 wrist(0.604f, -0.07f, 1.064f);       // retarget.py's left_hand
  const Vector3 fingertip(0.604f, -0.07f, 0.884f);   // 18 cm along the hand
  Quaternion identity;
  identity.SetAngleAxis(0.0f, Vector3(0, 0, 1));

  const float shortPlayer = 1.70f / defaultPlayerHeight;
  const float tallPlayer = 2.05f / defaultPlayerHeight;

  const JointTransform low = Skinning::MakeJointTransform(identity, wrist, wrist, shortPlayer);
  const JointTransform high = Skinning::MakeJointTransform(identity, wrist, wrist, tallPlayer);

  const Vector3 lowTip = Apply(low, fingertip * shortPlayer, true);
  const Vector3 highTip = Apply(high, fingertip * tallPlayer, true);
  const Vector3 lowWrist = Apply(low, wrist * shortPlayer, true);
  const Vector3 highWrist = Apply(high, wrist * tallPlayer, true);

  const float shortHand = (lowTip - lowWrist).GetLength();
  const float tallHand = (highTip - highWrist).GetLength();
  EXPECT_GT(tallHand, shortHand);
  // And in proportion: the ratio of the hands is the ratio of the players.
  EXPECT_NEAR(tallHand / shortHand, 2.05f / 1.70f, 1e-4f);
}

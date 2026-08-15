// Linear-blend-skinning convention tests for the native PES rig migration.
//
// HumanoidBase skins each influence as
//     R_current * R_bind^-1 * (v - p_bind) + p_current
// (humanoidbase.cpp UpdateFullbodyNodes/UpdateFullbodyModel). These tests pin
// down the two properties the engine relies on:
//  1. reproduction: posing the skeleton at the bind pose reproduces the mesh
//     exactly, whatever the bind orientations are;
//  2. helper-collapse: a bone that rigidly follows its parent produces
//     exactly the parent's vertex transform, so collapsing PES's dsk_/skh_
//     helper bones onto their animated ancestors is lossless.

#include <gtest/gtest.h>

#include "base/math/quaternion.hpp"
#include "base/math/vector3.hpp"

using blunted::Quaternion;
using blunted::Vector3;

namespace {

struct TestJoint {
  Vector3 bindPos;
  Quaternion bindRot;
  Vector3 curPos;
  Quaternion curRot;
};

// the engine's per-influence transform
Vector3 SkinInfluence(const TestJoint& j, const Vector3& v) {
  Vector3 out = v - j.bindPos;
  Quaternion delta = (j.curRot * j.bindRot.GetInverse()).GetNormalized();
  out.Rotate(delta);
  return out + j.curPos;
}

Quaternion AxisAngle(const Vector3& axis, float angle) {
  Quaternion q;
  q.SetAngleAxis(angle, axis.GetNormalized());
  return q;
}

}  // namespace

TEST(SkinningMath, BindPoseReproducesMeshExactly) {
  // non-identity bind orientation must not matter at the bind pose
  TestJoint j;
  j.bindPos = Vector3(0.2f, 0.0f, 1.0f);
  j.bindRot = AxisAngle(Vector3(0, 1, 0), 0.7f);
  j.curPos = j.bindPos;
  j.curRot = j.bindRot;
  Vector3 v(0.25f, -0.1f, 0.8f);
  Vector3 out = SkinInfluence(j, v);
  EXPECT_NEAR(out.coords[0], v.coords[0], 1e-5f);
  EXPECT_NEAR(out.coords[1], v.coords[1], 1e-5f);
  EXPECT_NEAR(out.coords[2], v.coords[2], 1e-5f);
}

TEST(SkinningMath, BlendedVertexIsContinuousAcrossAJoint) {
  // two joints sharing a vertex 50/50: the result must sit midway between
  // the two rigid predictions, not fly off (the old absolute-orientation
  // skinning tore here when bind rotations differed)
  TestJoint upper, lower;
  upper.bindPos = Vector3(0, 0, 1.4f);
  upper.bindRot = AxisAngle(Vector3(0, 1, 0), 0.17f);  // authored with a tilt
  lower.bindPos = Vector3(0, 0, 1.1f);
  lower.bindRot = AxisAngle(Vector3(1, 0, 0), -0.6f);  // bent elbow bind

  // pose: upper unchanged, lower bends 90 degrees about X through the joint
  upper.curPos = upper.bindPos;
  upper.curRot = upper.bindRot;
  lower.curPos = lower.bindPos;
  lower.curRot = AxisAngle(Vector3(1, 0, 0), 0.9f) * lower.bindRot;

  Vector3 v(0.0f, 0.02f, 1.1f);  // sits on the joint pivot
  Vector3 a = SkinInfluence(upper, v);
  Vector3 b = SkinInfluence(lower, v);
  Vector3 blended = a * 0.5f + b * 0.5f;

  // both rigid predictions stay near the pivot (v is ~2cm from it), so the
  // blend must too; with absolute-orientation skinning the lower influence
  // would rotate v by the full bind-included orientation and jump ~decimetres
  EXPECT_LT((a - v).GetLength(), 0.05f);
  EXPECT_LT((b - v).GetLength(), 0.05f);
  EXPECT_LT((blended - v).GetLength(), 0.05f);
}

TEST(SkinningMath, RigidChildCollapsesLosslesslyOntoParent) {
  // a helper bone that rigidly follows its parent transforms any vertex
  // exactly as the parent does -- the property the dsk_/skh_ collapse
  // (tools/pes21_import/retarget.py resolve_bone) relies on
  TestJoint parent;
  parent.bindPos = Vector3(0.1f, 0.0f, 1.2f);
  parent.bindRot = AxisAngle(Vector3(0, 0, 1), 0.3f);
  parent.curPos = Vector3(0.4f, 0.2f, 1.0f);
  parent.curRot = AxisAngle(Vector3(1, 0, 0), 1.1f) * parent.bindRot;

  // helper: offset bind, follows parent rigidly
  Quaternion parentDelta =
      (parent.curRot * parent.bindRot.GetInverse()).GetNormalized();
  TestJoint helper;
  helper.bindPos = Vector3(0.15f, -0.05f, 1.05f);
  helper.bindRot = AxisAngle(Vector3(0, 1, 0), -0.8f);  // arbitrary
  Vector3 offset = helper.bindPos - parent.bindPos;
  offset.Rotate(parentDelta);
  helper.curPos = parent.curPos + offset;
  helper.curRot = (parentDelta * helper.bindRot).GetNormalized();

  Vector3 v(0.18f, -0.02f, 1.02f);
  Vector3 viaHelper = SkinInfluence(helper, v);
  Vector3 viaParent = SkinInfluence(parent, v);
  EXPECT_NEAR(viaHelper.coords[0], viaParent.coords[0], 1e-5f);
  EXPECT_NEAR(viaHelper.coords[1], viaParent.coords[1], 1e-5f);
  EXPECT_NEAR(viaHelper.coords[2], viaParent.coords[2], 1e-5f);
}

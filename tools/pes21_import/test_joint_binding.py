"""Tests for the joint-binding check.

The gap this closes: a model whose arm never bends still passes every other
check we have. It parses, the mesh and face counts are healthy, the textures
resolve, and a turntable still shows a connected figure with arms - because
nothing has asked the arm to move. Only a vertex weighted to the elbow makes
the elbow real, and 34 of the 107 imported models on disk were missing at least
one arm joint when this was written.

Run: python3 -m unittest test_joint_binding -v
"""

import os
import tempfile
import unittest

import joint_binding
import retarget


def write_weights(path, rows):
    """rows: [(x, y, z, [(joint name, weight)])]"""
    ids = retarget.JOINT_ID
    with open(path, "w") as out:
        out.write("# gfweights 1\n")
        for x, y, z, binds in rows:
            terms = " ".join("%d:%f" % (ids[n], w) for n, w in binds)
            out.write("%f %f %f %s\n" % (x, y, z, terms))


class WhichJointsTheSkinActuallyDrives(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.path = os.path.join(self.dir, "fullbody_test.weights")

    def fully_bound_rows(self):
        return [(0.0, 0.0, 1.0, [(name, 1.0)]) for name in joint_binding.REQUIRED]

    def test_a_model_bound_to_every_joint_reports_nothing(self):
        write_weights(self.path, self.fully_bound_rows())
        self.assertEqual(joint_binding.unbound_joints(self.path), [])

    def test_a_missing_wrist_is_reported(self):
        """The commonest real case: hand geometry sits off the rig's wrist, so
        nearest-joint falloff gives it to the elbow and the hand never
        articulates."""
        rows = [r for r in self.fully_bound_rows()
                if r[3][0][0] not in ("left_hand", "right_hand")]
        write_weights(self.path, rows)
        self.assertEqual(joint_binding.unbound_joints(self.path),
                         ["left_hand", "right_hand"])

    def test_a_whole_dead_arm_is_reported_in_rig_order(self):
        dead = {"left_shoulder", "left_elbow", "left_hand"}
        rows = [r for r in self.fully_bound_rows() if r[3][0][0] not in dead]
        write_weights(self.path, rows)
        self.assertEqual(joint_binding.unbound_joints(self.path),
                         ["left_shoulder", "left_elbow", "left_hand"])

    def test_a_token_influence_does_not_count_as_bound(self):
        """A rounding-sized weight does not make the joint drive anything, and
        counting it would report a dead limb as healthy."""
        rows = [(0.0, 0.0, 1.0, [(name, 1.0)]) for name in joint_binding.REQUIRED
                if name != "left_hand"]
        rows.append((0.6, 0.0, 1.0, [("left_hand", 0.01)]))
        write_weights(self.path, rows)
        self.assertIn("left_hand", joint_binding.unbound_joints(self.path))

    def test_legs_are_checked_too(self):
        rows = [r for r in self.fully_bound_rows() if r[3][0][0] != "right_ankle"]
        write_weights(self.path, rows)
        self.assertEqual(joint_binding.unbound_joints(self.path), ["right_ankle"])


if __name__ == "__main__":
    unittest.main()

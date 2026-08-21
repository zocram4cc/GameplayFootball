"""Curling a bound hand out of its splayed bind pose.

PES models hands flat with the fingers spread and poses them at runtime from finger
channels this rig does not have, so they sit in bind forever and read as a paddle.
These tests pin the one bend that replaces that.
"""

import math
import unittest

import hand_pose


def straight_hand(length=0.18, width=0.09, count=20):
    """A flat hand at the origin: fingers along +x, spread across z."""
    points = []
    for i in range(count):
        along = length * i / float(count - 1)
        for lane in (-1.0, 0.0, 1.0):
            points.append((along, 0.0, lane * width * 0.5))
    return points


class HandCurl(unittest.TestCase):
    def test_the_palm_does_not_move(self):
        points = straight_hand()
        wrist = (0.0, 0.0, 0.0)
        posed = hand_pose.curl(points, wrist, degrees=35.0)
        for before, after in zip(points, posed):
            along = before[0]
            if along <= 0.18 * hand_pose.KNUCKLE_FRACTION - 1e-6:
                self.assertEqual(before, after, "a palm vertex moved")

    def test_the_fingertips_move_furthest(self):
        points = straight_hand()
        posed = hand_pose.curl(points, (0.0, 0.0, 0.0), degrees=35.0)
        shifts = [math.dist(b, a) for b, a in zip(points, posed)]
        # the last lane of points is the fingertips
        self.assertGreater(max(shifts[-3:]), max(shifts[:3]) + 0.01)

    def test_the_hand_gets_shorter_as_it_closes(self):
        points = straight_hand()
        posed = hand_pose.curl(points, (0.0, 0.0, 0.0), degrees=35.0)
        self.assertLess(max(p[0] for p in posed), max(p[0] for p in points))

    def test_it_curls_rather_than_splays(self):
        # the fingers must stay as wide as they were: a fold about the wrong axis
        # would fan them out or squeeze them together
        points = straight_hand()
        posed = hand_pose.curl(points, (0.0, 0.0, 0.0), degrees=35.0)
        before = max(p[2] for p in points) - min(p[2] for p in points)
        after = max(p[2] for p in posed) - min(p[2] for p in posed)
        self.assertAlmostEqual(before, after, delta=before * 0.15)

    def test_no_curl_leaves_the_hand_alone(self):
        points = straight_hand()
        self.assertEqual(hand_pose.curl(points, (0.0, 0.0, 0.0), degrees=0.0), points)

    def test_an_empty_mesh_is_not_an_error(self):
        self.assertEqual(hand_pose.curl([], (0.0, 0.0, 0.0)), [])

    def test_a_degenerate_mesh_is_left_as_it_is(self):
        points = [(0.0, 0.0, 0.0)] * 4
        self.assertEqual(hand_pose.curl(points, (0.0, 0.0, 0.0)), points)

    def test_nothing_becomes_a_nan(self):
        posed = hand_pose.curl(straight_hand(), (0.0, 0.0, 0.0), degrees=35.0)
        for p in posed:
            for c in p:
                self.assertFalse(math.isnan(c) or math.isinf(c))


class AseRewrite(unittest.TestCase):
    ASE = """*GEOMOBJECT {
\t*NODE_NAME "hand_l"
\t*MESH {
\t\t*MESH_VERTEX 0\t0.100000\t0.000000\t0.000000
\t\t*MESH_VERTEX 1\t0.180000\t0.000000\t0.010000
\t\t*MESH_VERTEX 2\t0.000000\t0.000000\t0.000000
\t}
}
*GEOMOBJECT {
\t*NODE_NAME "shirt"
\t*MESH {
\t\t*MESH_VERTEX 0\t0.500000\t0.500000\t0.500000
\t}
}
"""

    def test_only_the_hand_is_touched(self):
        posed, moved = hand_pose.pose_ase(self.ASE, degrees=35.0)
        self.assertIn("hand_l", moved)
        self.assertNotIn("shirt", moved)
        self.assertIn("0.500000\t0.500000\t0.500000", posed)

    def test_the_vertex_count_survives(self):
        posed, _ = hand_pose.pose_ase(self.ASE, degrees=35.0)
        self.assertEqual(posed.count("*MESH_VERTEX"), self.ASE.count("*MESH_VERTEX"))

    def test_a_file_with_no_hands_is_reported_rather_than_mangled(self):
        posed, moved = hand_pose.pose_ase(self.ASE, hand_names=("nothing",), degrees=35.0)
        self.assertEqual(moved, {})
        self.assertEqual(posed, self.ASE)

    def test_the_geometry_blocks_are_found_by_brace_depth(self):
        names = [name for name, _, _ in hand_pose.geom_blocks(self.ASE)]
        self.assertEqual(names, ["hand_l", "shirt"])


if __name__ == "__main__":
    unittest.main()

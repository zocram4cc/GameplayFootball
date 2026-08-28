"""Tests for the offline skinning probe.

Both of these pin a mistake the probe made on its first run, because both
mistakes produced confident numbers rather than errors.

Run: python3 -m unittest test_skin_probe -v
"""

import math
import os
import tempfile
import unittest

import skin_probe


class TheAnimIsOneLinePerJointNotPerFrame(unittest.TestCase):
    """A track is the name then repeating groups of frame and value. Read as a
    single keyframe - which the first version did - every measurement is of the
    rest pose however high a frame is asked for, and it looks like an answer."""

    def write(self, text):
        handle, path = tempfile.mkstemp(suffix=".anim")
        os.close(handle)
        open(path, "w").write(text)
        return path

    def test_every_keyframe_on_the_line_is_read(self):
        path = self.write("chest,0,0.0,0.0,0.0,1.0,2,0.1,0.0,0.0,0.9,8,0.2,0.0,0.0,0.8\n")
        frames = skin_probe.read_anim(path)["chest"]
        self.assertEqual(sorted(frames), [0, 2, 8])
        self.assertAlmostEqual(frames[8][0], 0.2)

    def test_the_player_track_is_a_position_and_has_a_shorter_group(self):
        path = self.write("player,0,1.0,2.0,3.0,2,4.0,5.0,6.0\n")
        frames = skin_probe.read_anim(path)["player"]
        self.assertEqual(sorted(frames), [0, 2])
        self.assertEqual(frames[2][:3], (4.0, 5.0, 6.0))

    def test_a_sparse_track_holds_its_last_keyframe(self):
        path = self.write("chest,0,0.0,0.0,0.0,1.0,10,0.5,0.0,0.0,0.5\n")
        frames = skin_probe.read_anim(path)["chest"]
        self.assertEqual(skin_probe.pose_at(frames, 4), frames[0])
        self.assertEqual(skin_probe.pose_at(frames, 10), frames[10])
        self.assertEqual(skin_probe.pose_at(frames, 99), frames[10])


class EdgesComeFromTheMeshNotFromProximity(unittest.TestCase):
    """Two surfaces that merely lie close - the inside of an arm and the ribs -
    separate the moment the arm moves. Counted as edges that reads as a tear
    when nothing has torn, which is what the first version reported."""

    def test_only_edges_a_triangle_owns_are_returned(self):
        handle, path = tempfile.mkstemp(suffix=".ase")
        os.close(handle)
        open(path, "w").write(
            '*GEOMOBJECT {\n\t*NODE_NAME "m"\n'
            "\t*MESH_VERTEX 0\t0.0\t0.0\t0.0\n"
            "\t*MESH_VERTEX 1\t1.0\t0.0\t0.0\n"
            "\t*MESH_VERTEX 2\t0.0\t1.0\t0.0\n"
            "\t*MESH_FACE 0: A: 0 B: 1 C: 2 AB: 1 BC: 1 CA: 1\n}\n")
        positions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0),
                     (0.001, 0.001, 0.0)]  # close to vertex 0 and joined to nothing
        edges = skin_probe.mesh_edges(path, positions)
        self.assertEqual(edges, {(0, 1), (1, 2), (0, 2)})
        self.assertNotIn((0, 3), edges)

    def test_a_missing_mesh_is_no_edges_rather_than_a_crash(self):
        self.assertEqual(skin_probe.mesh_edges("/nowhere/x.ase", []), set())


class SkinningIsTheEnginesArithmetic(unittest.TestCase):
    def test_an_unrotated_rig_leaves_the_mesh_where_it_was(self):
        bind = {"a": (0.0, 0.0, 1.0)}
        world = {"a": ((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 1.0))}
        posed = skin_probe.skin([(0.0, 0.1, 1.0)], [[(0, 1.0)]], bind, world, {0: "a"})
        for got, want in zip(posed[0], (0.0, 0.1, 1.0)):
            self.assertAlmostEqual(got, want)

    def test_a_joint_turns_the_vertices_it_owns_about_itself(self):
        bind = {"a": (0.0, 0.0, 0.0)}
        half = math.sqrt(0.5)
        world = {"a": ((0.0, 0.0, half, half), (0.0, 0.0, 0.0))}  # 90 deg about Z
        posed = skin_probe.skin([(1.0, 0.0, 0.0)], [[(0, 1.0)]], bind, world, {0: "a"})
        self.assertAlmostEqual(posed[0][0], 0.0, places=5)
        self.assertAlmostEqual(posed[0][1], 1.0, places=5)


if __name__ == "__main__":
    unittest.main()

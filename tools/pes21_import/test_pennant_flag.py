import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pennant_flag


def _ase(*meshes):
    header = '*3DSMAX_ASCIIEXPORT 200\n*MATERIAL_LIST {\n}\n'
    return header + "".join(meshes)


def _mesh(name, vertices, material=0):
    lines = ["*GEOMOBJECT {", '\t*NODE_NAME "%s"' % name, "\t*MESH {",
             "\t\t*MESH_NUMVERTEX %d" % len(vertices), "\t\t*MESH_NUMFACES 1",
             "\t\t*MESH_VERTEX_LIST {"]
    for i, v in enumerate(vertices):
        lines.append("\t\t\t*MESH_VERTEX %d\t%.6f\t%.6f\t%.6f" % (i, v[0], v[1], v[2]))
    lines += ["\t\t}", "\t\t*MESH_FACE_LIST {",
              "\t\t\t*MESH_FACE 0:    A:    0 B:    1 C:    2 AB:    1 BC:    1 CA:    1",
              "\t\t}", "\t}", "\t*MATERIAL_REF %d" % material, "}"]
    return "\n".join(lines) + "\n"


class TheFlagIsBuiltWhereTheBearersHoldIt(unittest.TestCase):
    """The imported cloth is PES's REST state - flat on the grass, because the
    ceremony's gani is what lifts it. Rebuilt, the rim has to sit at the
    bearers' hands and the middle has to hang below it."""

    def test_the_rim_is_held_up_and_the_middle_sags(self):
        vertices, faces, uvs = pennant_flag.disc(8.0)
        self.assertTrue(faces and uvs)
        rim = [v for v in vertices if math.hypot(v[0], v[1]) > 7.99]
        self.assertTrue(rim)
        for v in rim:
            self.assertAlmostEqual(v[2], pennant_flag.HOLD_HEIGHT, places=5)
        centre = min(vertices, key=lambda v: math.hypot(v[0], v[1]))
        self.assertAlmostEqual(centre[2], pennant_flag.HOLD_HEIGHT - pennant_flag.SAG, places=5)
        self.assertLess(centre[2], min(v[2] for v in rim))

    def test_every_vertex_stays_inside_the_ring_and_on_the_map(self):
        vertices, _, uvs = pennant_flag.disc(8.38)
        for v in vertices:
            self.assertLessEqual(math.hypot(v[0], v[1]), 8.38 + 1e-6)
        for u, v in uvs:
            self.assertGreaterEqual(u, -1e-6)
            self.assertLessEqual(u, 1.0 + 1e-6)
            self.assertGreaterEqual(v, -1e-6)
            self.assertLessEqual(v, 1.0 + 1e-6)

    def test_the_faces_index_vertices_that_exist(self):
        vertices, faces, _ = pennant_flag.disc(6.0)
        for face in faces:
            for corner in face:
                self.assertLess(corner, len(vertices))

    def test_a_bearer_is_not_mistaken_for_cloth(self):
        bearer = _mesh("bearers", [(8.0, 0.0, 0.0), (8.1, 0.0, 1.88), (8.2, 0.1, 1.2)])
        cloth = _mesh("cloth", [(0.0, 0.0, 0.0), (8.0, 0.0, 0.42), (0.0, 8.0, 0.10)])
        self.assertFalse(pennant_flag.is_cloth(bearer))
        self.assertTrue(pennant_flag.is_cloth(cloth))

    def test_the_bearers_pass_through_and_the_cloth_is_replaced(self):
        bearer = _mesh("bearers", [(8.0, 0.0, 0.0), (8.1, 0.0, 1.88), (8.2, 0.1, 1.2)])
        cloth = _mesh("cloth", [(0.0, 0.0, 0.0), (8.0, 0.0, 0.42), (0.0, 8.0, 0.10)], material=1)
        new, replaced, radius = pennant_flag.rebuild(_ase(bearer, cloth))
        self.assertEqual(replaced, 1)
        # The cloth reaches the ring at 8.0 m; the rim is held an arm inside it.
        self.assertAlmostEqual(radius, 8.0 - pennant_flag.ARM_REACH, places=3)
        # The bearers come back as one instance per place in the ring, so the
        # name carries the slot: PES's own figures, stood evenly round the flag.
        self.assertIn('*NODE_NAME "bearers_bearer00"', new)
        self.assertEqual(new.count('_bearer'), 2 * pennant_flag.RING_BEARERS)
        self.assertNotIn('*NODE_NAME "cloth"', new)
        self.assertIn(pennant_flag.NODE, new)
        self.assertIn(pennant_flag.UNDERSIDE_NODE, new)

    def test_running_it_twice_does_not_stack_flags(self):
        bearer = _mesh("bearers", [(8.0, 0.0, 0.0), (8.1, 0.0, 1.88), (8.2, 0.1, 1.2)])
        cloth = _mesh("cloth", [(0.0, 0.0, 0.0), (8.0, 0.0, 0.42), (0.0, 8.0, 0.10)])
        once, _, _ = pennant_flag.rebuild(_ase(bearer, cloth))
        twice, replaced, _ = pennant_flag.rebuild(once)
        self.assertEqual(replaced, 2)  # the disc and its backing, recognised
        self.assertEqual(twice.count(pennant_flag.NODE + '"'), once.count(pennant_flag.NODE + '"'))


if __name__ == "__main__":
    unittest.main()

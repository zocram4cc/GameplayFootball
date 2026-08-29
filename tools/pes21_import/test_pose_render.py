"""Tests for the offline posed renderer.

The rig work had no way to look at a skinned pose - gfviewer loads a static
object - so every judgement came from numbers, and the numbers misled twice.
This checks the one piece of logic the tool owns: matching the .ase's per-mesh
vertex numbering onto the weights file's single run, by position.
"""

import os
import tempfile
import unittest

import pose_render


class MatchingAseVerticesOntoTheWeightsRun(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.ase = os.path.join(self.dir, "m.ase")
        self.weights = os.path.join(self.dir, "m.weights")
        self.anim = os.path.join(self.dir, "m.anim")
        # one triangle, its three vertices bound rigidly to the body joint
        open(self.ase, "w").write(
            '*GEOMOBJECT {\n'
            '\t*NODE_NAME "one"\n'
            '\t*MESH_VERTEX 0\t0.100000\t0.000000\t1.000000\n'
            '\t*MESH_VERTEX 1\t0.200000\t0.000000\t1.000000\n'
            '\t*MESH_VERTEX 2\t0.100000\t0.000000\t1.100000\n'
            '\t*MESH_FACE 0: A: 0 B: 1 C: 2\n'
            '}\n')
        open(self.weights, "w").write(
            "0.100000 0.000000 1.000000 0:1.0\n"
            "0.200000 0.000000 1.000000 0:1.0\n"
            "0.100000 0.000000 1.100000 0:1.0\n")
        open(self.anim, "w").write("player,0,0.000000,0.000000,0.000000\n")

    def test_every_ase_vertex_finds_its_weights_entry(self):
        meshes = pose_render.posed_vertices(self.ase, self.weights, self.anim, 0)
        self.assertEqual(len(meshes), 1)
        name, verts, faces = meshes[0]
        self.assertEqual(name, "one")
        self.assertEqual(len(verts), 3)
        self.assertEqual(faces, [(0, 1, 2)])

    def test_an_identity_pose_leaves_the_rest_shape_alone(self):
        # no rotation in the clip, so the skinned mesh is the bind mesh; a
        # mismatch here means the position match failed and the vertex fell
        # back to its .ase value, which would hide a real fault
        meshes = pose_render.posed_vertices(self.ase, self.weights, self.anim, 0)
        _, verts, _ = meshes[0]
        for got, want in zip((verts[0], verts[1], verts[2]),
                             ((0.1, 0.0, 1.0), (0.2, 0.0, 1.0), (0.1, 0.0, 1.1))):
            for a, b in zip(got, want):
                self.assertAlmostEqual(a, b, places=4)


if __name__ == "__main__":
    unittest.main()

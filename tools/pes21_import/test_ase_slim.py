"""Tests for dropping the normals an ASE does not need.

Normals are 45% of the bytes in an imported stadium: of pes_st002.ase's 1,132 MB,
MESH_VERTEXNORMAL is 386.6 MB and MESH_FACENORMAL 121.2 MB. They are also redundant
there - over 200,000 sampled faces of pes_st011.ase, every one has its three vertex
normals identical, so the file writes a flat normal three times and carries no
smoothing at all. The loader derives exactly that from the winding when a mesh ships
none (src/loaders/asenormals.cpp).

Redundant is not the same as everywhere. props.ase measures 7.0% flat and
entrance.ase 1.8%, because the paramedics and the flag bearers really are
smooth-shaded, and dropping their normals would flatten them. So the decision is per
mesh: a mesh whose every face is flat loses its MESH_NORMALS block, and a mesh with
any smoothing in it keeps the block whole.

Run: python3 -m unittest test_ase_slim -v
"""

import unittest

import ase_slim


def mesh(name, faces):
    """One GEOMOBJECT whose normals are given as (facenormal, [v0, v1, v2])."""
    out = ['*GEOMOBJECT {', '\t*NODE_NAME "%s"' % name, '\t*MESH {',
           '\t\t*MESH_NUMFACES %d' % len(faces), '\t\t*MESH_NORMALS {']
    for i, (fn, vns) in enumerate(faces):
        out.append('\t\t\t*MESH_FACENORMAL %d\t%s' % (i, "\t".join("%.4f" % c for c in fn)))
        for v, vn in enumerate(vns):
            out.append('\t\t\t\t*MESH_VERTEXNORMAL %d\t%s'
                       % (v, "\t".join("%.4f" % c for c in vn)))
    out += ['\t\t}', '\t}', '}']
    return "\n".join(out) + "\n"


FLAT = mesh("shell", [((0, 0, 1), [(0, 0, 1)] * 3), ((0, 1, 0), [(0, 1, 0)] * 3)])
SMOOTH = mesh("paramedic", [((0, 0, 1), [(0, 0, 1), (0.1, 0, 0.99), (0, 0.1, 0.99)])])


class DroppingWhatIsDerivable(unittest.TestCase):
    def test_a_flat_mesh_loses_its_normals(self):
        out, stats = ase_slim.slim(FLAT)
        self.assertNotIn("MESH_NORMALS", out)
        self.assertNotIn("MESH_VERTEXNORMAL", out)
        self.assertEqual(stats["slimmed"], 1)
        self.assertEqual(stats["kept"], 0)

    def test_a_smooth_mesh_keeps_them_whole(self):
        out, stats = ase_slim.slim(SMOOTH)
        self.assertEqual(out, SMOOTH)
        self.assertEqual(stats["slimmed"], 0)
        self.assertEqual(stats["kept"], 1)

    def test_one_smooth_face_is_enough_to_keep_the_block(self):
        mixed = mesh("mixed", [((0, 0, 1), [(0, 0, 1)] * 3),
                               ((0, 1, 0), [(0, 1, 0), (0.1, 0.99, 0), (0, 1, 0)])])
        out, stats = ase_slim.slim(mixed)
        self.assertIn("MESH_VERTEXNORMAL", out)
        self.assertEqual(stats["kept"], 1)

    def test_each_mesh_is_judged_on_its_own(self):
        out, stats = ase_slim.slim(FLAT + SMOOTH)
        self.assertEqual((stats["slimmed"], stats["kept"]), (1, 1))
        # the smooth one's normals survive in the output
        self.assertIn("0.9900", out)

    def test_everything_but_the_normals_is_untouched(self):
        # every tagged line that is not a normal survives. The brace that closed the
        # MESH_NORMALS block is part of that block and goes with it, so compare the
        # content rather than the punctuation.
        out, _ = ase_slim.slim(FLAT)
        kept = out.splitlines()
        for line in FLAT.splitlines():
            tag = line.strip()
            if not tag.startswith("*") or "NORMAL" in tag:
                continue
            self.assertIn(line, kept)

    def test_the_braces_stay_balanced(self):
        out, _ = ase_slim.slim(FLAT + SMOOTH)
        self.assertEqual(out.count("{"), out.count("}"))

    def test_running_it_twice_changes_nothing_more(self):
        once, _ = ase_slim.slim(FLAT + SMOOTH)
        twice, stats = ase_slim.slim(once)
        self.assertEqual(once, twice)
        self.assertEqual(stats["slimmed"], 0)

    def test_a_mesh_with_no_normals_at_all_is_left_alone(self):
        bare = '*GEOMOBJECT {\n\t*NODE_NAME "x"\n\t*MESH {\n\t\t*MESH_NUMFACES 0\n\t}\n}\n'
        out, stats = ase_slim.slim(bare)
        self.assertEqual(out, bare)
        self.assertEqual((stats["slimmed"], stats["kept"]), (0, 0))

    def test_text_with_no_meshes_comes_back_whole(self):
        self.assertEqual(ase_slim.slim("*3DSMAX_ASCIIEXPORT 200\n")[0],
                         "*3DSMAX_ASCIIEXPORT 200\n")

    def test_the_bytes_saved_are_reported(self):
        out, stats = ase_slim.slim(FLAT)
        self.assertEqual(stats["saved"], len(FLAT) - len(out))
        self.assertGreater(stats["saved"], 0)

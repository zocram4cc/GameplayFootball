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


def mesh_with_geometry(name, tris, face_normals):
    """A GEOMOBJECT carrying positions, faces and a flat normal per face.

    tris: [((x,y,z), (x,y,z), (x,y,z))]; face_normals: one per triangle, written
    three times over as a flat face does.
    """
    verts = []
    for tri in tris:
        verts.extend(tri)
    out = ['*GEOMOBJECT {', '\t*NODE_NAME "%s"' % name, '\t*MESH {',
           '\t\t*MESH_NUMVERTEX %d' % len(verts), '\t\t*MESH_NUMFACES %d' % len(tris),
           '\t\t*MESH_VERTEX_LIST {']
    for i, v in enumerate(verts):
        out.append('\t\t\t*MESH_VERTEX %d\t%.4f\t%.4f\t%.4f' % (i, v[0], v[1], v[2]))
    out += ['\t\t}', '\t\t*MESH_FACE_LIST {']
    for i in range(len(tris)):
        out.append('\t\t\t*MESH_FACE %d:    A: %d B: %d C: %d  AB: 1 BC: 1 CA: 1'
                   % (i, i * 3, i * 3 + 1, i * 3 + 2))
    out += ['\t\t}', '\t\t*MESH_NORMALS {']
    for i, fn in enumerate(face_normals):
        out.append('\t\t\t*MESH_FACENORMAL %d\t%.4f\t%.4f\t%.4f' % (i, fn[0], fn[1], fn[2]))
        for v in range(3):
            out.append('\t\t\t\t*MESH_VERTEXNORMAL %d\t%.4f\t%.4f\t%.4f'
                       % (i * 3 + v, fn[0], fn[1], fn[2]))
    out += ['\t\t}', '\t}', '}']
    return "\n".join(out) + "\n"


# A triangle in the z=0 plane wound anticlockwise seen from +z, so the winding

def _smoothed(name, tris, per_face_vertex_normals):
    """Like mesh_with_geometry, but each face's three vertex normals given explicitly."""
    face_normals = [vns[0] for vns in per_face_vertex_normals]
    text = mesh_with_geometry(name, tris, face_normals)
    out = []
    face = -1
    corner = 0
    for line in text.splitlines():
        if "*MESH_FACENORMAL" in line:
            face += 1
            corner = 0
            out.append(line)
            continue
        if "*MESH_VERTEXNORMAL" in line:
            normal = per_face_vertex_normals[face][corner]
            out.append('\t\t\t\t*MESH_VERTEXNORMAL %d\t%.4f\t%.4f\t%.4f'
                       % (face * 3 + corner, normal[0], normal[1], normal[2]))
            corner += 1
            continue
        out.append(line)
    return "\n".join(out) + "\n"


# Two triangles whose winding gives (0, 0, 1) and (0, 1, 0), so a flat mesh over
# them is genuinely reproducible from the winding alone. Geometry is part of the
# fixture because that is what makes the normals redundant rather than merely flat.
_FLAT_TRIS = [((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)),
              ((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), (1.0, 0.0, 0.0))]
FLAT = mesh_with_geometry("shell", _FLAT_TRIS, [(0.0, 0.0, 1.0), (0.0, 1.0, 0.0)])
SMOOTH = _smoothed("paramedic", _FLAT_TRIS[:1],
                   [[(0.0, 0.0, 1.0), (0.1, 0.0, 0.99), (0.0, 0.1, 0.99)]])

# The old shape of the fixture: normals and nothing to check them against.
NORMALS_ONLY = mesh("headless", [((0, 0, 1), [(0, 0, 1)] * 3)])


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
        mixed = _smoothed("mixed", _FLAT_TRIS,
                          [[(0.0, 0.0, 1.0)] * 3,
                           [(0.0, 1.0, 0.0), (0.1, 0.99, 0.0), (0.0, 1.0, 0.0)]])
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


# gives (0, 0, 1).
UP_TRI = ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0))


class NormalsMustAlsoMatchTheWinding(unittest.TestCase):
    """Flat is not the same as derivable.

    A mesh is flat when its three vertex normals agree per face - that says there
    is no smoothing, and nothing about which way the face points. A sky dome is
    drawn from the inside and ships one normal pointing away from the sun; the
    goal net and the debug helpers carry authored normals unrelated to their
    winding. Dropping those does not reproduce them, it replaces them.

    Measured on what the compression pass had already rewritten: the derived
    normal matched the stripped one on 100% of faces in lowerleg.ase,
    background01.ase and test/pitch.ase, but on 26.3% of sky.ase, 18.9% of
    goals.ase, 20.4% of test.ase, 25.0% of bald.ase and 2.6% of direction.ase.
    """

    def test_a_flat_mesh_agreeing_with_its_winding_is_slimmed(self):
        text = mesh_with_geometry("ground", [UP_TRI], [(0.0, 0.0, 1.0)])
        out, stats = ase_slim.slim(text)
        self.assertEqual(stats["slimmed"], 1)
        self.assertNotIn("MESH_NORMALS", out)

    def test_a_flat_mesh_facing_against_its_winding_is_kept(self):
        # the inside-out sky dome: flat, and pointing the other way
        text = mesh_with_geometry("dome", [UP_TRI], [(0.0, 0.0, -1.0)])
        out, stats = ase_slim.slim(text)
        self.assertEqual(stats["kept"], 1)
        self.assertIn("MESH_NORMALS", out)

    def test_an_authored_normal_off_the_winding_is_kept(self):
        text = mesh_with_geometry("helper", [UP_TRI], [(0.7071, 0.0, 0.7071)])
        out, stats = ase_slim.slim(text)
        self.assertEqual(stats["kept"], 1)

    def test_one_disagreeing_face_keeps_the_whole_block(self):
        tris = [UP_TRI, ((0.0, 0.0, 1.0), (1.0, 0.0, 1.0), (0.0, 1.0, 1.0))]
        text = mesh_with_geometry("mixed", tris, [(0.0, 0.0, 1.0), (0.0, 0.0, -1.0)])
        out, stats = ase_slim.slim(text)
        self.assertEqual(stats["kept"], 1)

    def test_a_mesh_with_no_geometry_to_check_against_is_kept(self):
        """Normals with no positions cannot be shown redundant, so they stay."""
        out, stats = ase_slim.slim(NORMALS_ONLY)
        self.assertEqual(stats["kept"], 1)

    def test_a_degenerate_face_does_not_veto_the_mesh(self):
        """A zero-area triangle has no winding normal; asenormals.cpp returns zero."""
        tris = [UP_TRI, ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (2.0, 0.0, 0.0))]
        text = mesh_with_geometry("sliver", tris, [(0.0, 0.0, 1.0), (0.0, 0.0, 1.0)])
        out, stats = ase_slim.slim(text)
        self.assertEqual(stats["slimmed"], 1)

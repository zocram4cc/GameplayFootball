"""Tests for dropping the vertices no face in an ASE uses.

Measured over the 90 imported player models: 912,870 of 2,089,952 vertices - 43.7% -
are never referenced by any face, about 580 MB of the 1,328 MB the models occupy. The
worst are 83% orphaned (ateam_70214, 19,547 of 23,480). That is the stretched-triangle
drop removing faces and leaving their vertices behind.

Compacting has to keep three index spaces straight:

  MESH_VERTEX  <- MESH_FACE
  MESH_TVERT   <- MESH_TFACE   (a separate space; TFACE does not index vertices)
  MESH_NORMALS <- by face, not by vertex, so renumbering vertices leaves it alone
                  as long as the face order and count do not change

Run: python3 -m unittest test_ase_compact -v
"""

import unittest

import ase_compact


def mesh(vertices, faces, tverts=None, tfaces=None, name="m"):
    out = ['*GEOMOBJECT {', '\t*NODE_NAME "%s"' % name, '\t*MESH {',
           '\t\t*MESH_NUMVERTEX %d' % len(vertices),
           '\t\t*MESH_NUMFACES %d' % len(faces), '\t\t*MESH_VERTEX_LIST {']
    for i, v in enumerate(vertices):
        out.append('\t\t\t*MESH_VERTEX %d\t%.4f\t%.4f\t%.4f' % (i, v[0], v[1], v[2]))
    out += ['\t\t}', '\t\t*MESH_FACE_LIST {']
    for i, f in enumerate(faces):
        out.append('\t\t\t*MESH_FACE %d: A: %d B: %d C: %d AB: 1 BC: 1 CA: 1 '
                   '*MESH_SMOOTHING 1 *MESH_MTLID 0' % (i, f[0], f[1], f[2]))
    out.append('\t\t}')
    if tverts is not None:
        out.append('\t\t*MESH_NUMTVERTEX %d' % len(tverts))
        out.append('\t\t*MESH_TVERTLIST {')
        for i, t in enumerate(tverts):
            out.append('\t\t\t*MESH_TVERT %d\t%.5f\t%.5f\t0.0' % (i, t[0], t[1]))
        out += ['\t\t}', '\t\t*MESH_NUMTVFACES %d' % len(tfaces), '\t\t*MESH_TFACELIST {']
        for i, t in enumerate(tfaces):
            out.append('\t\t\t*MESH_TFACE %d\t%d\t%d\t%d' % (i, t[0], t[1], t[2]))
        out.append('\t\t}')
    out += ['\t}', '}']
    return "\n".join(out) + "\n"


# vertices 1 and 3 are used; 0, 2, 4 are orphans
ORPHANED = mesh([(0, 0, 0), (1, 0, 0), (9, 9, 9), (1, 1, 0), (8, 8, 8)],
                [(1, 3, 1)],
                tverts=[(0.1, 0.1), (0.2, 0.2), (0.3, 0.3)],
                tfaces=[(0, 2, 0)])
TIGHT = mesh([(1, 0, 0), (1, 1, 0)], [(0, 1, 0)],
             tverts=[(0.1, 0.1), (0.2, 0.2)], tfaces=[(0, 1, 0)])


def numbers(text, token):
    out = []
    for line in text.splitlines():
        if token in line:
            out.append(line.split())
    return out


class DroppingOrphans(unittest.TestCase):
    def test_the_unused_vertices_go(self):
        out, stats = ase_compact.compact(ORPHANED)
        self.assertEqual(stats["vertices_dropped"], 3)
        self.assertEqual(len(numbers(out, "*MESH_VERTEX ")), 2)

    def test_the_count_it_declares_follows(self):
        out, _ = ase_compact.compact(ORPHANED)
        line = [l for l in out.splitlines() if "*MESH_NUMVERTEX" in l][0]
        self.assertEqual(int(line.split()[-1]), 2)

    def test_the_faces_point_at_the_same_corners_afterwards(self):
        out, _ = ase_compact.compact(ORPHANED)
        # vertex 1 was (1,0,0) and 3 was (1,1,0); the face must still be that triangle
        verts = [tuple(float(x) for x in l[2:5]) for l in numbers(out, "*MESH_VERTEX ")]
        face = numbers(out, "*MESH_FACE ")[0]
        idx = [int(face[face.index("A:") + 1]), int(face[face.index("B:") + 1]),
               int(face[face.index("C:") + 1])]
        self.assertEqual([verts[i] for i in idx], [(1.0, 0.0, 0.0), (1.0, 1.0, 0.0),
                                                   (1.0, 0.0, 0.0)])

    def test_texture_vertices_are_their_own_index_space(self):
        # tvert 1 is unused by any tface and must go, without disturbing 0 and 2
        out, stats = ase_compact.compact(ORPHANED)
        self.assertEqual(stats["tverts_dropped"], 1)
        tverts = [tuple(float(x) for x in l[2:4]) for l in numbers(out, "*MESH_TVERT ")]
        self.assertEqual(tverts, [(0.1, 0.1), (0.3, 0.3)])
        tface = numbers(out, "*MESH_TFACE ")[0]
        self.assertEqual([int(x) for x in tface[2:5]], [0, 1, 0])

    def test_a_mesh_with_nothing_spare_is_returned_untouched(self):
        out, stats = ase_compact.compact(TIGHT)
        self.assertEqual((stats["vertices_dropped"], stats["tverts_dropped"]), (0, 0))
        self.assertEqual(out, TIGHT)

    def test_the_face_count_and_order_never_change(self):
        # the normals block is indexed by face, so reordering faces would break it
        out, _ = ase_compact.compact(ORPHANED)
        self.assertEqual(len(numbers(out, "*MESH_FACE ")),
                         len(numbers(ORPHANED, "*MESH_FACE ")))
        line = [l for l in out.splitlines() if "*MESH_NUMFACES" in l][0]
        self.assertEqual(int(line.split()[-1]), 1)

    def test_running_it_twice_finds_nothing_the_second_time(self):
        once, _ = ase_compact.compact(ORPHANED)
        twice, stats = ase_compact.compact(once)
        self.assertEqual(once, twice)
        self.assertEqual(stats["vertices_dropped"], 0)

    def test_each_mesh_is_compacted_on_its_own(self):
        out, stats = ase_compact.compact(ORPHANED + TIGHT)
        self.assertEqual(stats["vertices_dropped"], 3)
        self.assertEqual(len(numbers(out, "*MESH_VERTEX ")), 4)

    def test_text_with_no_meshes_comes_back_whole(self):
        self.assertEqual(ase_compact.compact("*3DSMAX_ASCIIEXPORT 200\n")[0],
                         "*3DSMAX_ASCIIEXPORT 200\n")

"""Tests for making the advertising ring read the right way round.

Rendered and read off a capture, the imported ring shows a run of boards where some
ads read normally and others are mirrored ("LESBIANS" as "SNAIBSEL"). Measured over
adboards.ase: of the 89 meshes carrying board faces, 70 read correctly throughout and
one is mirrored throughout, but 18 - the big merged runs - hold both, and those carry
about 900 of the 916 mirrored faces. Inside one of them the same UV rectangle turns up
twice with opposite handedness (nine faces at U 0.668..0.854 mirrored against nine at
0.670..0.841 readable), which is what mirroring a duplicated segment leaves behind.

PES gets away with it because it assigns the advertising faces at runtime through
bill_anime.json. This engine uses the model's own UVs, so the mirrored copies show
mirrored ads.

The fix cannot be a per-vertex one: an ASE shares one UV per vertex across every face
that uses it, and the mirrored faces sit in the same mesh as readable ones. So the UV
list is rebuilt per face corner, and only the corners of mirrored faces move.

Run: python3 -m unittest test_adboard_uvs -v
"""

import unittest

import adboard_uvs


def ase(vertices, faces, uvs, tfaces, name="board"):
    """The smallest ASE that carries one mesh's geometry and UVs."""
    out = ['*GEOMOBJECT {', '\t*NODE_NAME "%s"' % name, '\t*MESH {',
           '\t\t*MESH_NUMVERTEX %d' % len(vertices), '\t\t*MESH_NUMFACES %d' % len(faces),
           '\t\t*MESH_VERTEX_LIST {']
    for i, v in enumerate(vertices):
        out.append('\t\t\t*MESH_VERTEX %d\t%.4f\t%.4f\t%.4f' % (i, v[0], v[1], v[2]))
    out.append('\t\t}')
    out.append('\t\t*MESH_FACE_LIST {')
    for i, f in enumerate(faces):
        out.append('\t\t\t*MESH_FACE %d: A: %d B: %d C: %d AB: 1 BC: 1 CA: 1 '
                   '*MESH_SMOOTHING 1 *MESH_MTLID 0' % (i, f[0], f[1], f[2]))
    out.append('\t\t}')
    out.append('\t\t*MESH_NUMTVERTEX %d' % len(uvs))
    out.append('\t\t*MESH_TVERTLIST {')
    for i, uv in enumerate(uvs):
        out.append('\t\t\t*MESH_TVERT %d\t%.5f\t%.5f\t0.0' % (i, uv[0], uv[1]))
    out.append('\t\t}')
    out.append('\t\t*MESH_NUMTVFACES %d' % len(tfaces))
    out.append('\t\t*MESH_TFACELIST {')
    for i, tf in enumerate(tfaces):
        out.append('\t\t\t*MESH_TFACE %d\t%d\t%d\t%d' % (i, tf[0], tf[1], tf[2]))
    out.append('\t\t}')
    out += ['\t}', '}']
    return "\n".join(out) + "\n"


# A far-touchline board facing the pitch (normal -y), with its ad the right way round
READABLE = ase([(0.0, 40.0, 0.0), (2.0, 40.0, 0.0), (2.0, 40.0, 1.0)],
               [(0, 1, 2)], [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0)], [(0, 1, 2)])
# the same board with its UVs mirrored
MIRRORED = ase([(0.0, 40.0, 0.0), (2.0, 40.0, 0.0), (2.0, 40.0, 1.0)],
               [(0, 1, 2)], [(1.0, 1.0), (0.0, 1.0), (0.0, 0.0)], [(0, 1, 2)])


class Reading(unittest.TestCase):
    def test_a_readable_board_is_left_exactly_as_it_was(self):
        fixed, stats = adboard_uvs.normalise(READABLE)
        self.assertEqual(stats["mirrored"], 0)
        self.assertEqual(fixed, READABLE)

    def test_a_mirrored_board_is_turned_back(self):
        fixed, stats = adboard_uvs.normalise(MIRRORED)
        self.assertEqual(stats["mirrored"], 1)
        self.assertEqual(adboard_uvs.normalise(fixed)[1]["mirrored"], 0)

    def test_the_geometry_is_untouched(self):
        fixed, _ = adboard_uvs.normalise(MIRRORED)
        for line in MIRRORED.splitlines():
            if "MESH_VERTEX " in line or "MESH_FACE " in line:
                self.assertIn(line, fixed.splitlines())

    def test_v_is_left_alone_so_the_ad_stays_the_right_way_up(self):
        fixed, _ = adboard_uvs.normalise(MIRRORED)
        vs = sorted(round(float(l.split()[3]), 5) for l in fixed.splitlines()
                    if "*MESH_TVERT " in l)
        self.assertEqual(vs, [0.0, 1.0, 1.0])


class MixingInsideOneMesh(unittest.TestCase):
    """The case that forces the UV list to be rebuilt per corner.

    Two panels in one mesh sharing vertex 1: one reads correctly, one mirrored. A
    per-vertex fix would have to move a UV that the readable panel also uses.
    """

    # Vertex 1 is the right edge of the readable panel at U 0.5 and, once the second
    # panel is turned back, its own corner wants U 0.1. One vertex cannot hold both,
    # which is the whole reason the list is rebuilt per corner.
    MIXED = ase(
        [(0.0, 40.0, 0.0), (2.0, 40.0, 0.0), (2.0, 40.0, 1.0), (4.0, 40.0, 0.0),
         (4.0, 40.0, 1.0)],
        [(0, 1, 2), (1, 3, 4)],
        [(0.0, 1.0), (0.5, 1.0), (0.5, 0.0), (0.1, 1.0), (0.1, 0.0)],
        [(0, 1, 2), (1, 3, 4)])

    def test_only_the_mirrored_panel_moves(self):
        fixed, stats = adboard_uvs.normalise(self.MIXED)
        self.assertEqual(stats["mirrored"], 1)
        self.assertEqual(stats["readable"], 1)
        self.assertEqual(adboard_uvs.normalise(fixed)[1]["mirrored"], 0)

    def test_the_readable_panel_keeps_the_uvs_it_had(self):
        fixed, _ = adboard_uvs.normalise(self.MIXED)
        uvs = [(round(float(l.split()[2]), 5), round(float(l.split()[3]), 5))
               for l in fixed.splitlines() if "*MESH_TVERT " in l]
        tface = [tuple(map(int, l.split()[2:5])) for l in fixed.splitlines()
                 if "*MESH_TFACE " in l][0]
        self.assertEqual([uvs[i] for i in tface], [(0.0, 1.0), (0.5, 1.0), (0.5, 0.0)])

    def test_the_shared_vertex_ends_up_with_a_uv_per_panel(self):
        fixed, _ = adboard_uvs.normalise(self.MIXED)
        uvs = [(round(float(l.split()[2]), 5), round(float(l.split()[3]), 5))
               for l in fixed.splitlines() if "*MESH_TVERT " in l]
        tfaces = [tuple(map(int, l.split()[2:5])) for l in fixed.splitlines()
                  if "*MESH_TFACE " in l]
        # its corner in the readable panel is 0.5; in the panel that was turned back
        # it is 0.1, and the two are separate entries
        self.assertEqual(uvs[tfaces[0][1]], (0.5, 1.0))
        self.assertEqual(uvs[tfaces[1][0]], (0.1, 1.0))
        self.assertNotEqual(tfaces[0][1], tfaces[1][0])

    def test_the_tvert_count_matches_the_list_it_declares(self):
        fixed, _ = adboard_uvs.normalise(self.MIXED)
        declared = int([l for l in fixed.splitlines() if "*MESH_NUMTVERTEX" in l][0].split()[-1])
        listed = len([l for l in fixed.splitlines() if "*MESH_TVERT " in l])
        self.assertEqual(declared, listed)

    def test_every_tface_index_is_inside_the_list(self):
        fixed, _ = adboard_uvs.normalise(self.MIXED)
        listed = len([l for l in fixed.splitlines() if "*MESH_TVERT " in l])
        for line in fixed.splitlines():
            if "*MESH_TFACE " in line:
                for index in map(int, line.split()[2:5]):
                    self.assertLess(index, listed)


class LeavingAloneWhatHasNoTextToRead(unittest.TestCase):
    def test_a_flat_face_is_not_judged(self):
        flat = ase([(0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (2.0, 2.0, 0.0)],
                   [(0, 1, 2)], [(1.0, 1.0), (0.0, 1.0), (0.0, 0.0)], [(0, 1, 2)])
        fixed, stats = adboard_uvs.normalise(flat)
        self.assertEqual(stats["mirrored"], 0)
        self.assertEqual(fixed, flat)

    def test_a_mesh_with_no_uvs_at_all_survives(self):
        bare = ase([(0.0, 40.0, 0.0), (2.0, 40.0, 0.0), (2.0, 40.0, 1.0)],
                   [(0, 1, 2)], [], [])
        fixed, stats = adboard_uvs.normalise(bare)
        self.assertEqual(stats["mirrored"], 0)
        self.assertEqual(fixed, bare)

    def test_text_with_no_meshes_is_returned_whole(self):
        self.assertEqual(adboard_uvs.normalise("*3DSMAX_ASCIIEXPORT 200\n")[0],
                         "*3DSMAX_ASCIIEXPORT 200\n")


if __name__ == "__main__":
    unittest.main()

"""Tests for the touchline staff PES keeps in its common package.

A stadium pack does not carry the people standing beside the pitch. PES keeps one
copy in Asset/model/bg/common/staff and gives it to every ground - and the 4cc
mod replaces the skins, which is why the reference broadcast has characters
rather than coaches in club coats. Planet Namek's own staff pack is one of the
48-byte empty overrides, so without the common set there is nobody on the
touchline at all.

The models are single-mesh, about 2.1 m tall and rigged, but they stand still, so
they import as static geometry in their bind pose. Placement is the part worth
getting right: they belong in the technical areas, outside the touchline, facing
the pitch.

Run: python3 -m unittest test_stadium_staff -v
"""

import re
import math
import unittest

import stadium_staff


HALF_X = 55.0  # gametypes.hpp: x runs goal to goal
HALF_Y = 36.0  # y touchline to touchline


class DressedFirst(unittest.TestCase):
    """Which of a pack's staff models to actually put on the touchline.

    A stadium pack ships more staff models than there is room for - st002 has 61 -
    and takes them alphabetically, which is how twenty figures came out plain
    white: the first eight in the alphabet wear PES's stock coach skins
    (ca_blou2018_band_bsm, gu_spain_bsm), and no archive on hand carries those,
    while the ones the 4cc author actually dressed (staff_campesina,
    staff_doomyuri1) sit further down the list. Prefer the models whose skins the
    pack ships; nobody is dropped, because a white figure still beats an empty
    touchline.
    """

    def test_a_model_with_its_skin_comes_before_one_without(self):
        order = stadium_staff.dressed_first(
            ["coach.fmdl", "fairy.fmdl"],
            {"coach.fmdl": ["gu_spain_bsm"], "fairy.fmdl": ["staff_evilfairy"]},
            {"staff_evilfairy"})
        self.assertEqual(order, ["fairy.fmdl", "coach.fmdl"])

    def test_nobody_is_dropped(self):
        models = ["a.fmdl", "b.fmdl", "c.fmdl"]
        order = stadium_staff.dressed_first(models, {m: ["missing"] for m in models}, set())
        self.assertEqual(sorted(order), sorted(models))

    def test_the_order_within_a_group_is_kept(self):
        models = ["a.fmdl", "b.fmdl", "c.fmdl", "d.fmdl"]
        skins = {"a.fmdl": ["x"], "b.fmdl": ["have"], "c.fmdl": ["y"], "d.fmdl": ["have"]}
        self.assertEqual(stadium_staff.dressed_first(models, skins, {"have"}),
                         ["b.fmdl", "d.fmdl", "a.fmdl", "c.fmdl"])

    def test_a_model_half_dressed_counts_as_undressed(self):
        # a figure with one bare mesh is the same white patch as a bare figure
        order = stadium_staff.dressed_first(
            ["half.fmdl", "whole.fmdl"],
            {"half.fmdl": ["have", "missing"], "whole.fmdl": ["have"]},
            {"have"})
        self.assertEqual(order, ["whole.fmdl", "half.fmdl"])

    def test_a_model_with_no_textures_at_all_is_undressed(self):
        order = stadium_staff.dressed_first(
            ["bare.fmdl", "dressed.fmdl"],
            {"bare.fmdl": [], "dressed.fmdl": ["have"]},
            {"have"})
        self.assertEqual(order, ["dressed.fmdl", "bare.fmdl"])


class OnlyTheDressedStandThere(unittest.TestCase):
    """Undressed models are left out rather than stood on the touchline.

    Six of the nine grounds converted have no staff of their own and borrow PES's
    shared set, whose skins (ca_blou2018_band_bsm, gu_spain_bsm, pr_cset000_bsm)
    are in none of the archives here. They came out as eight white mannequins
    beside the pitch, in every wide shot, on every ground. An empty technical area
    is not a defect; a row of blank white figures is.
    """

    def test_a_ground_with_dressed_models_uses_only_those(self):
        self.assertEqual(
            stadium_staff.only_dressed(["coach.fmdl", "fairy.fmdl"],
                                       {"coach.fmdl": ["missing"], "fairy.fmdl": ["have"]},
                                       {"have"}),
            ["fairy.fmdl"])

    def test_a_ground_with_none_dressed_gets_nobody(self):
        self.assertEqual(
            stadium_staff.only_dressed(["a.fmdl", "b.fmdl"],
                                       {"a.fmdl": ["missing"], "b.fmdl": []}, {"have"}),
            [])

    def test_the_order_is_kept(self):
        models = ["a.fmdl", "b.fmdl", "c.fmdl"]
        skins = {m: ["have"] for m in models}
        self.assertEqual(stadium_staff.only_dressed(models, skins, {"have"}), models)


class Placements(unittest.TestCase):
    def setUp(self):
        self.spots = stadium_staff.placements(HALF_X, HALF_Y, per_side=4)

    def test_there_is_somebody_on_each_touchline(self):
        self.assertTrue(any(y > 0 for _x, y, _yaw in self.spots))
        self.assertTrue(any(y < 0 for _x, y, _yaw in self.spots))

    def test_nobody_stands_on_the_pitch(self):
        for x, y, _yaw in self.spots:
            self.assertGreater(abs(y), HALF_Y, "a figure at y=%.1f is on the pitch" % y)

    def test_nobody_wanders_off_into_the_stands(self):
        for x, y, _yaw in self.spots:
            self.assertLess(abs(y), HALF_Y + 6.0)
            self.assertLess(abs(x), HALF_X)

    def test_they_stand_in_the_technical_areas_beside_the_halfway_line(self):
        for x, _y, _yaw in self.spots:
            self.assertLess(abs(x), 20.0)

    def test_they_face_the_pitch(self):
        # yaw is the engine's: a figure at yaw a faces (sin a, -cos a)
        for _x, y, yaw in self.spots:
            facing_y = -math.cos(yaw)
            self.assertGreater(facing_y * (0.0 - y), 0.0,
                               "a figure at y=%.1f faces away from the pitch" % y)

    def test_asking_for_more_puts_more_on_each_side(self):
        self.assertEqual(len(stadium_staff.placements(HALF_X, HALF_Y, per_side=6)), 12)
        self.assertEqual(len(stadium_staff.placements(HALF_X, HALF_Y, per_side=0)), 0)

    def test_they_are_spread_out_rather_than_stacked(self):
        xs = sorted(x for x, y, _yaw in self.spots if y > 0)
        for a, b in zip(xs, xs[1:]):
            self.assertGreater(b - a, 1.0)


class Centring(unittest.TestCase):
    """The models do not stand on their own origin."""

    def test_a_figure_is_centred_on_its_mark(self):
        # A mesh whose footprint sits four metres in front of its origin - which
        # is what PES's staff models do - put half the row on the pitch when the
        # origin was dropped on the mark.
        mesh = [(-0.4, 3.6, 0.0), (0.4, 4.4, 0.0), (0.0, 4.0, 2.1)]
        offset = stadium_staff.footprint_offset(mesh)
        self.assertAlmostEqual(offset[0], 0.0, places=4)
        self.assertAlmostEqual(offset[1], -4.0, places=4)

    def test_the_ground_is_left_where_it_is(self):
        mesh = [(0.0, 0.0, 0.0), (0.0, 0.0, 2.1)]
        self.assertEqual(len(stadium_staff.footprint_offset(mesh)), 2)

    def test_a_centred_mesh_is_not_moved(self):
        mesh = [(-1.0, -1.0, 0.0), (1.0, 1.0, 2.0)]
        offset = stadium_staff.footprint_offset(mesh)
        self.assertAlmostEqual(offset[0], 0.0, places=4)
        self.assertAlmostEqual(offset[1], 0.0, places=4)

    def test_a_row_of_figures_stays_off_the_pitch(self):
        # the whole point: with the offset applied, nobody crosses the touchline
        mesh = [(-0.4, 3.6, 0.0), (0.4, 4.4, 0.0)]
        offset = stadium_staff.footprint_offset(mesh)
        for _x, y, yaw in stadium_staff.placements(HALF_X, HALF_Y, per_side=4):
            for vertex in mesh:
                moved = stadium_staff.place_vertex(
                    (vertex[0] + offset[0], vertex[1] + offset[1], vertex[2]), (0.0, y), yaw)
                self.assertGreater(abs(moved[1]), HALF_Y,
                                   "a figure reaches y=%.2f, inside the touchline" % moved[1])


class Clearance(unittest.TestCase):
    """Set out by its own size, so nothing reaches over the line."""

    def test_a_slim_figure_stands_close_to_the_line(self):
        y = stadium_staff.mark_for_depth(1.0, 0.8, HALF_Y)
        self.assertGreater(y, HALF_Y)
        self.assertLess(y, HALF_Y + 3.0)

    def test_a_camera_crane_is_set_further_back(self):
        slim = stadium_staff.mark_for_depth(1.0, 0.8, HALF_Y)
        crane = stadium_staff.mark_for_depth(1.0, 6.0, HALF_Y)
        self.assertGreater(crane, slim)

    def test_nothing_reaches_over_the_line(self):
        for depth in (0.6, 1.4, 3.0, 6.0):
            y = stadium_staff.mark_for_depth(1.0, depth, HALF_Y)
            self.assertGreaterEqual(y - depth / 2.0, HALF_Y)

    def test_the_far_touchline_is_the_mirror_of_the_near_one(self):
        self.assertAlmostEqual(stadium_staff.mark_for_depth(-1.0, 2.0, HALF_Y),
                               -stadium_staff.mark_for_depth(1.0, 2.0, HALF_Y), places=4)


class Transform(unittest.TestCase):
    def test_a_figure_is_turned_then_moved(self):
        # The engine's convention: a figure at yaw a faces (sin a, -cos a), so
        # the model's own forward is -y. Turned a quarter turn it faces +x, and a
        # point a metre ahead of it lands a metre along +x from its mark.
        out = stadium_staff.place_vertex((0.0, -1.0, 0.0), (10.0, -37.0), math.pi / 2)
        self.assertAlmostEqual(out[0], 11.0, places=4)
        self.assertAlmostEqual(out[1], -37.0, places=4)

    def test_height_is_left_alone(self):
        out = stadium_staff.place_vertex((0.0, 0.0, 1.8), (5.0, 37.0), 1.2)
        self.assertAlmostEqual(out[2], 1.8, places=4)

    def test_the_origin_lands_on_its_mark(self):
        out = stadium_staff.place_vertex((0.0, 0.0, 0.0), (-3.0, 37.5), 0.4)
        self.assertAlmostEqual(out[0], -3.0, places=4)
        self.assertAlmostEqual(out[1], 37.5, places=4)


if __name__ == "__main__":
    unittest.main()


class ABannerLyingOnThePitch(unittest.TestCase):
    """PES's centre-circle banner came out a dark disc on the grass.

    Its four flag faces are flat, they lie a few centimetres above the pitch, and
    their winding gives a geometric normal of -0.99 in z: they face straight down.
    PES does not mind - it draws them from its own material - but this engine
    derives a mesh's lighting from the winding that was written, so the printed
    side of the banner was lit as though it faced away from the sky and came out
    black, competition emblem and all.

    A flat thing lying on a pitch is there to be seen from above, so its winding is
    reversed on the way in. Only that case: a wall is flat too, and a solid has
    faces pointing every way.
    """

    def _quad(self, up):
        # a metre of ground, wound so its normal points up or down
        v = [(0.0, 0.0, 0.02), (1.0, 0.0, 0.02), (1.0, 1.0, 0.02), (0.0, 1.0, 0.02)]
        f = [(0, 1, 2), (0, 2, 3)] if up else [(2, 1, 0), (3, 2, 0)]
        return v, f

    def test_a_face_up_banner_is_left_alone(self):
        v, f = self._quad(up=True)
        self.assertFalse(stadium_staff.wants_winding_flipped(v, f))

    def test_a_face_down_banner_is_turned_over(self):
        v, f = self._quad(up=False)
        self.assertTrue(stadium_staff.wants_winding_flipped(v, f))

    def test_a_wall_is_left_alone(self):
        # flat, but vertical: which way it faces is the model's business
        v = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 0.0, 2.0), (0.0, 0.0, 2.0)]
        for f in ([(0, 1, 2), (0, 2, 3)], [(2, 1, 0), (3, 2, 0)]):
            self.assertFalse(stadium_staff.wants_winding_flipped(v, f))

    def test_a_solid_is_left_alone(self):
        # a closed box: its faces point every way, so there is nothing to turn over
        v = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
             (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]
        f = [(0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6), (0, 4, 5), (0, 5, 1),
             (2, 6, 7), (2, 7, 3), (1, 5, 6), (1, 6, 2), (0, 3, 7), (0, 7, 4)]
        self.assertFalse(stadium_staff.wants_winding_flipped(v, f))

    def test_the_big_faces_decide_it(self):
        # A banner with a tiny upward-facing tab does not stop being a banner: the
        # judgement is by area, not by face count.
        v = [(0.0, 0.0, 0.02), (4.0, 0.0, 0.02), (4.0, 4.0, 0.02), (0.0, 4.0, 0.02),
             (0.0, 0.0, 0.05), (0.1, 0.0, 0.05), (0.1, 0.1, 0.05)]
        f = [(2, 1, 0), (3, 2, 0), (4, 5, 6)]
        self.assertTrue(stadium_staff.wants_winding_flipped(v, f))

    def test_nothing_is_left_alone(self):
        self.assertFalse(stadium_staff.wants_winding_flipped([], []))


class _Vec:
    def __init__(self, x, y, z):
        self.x, self.y, self.z = x, y, z


class _UV:
    def __init__(self, u, v):
        self.u, self.v = u, v


class _Vertex:
    def __init__(self, pos, uv):
        self.position = _Vec(*pos)
        self.uv = [_UV(*uv)]


class _Face:
    def __init__(self, vertices):
        self.vertices = vertices


class _Mesh:
    def __init__(self, corners, tris):
        self.vertices = [_Vertex(p, uv) for p, uv in corners]
        self.faces = [_Face([self.vertices[i] for i in tri]) for tri in tris]


def _figure_text(mesh):
    import io
    out = io.StringIO()
    stadium_staff._write_figure(out, "thing", 0, mesh, (0.0, 0.0), 0.0, off_pitch=False)
    return out.getvalue()


class UVsAreWrittenPerFaceCorner(unittest.TestCase):
    """One UV per vertex cannot express a sheet pair sharing positions.

    The corner flag's cloth is two sheets that share 9 of their 18 positions, so a
    back-face corner's UV overwrote the front's and the back sampled the wrong half
    of cf_common_bsm. A TFACE indexing the vertex list can only ever give a shared
    position one UV, so the list is written per face corner instead - the same thing
    adboard_uvs.py does for the advertising ring.
    """

    def setUp(self):
        # two triangles sharing an edge, and disagreeing about that edge's UV
        corners = [((0.0, 0.0, 1.0), (0.0, 0.0)), ((1.0, 0.0, 1.0), (1.0, 0.0)),
                   ((0.0, 1.0, 1.0), (0.0, 1.0)), ((1.0, 1.0, 1.0), (1.0, 1.0))]
        self.mesh = _Mesh(corners, [(0, 1, 2), (1, 3, 2)])
        self.text = _figure_text(self.mesh)

    def test_there_is_one_tvert_per_face_corner(self):
        self.assertIn("*MESH_NUMTVERTEX 6", self.text)     # 2 faces x 3 corners
        self.assertIn("*MESH_NUMTVFACES 2", self.text)

    def test_each_tface_indexes_its_own_three_tverts(self):
        self.assertIn("*MESH_TFACE 0\t0\t1\t2", self.text)
        self.assertIn("*MESH_TFACE 1\t3\t4\t5", self.text)

    def test_the_uvs_still_belong_to_the_right_corners(self):
        """Per-corner indexing must not shuffle which art lands where."""
        tverts = re.findall(r'\*MESH_TVERT \d+\t([-\d.]+)\t([-\d.]+)', self.text)
        got = [(float(u), float(v)) for u, v in tverts]
        # v is flipped on the way in, so (0,0) becomes (0,1)
        self.assertEqual(got[0], (0.0, 1.0))
        self.assertEqual(len(got), 6)

    def test_the_vertex_list_is_untouched(self):
        """Positions stay shared; only the UV pool is unwelded."""
        self.assertIn("*MESH_NUMVERTEX 4", self.text)
        self.assertIn("*MESH_NUMFACES 2", self.text)

    def test_a_mesh_with_no_uvs_still_writes_a_tvert_per_corner(self):
        mesh = _Mesh([((0.0, 0.0, 1.0), (0.0, 0.0))] * 3, [(0, 1, 2)])
        for vertex in mesh.vertices:
            vertex.uv = []
        text = _figure_text(mesh)
        self.assertIn("*MESH_NUMTVERTEX 3", text)
        self.assertIn("*MESH_TFACE 0\t0\t1\t2", text)

"""Tests for the furniture PES stands around a pitch.

A stadium pack ships none of it. PES keeps one set in its own packages and hands
it to every ground - which is why every 4cc pack's audi, cheer, standsFlag,
scarecrow and tv sub-packs are 48-byte stubs - so the corner flags, the fourth
official's board, the television cameras, the barrier at the tunnel mouth and the
paramedics all live in base PES21 (dt12_g4.cpk: common/demo/prop and
common/demo/fixdemoobj). This engine has never had any of it: there is not even a
corner flag in media/objects.

What can be imported is decided by whether the skins are installed. The objects
are complete - gadget_cornerflag is 263 vertices and fully textured - while the
stock-kit humans (stewards, press, the television crew) ask for clothing textures
no archive here carries, the same gap as the staff coach kit, so they are left out
rather than stood there white.

Placement is this module's job: PES positions them from its demo data, which is
authored per stadium, and what carries over is where they belong on a football
pitch. Corner flags go on the corners, cameras outside the perimeter looking in,
the board and the medics in the technical areas, the barrier along the tunnel
side - which for this engine is -y, the touchline the walk-on comes in over.

Run: python3 -m unittest test_stadium_props -v
"""

import math
import unittest

import stadium_props


HALF_X = 55.0  # gametypes.hpp
HALF_Y = 36.0


def _faces(mark, target=(0.0, 0.0)):
    """Whether a mark's yaw points it at `target` (a figure at yaw a faces (sin a, -cos a))."""
    x, y, yaw = mark
    facing = (math.sin(yaw), -math.cos(yaw))
    towards = (target[0] - x, target[1] - y)
    length = math.hypot(*towards) or 1.0
    towards = (towards[0] / length, towards[1] / length)
    return facing[0] * towards[0] + facing[1] * towards[1] > 0.9


class CornerFlags(unittest.TestCase):
    def test_there_are_four_of_them(self):
        self.assertEqual(len(stadium_props.corner_flag_marks(HALF_X, HALF_Y)), 4)

    def test_they_stand_on_the_corners(self):
        for x, y, _yaw in stadium_props.corner_flag_marks(HALF_X, HALF_Y):
            self.assertAlmostEqual(abs(x), HALF_X, places=6)
            self.assertAlmostEqual(abs(y), HALF_Y, places=6)

    def test_every_corner_gets_one(self):
        corners = {(x > 0, y > 0) for x, y, _ in stadium_props.corner_flag_marks(HALF_X, HALF_Y)}
        self.assertEqual(len(corners), 4)


class Cameras(unittest.TestCase):
    def setUp(self):
        self.marks = stadium_props.camera_marks(HALF_X, HALF_Y)

    def test_no_camera_stands_on_the_pitch(self):
        for x, y, _yaw in self.marks:
            self.assertTrue(abs(x) > HALF_X or abs(y) > HALF_Y,
                            "camera at %.1f, %.1f is on the field" % (x, y))

    def test_they_all_look_at_the_middle(self):
        for mark in self.marks:
            self.assertTrue(_faces(mark), "camera at %.1f, %.1f faces away" % mark[:2])

    def test_there_is_one_behind_each_goal(self):
        behind = [m for m in self.marks if abs(m[0]) > HALF_X]
        self.assertGreaterEqual(len(behind), 2)
        self.assertEqual(len({m[0] > 0 for m in behind}), 2)

    def test_and_one_on_the_far_touchline(self):
        # the main broadcast camera side, away from the tunnel
        far = [m for m in self.marks if m[1] > HALF_Y]
        self.assertTrue(far)


class TechnicalAreas(unittest.TestCase):
    def setUp(self):
        self.marks = stadium_props.technical_area_marks(HALF_X, HALF_Y)

    def test_there_is_one_either_side_of_the_halfway_line(self):
        self.assertEqual(len(self.marks), 2)
        self.assertEqual(len({m[0] > 0 for m in self.marks}), 2)

    def test_they_are_off_the_pitch_on_the_tunnel_side(self):
        for x, y, _yaw in self.marks:
            self.assertLess(y, -HALF_Y)
            self.assertLess(abs(x), 25.0)

    def test_they_face_the_pitch(self):
        for mark in self.marks:
            self.assertTrue(_faces(mark))


class Barrier(unittest.TestCase):
    def test_it_lies_along_the_tunnel_touchline(self):
        x, y, yaw = stadium_props.barrier_mark(HALF_X, HALF_Y)
        self.assertLess(y, -HALF_Y)
        # a 25 m barrier runs along the line, not across it: it faces the pitch
        self.assertTrue(_faces((x, y, yaw)))


class ClearanceOfEverything(unittest.TestCase):
    """Nothing but the corner flags may touch the field of play."""

    def test_no_mark_of_any_kind_lands_inside_the_lines(self):
        marks = (stadium_props.camera_marks(HALF_X, HALF_Y)
                 + stadium_props.technical_area_marks(HALF_X, HALF_Y)
                 + [stadium_props.barrier_mark(HALF_X, HALF_Y)])
        for x, y, _yaw in marks:
            self.assertTrue(abs(x) > HALF_X or abs(y) > HALF_Y, "%.1f, %.1f is on the field" % (x, y))


class WhichPropsToTake(unittest.TestCase):
    """The set is named rather than swept up, so a cup ceremony does not arrive."""

    def test_the_touchline_set_is_what_a_match_has(self):
        wanted = stadium_props.WANTED
        self.assertIn("gadget_cornerflag", wanted)
        self.assertIn("substitute_board_cmn", wanted)
        self.assertIn("doh_beltpole", wanted)

    def test_no_trophies_or_gadgets_from_the_cutscenes(self):
        for name in stadium_props.WANTED:
            self.assertFalse(name.startswith("cup_"), name)
            self.assertFalse(name.startswith("trophy_"), name)
            self.assertFalse(name.startswith("news_"), name)

    def test_a_prop_is_matched_by_its_own_file_name(self):
        self.assertEqual(stadium_props.prop_role("/x/y/gadget_cornerflag.fmdl"), "cornerflag")
        self.assertEqual(stadium_props.prop_role("/x/y/mob_prop_tvcamera01.fmdl"), "camera")
        self.assertEqual(stadium_props.prop_role("/x/y/substitute_board_cmn.fmdl"), "bench")
        self.assertEqual(stadium_props.prop_role("/x/y/dm_medicalstaff_01.fmdl"), "bench")
        self.assertEqual(stadium_props.prop_role("/x/y/doh_beltpole.fmdl"), "barrier")
        self.assertIsNone(stadium_props.prop_role("/x/y/cup_uefa_euro_hi.fmdl"))


class OnePropPerMark(unittest.TestCase):
    """Three camera models and three camera marks make three cameras, not nine.

    Taking every model to every mark stood a broadcast camera, a hand camera and a
    video camera on the same spot behind each goal.
    """

    def test_the_models_are_shared_out_over_the_marks(self):
        pairs = stadium_props.assign(["a", "b", "c"], [(0, 0, 0), (1, 0, 0), (2, 0, 0)])
        self.assertEqual([p[0] for p in pairs], ["a", "b", "c"])

    def test_one_model_fills_every_mark(self):
        pairs = stadium_props.assign(["flag"], [(0, 0, 0), (1, 0, 0), (2, 0, 0), (3, 0, 0)])
        self.assertEqual(len(pairs), 4)
        self.assertEqual({p[0] for p in pairs}, {"flag"})

    def test_more_models_than_marks_leaves_the_extras_out(self):
        pairs = stadium_props.assign(["a", "b", "c"], [(0, 0, 0)])
        self.assertEqual(len(pairs), 1)

    def test_no_marks_places_nothing(self):
        self.assertEqual(stadium_props.assign(["a"], []), [])


class SittingOnTheGround(unittest.TestCase):
    """PES hangs some props off their attach point rather than standing them up.

    Measured: gadget_cornerflag runs z -0.07..1.57 and the sub board -0.03..0.39,
    both effectively on the ground, but mob_prop_tvcamera01 runs -0.42..0.82 and
    mob_prop_camera00 -1.63..0.09 - a camera that would be half buried and one
    that would be entirely underground. Every piece is set down on the grass.
    """

    class _Point(object):
        def __init__(self, x, y, z):
            self.x, self.y, self.z = x, y, z

    class _UV(object):
        u = 0.0
        v = 0.0

    class _Vertex(object):
        def __init__(self, x, y, z):
            self.position = SittingOnTheGround._Point(x, y, z)
            self.uv = [SittingOnTheGround._UV()]

    class _Face(object):
        def __init__(self, vertices):
            self.vertices = vertices

    def _mesh(self, low, high):
        # Fox axes: y is up
        verts = [self._Vertex(x, y, z) for x in (-0.2, 0.2) for y in (low, high) for z in (-0.2, 0.2)]

        class Mesh(object):
            pass
        mesh = Mesh()
        mesh.vertices = verts
        mesh.faces = [self._Face(verts[:3])]
        return mesh

    def _written_z(self, low, high):
        import io
        out = io.StringIO()
        stadium_props.write_prop(out, "prop", 0, self._mesh(low, high), (0.0, -40.0), 0.0)
        zs = [float(line.split()[4]) for line in out.getvalue().splitlines()
              if "*MESH_VERTEX " in line]
        return (min(zs), max(zs))

    def test_a_prop_hanging_below_its_origin_is_stood_up(self):
        low, _high = self._written_z(-1.63, 0.09)
        self.assertAlmostEqual(low, 0.0, places=3)

    def test_its_height_is_not_changed(self):
        low, high = self._written_z(-1.63, 0.09)
        self.assertAlmostEqual(high - low, 1.72, places=3)

    def test_one_already_standing_is_left_where_it_is(self):
        low, _high = self._written_z(0.0, 1.5)
        self.assertAlmostEqual(low, 0.0, places=3)

    def test_a_hair_below_the_grass_is_lifted_onto_it(self):
        low, _high = self._written_z(-0.07, 1.57)
        self.assertAlmostEqual(low, 0.0, places=3)


class PlacedWhereTheMarkIs(unittest.TestCase):
    """A prop stands on its mark, not pushed off the pitch like a coach.

    The staff writer sets every figure out past the touchline by its own depth,
    which is right for people on a touchline and wrong for everything else: it
    would take the corner flags off the corners, and a camera meant to sit six
    metres behind a goal at (61, 0) would land at (61, 38), out by the halfway
    line.
    """

    class _Point(object):
        def __init__(self, x, y, z):
            self.x, self.y, self.z = x, y, z

    class _Vertex(object):
        def __init__(self, x, y, z):
            self.position = PlacedWhereTheMarkIs._Point(x, y, z)
            self.uv = [PlacedWhereTheMarkIs._UV()]

    class _UV(object):
        u = 0.0
        v = 0.0

    class _Face(object):
        def __init__(self, vertices):
            self.vertices = vertices

    class _Mesh(object):
        def __init__(self):
            # a half-metre post standing on its own origin, in Fox axes (y up)
            self.vertices = [PlacedWhereTheMarkIs._Vertex(x, z, y)
                             for x in (-0.25, 0.25) for y in (-0.25, 0.25) for z in (0.0, 1.5)]
            self.faces = [PlacedWhereTheMarkIs._Face(self.vertices[:3])]

    def _written_centre(self, mark):
        import io
        import stadium_staff
        out = io.StringIO()
        stadium_props.write_prop(out, "prop", 0, self._Mesh(), (mark[0], mark[1]), mark[2])
        xs, ys = [], []
        for line in out.getvalue().splitlines():
            if "*MESH_VERTEX " in line:
                parts = line.split()
                xs.append(float(parts[2]))
                ys.append(float(parts[3]))
        return ((min(xs) + max(xs)) / 2.0, (min(ys) + max(ys)) / 2.0)

    def test_a_corner_flag_lands_on_its_corner(self):
        x, y = self._written_centre((HALF_X, -HALF_Y, 0.0))
        self.assertAlmostEqual(x, HALF_X, places=3)
        self.assertAlmostEqual(y, -HALF_Y, places=3)

    def test_a_camera_behind_the_goal_stays_behind_the_goal(self):
        x, y = self._written_centre((61.0, 0.0, 0.0))
        self.assertAlmostEqual(x, 61.0, places=3)
        self.assertAlmostEqual(y, 0.0, places=3)

    def test_the_tunnel_side_marks_are_kept_as_given(self):
        x, y = self._written_centre((12.0, -38.5, 0.0))
        self.assertAlmostEqual(x, 12.0, places=3)
        self.assertAlmostEqual(y, -38.5, places=3)


class TheEntranceSet(unittest.TestCase):
    """What PES carries out for the walkout and takes away again.

    doh_fb_home/away are the flag bearers (10.9k vertices, dressed),
    banner_nationalflag_home/away and banner_euro_competition the banners they hold,
    tunnelarch_uefa_euro the arch over the tunnel mouth, circleflag_afc_cl_01 the
    pennant display on the centre circle, and passage_01..99 the tunnel itself.
    None of it belongs on the pitch once the match starts, so it is a separate set
    the engine drops at kickoff.
    """

    def test_the_flag_bearers_and_their_banners_are_in_it(self):
        self.assertIn("doh_fb_home", stadium_props.ENTRANCE_WANTED)
        self.assertIn("banner_nationalflag_home", stadium_props.ENTRANCE_WANTED)

    def test_so_is_the_arch(self):
        self.assertIn("tunnelarch_uefa_euro", stadium_props.ENTRANCE_WANTED)

    def test_the_competition_pennant_display_is_not_in_it_by_default(self):
        # circleflag is what PES rings the centre circle with for a continental
        # tie; a 4cc broadcast has no such thing, and it sat there through every
        # wide beat of the presentation
        self.assertNotIn("circleflag_afc_cl_01", stadium_props.ENTRANCE_WANTED)
        self.assertIn("circleflag_afc_cl_01", stadium_props.COMPETITION_EXTRAS)

    def test_the_touchline_furniture_is_not(self):
        # it stays out for the whole match
        self.assertNotIn("gadget_cornerflag", stadium_props.ENTRANCE_WANTED)
        self.assertNotIn("doh_beltpole", stadium_props.ENTRANCE_WANTED)

    def test_each_piece_knows_where_it_goes(self):
        self.assertEqual(stadium_props.entrance_role("/x/doh_fb_home.fmdl"), "flagbearer")
        self.assertEqual(stadium_props.entrance_role("/x/banner_nationalflag_away.fmdl"), "banner")
        self.assertEqual(stadium_props.entrance_role("/x/tunnelarch_uefa_euro.fmdl"), "arch")
        self.assertEqual(stadium_props.entrance_role("/x/circleflag_afc_cl_01.fmdl"), "pennant")
        self.assertEqual(stadium_props.entrance_role("/x/passage_01.fmdl"), "tunnel")
        self.assertIsNone(stadium_props.entrance_role("/x/gadget_cornerflag.fmdl"))

    def test_the_pennant_display_stands_on_the_centre_circle(self):
        marks = stadium_props.marks_for_entrance("pennant", HALF_X, HALF_Y)
        self.assertTrue(marks)
        for x, y, _yaw in marks:
            self.assertLess(math.hypot(x, y), 10.0)

    def test_the_flag_bearers_wait_at_the_tunnel_mouth(self):
        marks = stadium_props.marks_for_entrance("flagbearer", HALF_X, HALF_Y)
        self.assertTrue(marks)
        for x, y, _yaw in marks:
            self.assertLess(y, -HALF_Y)          # outside the near touchline
            self.assertLess(abs(x), 20.0)        # by the halfway line, where the walk comes in

    def test_the_arch_is_over_that_mouth_too(self):
        arch = stadium_props.marks_for_entrance("arch", HALF_X, HALF_Y)
        bearers = stadium_props.marks_for_entrance("flagbearer", HALF_X, HALF_Y)
        self.assertTrue(arch)
        self.assertLess(abs(arch[0][1] - bearers[0][1]), 12.0)

    def test_the_tunnel_sits_behind_the_mouth_not_on_the_pitch(self):
        marks = stadium_props.marks_for_entrance("tunnel", HALF_X, HALF_Y)
        self.assertTrue(marks)
        for _x, y, _yaw in marks:
            self.assertLess(y, -HALF_Y - 20.0)

    def test_something_with_no_place_in_the_walkout_gets_no_marks(self):
        self.assertEqual(stadium_props.marks_for_entrance("cornerflag", HALF_X, HALF_Y), [])


class DressedMeshByMesh(unittest.TestCase):
    """A prop with one placeholder panel is still worth having.

    tunnelarch_uefa_euro carries four meshes: two textured and two on
    dummy_embA/embH, which are the placeholders PES swaps for the two teams'
    emblems. Judged all-or-nothing the whole arch was left in the pack; judged mesh
    by mesh the arch comes and the two blank panels do not.
    """

    def test_a_model_with_some_dressed_meshes_is_kept(self):
        keep = stadium_props.dressed_meshes([True, True, False, False])
        self.assertEqual(keep, [0, 1])

    def test_one_with_none_dressed_is_left_out_entirely(self):
        self.assertEqual(stadium_props.dressed_meshes([False, False]), [])

    def test_a_fully_dressed_model_keeps_everything(self):
        self.assertEqual(stadium_props.dressed_meshes([True, True, True]), [0, 1, 2])

    def test_no_meshes_at_all_is_nothing_to_keep(self):
        self.assertEqual(stadium_props.dressed_meshes([]), [])


if __name__ == "__main__":
    unittest.main()

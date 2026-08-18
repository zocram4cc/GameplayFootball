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


if __name__ == "__main__":
    unittest.main()

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

import math
import unittest

import stadium_staff


HALF_X = 55.0  # gametypes.hpp: x runs goal to goal
HALF_Y = 36.0  # y touchline to touchline


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

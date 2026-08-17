"""Tests for a stadium's own sky, which is a mesh the camera stands inside.

Planet Namek bakes its sky into the scene: a 1154 m dome textured with
namekbackground, reaching about 620 m above the pitch. Two things follow from the
camera being *inside* it, and both were wrong:

  * its faces point outward, so back-face culling removes all of them and the sky
    simply is not drawn - what you see instead is the engine's fallback gradient;
  * it is lit like any other surface, so forcing it visible turns the green sky
    into a white blowout.

A dome therefore needs reversed winding, like the outline shells, and normals that
leave it effectively unlit - which is what ase_util.write_mesh_normals's `constant`
option is for.

Telling a dome from the rest of the stadium has to be reliable in both
directions: mistaking the terrain for a sky would turn the ground inside out.

Run: python3 -m unittest test_stadium_sky -v
"""

import unittest

import stadium_to_gf


class IsSkyDomeTest(unittest.TestCase):
    def test_planet_nameks_dome_is_one(self):
        # measured: 1154 x 1154 x 641, centred 304 m up, so it reaches ~624 m
        self.assertTrue(stadium_to_gf.is_sky_dome(1154.4, 1154.4, 624.0, True))

    def test_planet_nameks_terrain_is_not(self):
        # 276 x 143, barely off the ground: the island the pitch sits on
        self.assertFalse(stadium_to_gf.is_sky_dome(276.6, 143.1, 12.9, True))

    def test_a_cloud_dome_counts_too(self):
        # the second dome in the same scene, 1123 m across and 440 m up
        self.assertTrue(stadium_to_gf.is_sky_dome(1123.6, 1123.4, 440.0, True))

    def test_a_wide_flat_plane_is_not_a_sky(self):
        # an apron or a water plane can be enormous and still be underfoot
        self.assertFalse(stadium_to_gf.is_sky_dome(800.0, 800.0, 3.0, True))

    def test_something_tall_but_narrow_is_not_a_sky(self):
        # a floodlight mast, a tower block behind the stand
        self.assertFalse(stadium_to_gf.is_sky_dome(12.0, 12.0, 90.0, True))

    def test_a_dome_that_does_not_surround_the_pitch_is_not_ours_to_invert(self):
        # a big backdrop off to one side: the camera is not inside it
        self.assertFalse(stadium_to_gf.is_sky_dome(1200.0, 1200.0, 600.0, False))

    def test_a_mesh_with_no_size_is_not_a_sky(self):
        self.assertFalse(stadium_to_gf.is_sky_dome(0.0, 0.0, 0.0, True))


class SkyNormalTest(unittest.TestCase):
    def test_the_constant_normal_points_down(self):
        # Away from the sun leaves the surface unlit, so its own colour shows
        # rather than a lit blowout; the engine's own sky.ase does the same.
        self.assertEqual(stadium_to_gf.SKY_CONSTANT_NORMAL, (0.0, 0.0, -1.0))


if __name__ == "__main__":
    unittest.main()

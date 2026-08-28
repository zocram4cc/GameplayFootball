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
Size alone cannot do it. Custom grounds are built at any scale - st019's bowl
spans 1.5 x 5 km and reaches 1160 m, bigger than Namek's sky - and judging by
metres classified that whole stadium as sky, leaving two billboards in the scene.

What actually makes a sky is which way it faces. PES authors a dome with its
normals turned in on the camera standing inside it (Namek's two: -1.00 and -0.97
against the outward radial), while stands, terrain and scenery face any which way
and average out near zero (st019's three bowl meshes: +0.02, +0.03, +0.05). That
is measured, not guessed at, and it holds for a ground of any size.

Run: python3 -m unittest test_stadium_sky -v
"""

import io
import unittest

import stadium_to_gf


class IsSkyDomeTest(unittest.TestCase):
    # camera_facing is how much a mesh's normals turn in on the pitch centre:
    # +1 every normal pointing at it, 0 no agreement, -1 all turned away.
    def test_planet_nameks_dome_is_one(self):
        # measured: 1154 x 1154 x 641, centred 304 m up, so it reaches ~624 m
        self.assertTrue(stadium_to_gf.is_sky_dome(1154.4, 1154.4, 624.0, True, 1.00))

    def test_planet_nameks_terrain_is_not(self):
        # 276 x 143, barely off the ground: the island the pitch sits on
        self.assertFalse(stadium_to_gf.is_sky_dome(276.6, 143.1, 12.9, True, 0.25))

    def test_a_cloud_dome_counts_too(self):
        # the second dome in the same scene, 1123 m across and 440 m up
        self.assertTrue(stadium_to_gf.is_sky_dome(1123.6, 1123.4, 440.0, True, 0.97))

    def test_a_wide_flat_plane_is_not_a_sky(self):
        # an apron or a water plane can be enormous and still be underfoot
        self.assertFalse(stadium_to_gf.is_sky_dome(800.0, 800.0, 3.0, True, 0.1))

    def test_something_tall_but_narrow_is_not_a_sky(self):
        # a floodlight mast, a tower block behind the stand
        self.assertFalse(stadium_to_gf.is_sky_dome(12.0, 12.0, 90.0, True, 1.0))

    def test_a_dome_that_does_not_surround_the_pitch_is_not_ours_to_invert(self):
        # a big backdrop off to one side: the camera is not inside it
        self.assertFalse(stadium_to_gf.is_sky_dome(1200.0, 1200.0, 600.0, False, 1.0))

    def test_a_mesh_with_no_size_is_not_a_sky(self):
        self.assertFalse(stadium_to_gf.is_sky_dome(0.0, 0.0, 0.0, True, 0.0))


class ASkyIsToldByWhichWayItFacesTest(unittest.TestCase):
    """The measurements that size alone got wrong, from five 4cc grounds."""

    def test_st019s_kilometre_bowl_is_not_a_sky(self):
        # A custom ground can be bigger than another's sky: these three meshes
        # are the whole of st019, and calling them sky left the stadium empty.
        for span_x, span_y, top_z, facing in [(1561.0, 5013.0, 1160.0, -0.02),
                                              (2660.0, 5921.0, 180.0, -0.03),
                                              (935.0, 5412.0, 181.0, -0.05)]:
            self.assertFalse(stadium_to_gf.is_sky_dome(span_x, span_y, top_z, True, facing),
                             "%.0f x %.0f m mesh taken for a sky" % (span_x, span_y))

    def test_st002s_stands_are_not_a_sky(self):
        # 1.8 km of seating either side of the pitch, facing every which way
        self.assertFalse(stadium_to_gf.is_sky_dome(1846.0, 1126.0, 144.0, True, 0.01))
        self.assertFalse(stadium_to_gf.is_sky_dome(1899.0, 1133.0, 153.0, True, 0.01))

    def test_st002s_own_domes_still_are(self):
        # the same scene's backdrop and sky: 3.4 km and 4.0 km across
        self.assertTrue(stadium_to_gf.is_sky_dome(3389.0, 3397.0, 189.0, True, 0.95))
        self.assertTrue(stadium_to_gf.is_sky_dome(4028.0, 4028.0, 1241.0, True, 0.99))

    def test_a_ceiling_of_flipped_normals_is_not_a_sky(self):
        # st002's 4.1 km apron, normals pointing down: it half-agrees with a
        # dome and is nothing like one.
        self.assertFalse(stadium_to_gf.is_sky_dome(4107.0, 4103.0, 111.0, True, 0.44))

    def test_st041s_grandstand_roof_is_not_a_sky(self):
        # 352 x 327 m and 78 m up cleared every size threshold there was
        self.assertFalse(stadium_to_gf.is_sky_dome(352.0, 327.0, 78.0, True, 0.05))


class CameraFacingTest(unittest.TestCase):
    """Measuring which way a mesh's normals turn, from the mesh itself."""

    class _Point(object):
        def __init__(self, x, y, z):
            self.x, self.y, self.z = x, y, z

    class _Vertex(object):
        def __init__(self, position, normal):
            self.position, self.normal = position, normal

    class _Mesh(object):
        def __init__(self, vertices):
            self.vertices = vertices

    def _dome(self, sign):
        """A ring of four verts 100 m out, normals radial: sign +1 out, -1 in.

        In Fox space, which is what the converter reads: x across, y up, z the
        axis GF calls -y.
        """
        ring = [(1.0, 0.0), (-1.0, 0.0), (0.0, 1.0), (0.0, -1.0)]
        return self._Mesh([
            self._Vertex(self._Point(100.0 * dx, 20.0, 100.0 * dz),
                         self._Point(sign * dx, 0.0, sign * dz))
            for dx, dz in ring])

    def test_a_dome_authored_for_the_camera_inside_it_reads_as_facing_in(self):
        self.assertAlmostEqual(stadium_to_gf.mesh_camera_facing(self._dome(-1.0)), 1.0, places=3)

    def test_a_shell_facing_outwards_reads_as_the_opposite(self):
        self.assertAlmostEqual(stadium_to_gf.mesh_camera_facing(self._dome(1.0)), -1.0, places=3)

    def test_ground_underfoot_agrees_with_neither(self):
        # a flat plane, every normal up: nothing to do with the camera
        plane = self._Mesh([
            self._Vertex(self._Point(x, 0.0, z), self._Point(0.0, 1.0, 0.0))
            for x in (-100.0, 100.0) for z in (-100.0, 100.0)])
        self.assertAlmostEqual(stadium_to_gf.mesh_camera_facing(plane), 0.0, places=3)

    def test_an_empty_mesh_is_no_dome(self):
        self.assertEqual(stadium_to_gf.mesh_camera_facing(self._Mesh([])), 0.0)


class SkyNormalTest(unittest.TestCase):
    def test_the_constant_normal_points_down(self):
        # Away from the sun leaves the surface unlit, so its own colour shows
        # rather than a lit blowout; the engine's own sky.ase does the same.
        self.assertEqual(stadium_to_gf.SKY_CONSTANT_NORMAL, (0.0, 0.0, -1.0))


if __name__ == "__main__":
    unittest.main()


class SkyMaterialIsSelfIlluminated(unittest.TestCase):
    """A sky is not a wall. The engine's own media/objects/stadiums/sky/sky.ase
    is AMBIENT 1, DIFFUSE 1, SELFILLUM 1; an imported dome written like every
    other surface - 0.588 and no self illumination - is lit like one.

    All three of Planet Namek's sky faults were that single line. With the
    normals it ships the dome goes dark, with them stripped it facets into
    blocks as each triangle catches the light differently, and flipping the
    normal only trades one for the other. Self-illuminated, it shows its own
    texture and the normal stops mattering."""

    def material(self, sky):
        out = io.StringIO()
        stadium_to_gf._write_material(out, 0, "m", "t.png", "st017", None, sky=sky)
        return out.getvalue()

    def test_a_sky_is_fully_self_illuminated(self):
        text = self.material(sky=True)
        self.assertIn("*MATERIAL_SELFILLUM 1.0", text)
        self.assertIn("*MATERIAL_AMBIENT 1.000", text)
        self.assertIn("*MATERIAL_DIFFUSE 1.000", text)

    def test_everything_else_is_still_lit(self):
        text = self.material(sky=False)
        self.assertIn("*MATERIAL_SELFILLUM 0.0", text)
        self.assertIn("*MATERIAL_AMBIENT 0.588", text)

    def test_it_matches_what_the_engines_own_sky_ships(self):
        """The house convention, read off media/objects/stadiums/sky/sky.ase."""
        text = self.material(sky=True)
        for line in ("*MATERIAL_AMBIENT 1.000\t1.000\t1.000",
                     "*MATERIAL_DIFFUSE 1.000\t1.000\t1.000",
                     "*MATERIAL_SELFILLUM 1.0"):
            self.assertIn(line, text)

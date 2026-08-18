"""Tests for importing a stadium's own pitch art.

The engine paints its pitch procedurally and then blends one image over the whole
of it by that image's alpha: media/textures/pitch/overlay.png, sampled across
+/-60 x +/-40 m (proceduralpitch.cpp, pitchFullHalfW/H). That is exactly the hook
a PES pitch needs, because PES's pitch is a flat mesh with the mowing bands, the
worn patches and the goalmouth scuffs painted into decal textures over it:
st002's pitch model is 114 x 76 m with pitch_alp on it, and 2048 x 4096 of real
content in that ftex.

So the import is a rasterisation: for every triangle of the pack's pitch mesh, map
its corners from metres into the overlay's pixels and fill it with what the decal
texture says at those UVs. Nothing is invented - what PES draws on its own pitch
is what lands in the overlay.

Run: python3 -m unittest test_pitch_overlay -v
"""

import unittest

import pitch_overlay


class PitchToPixel(unittest.TestCase):
    """The engine's own mapping, which the overlay has to agree with exactly."""

    def test_the_centre_spot_is_the_middle_of_the_image(self):
        self.assertEqual(pitch_overlay.pitch_to_pixel(0.0, 0.0, 4096, 2048), (2048.0, 1024.0))

    def test_the_corners_are_the_corners(self):
        # the overlay covers the rim as well as the field: +/-60 x +/-40 m
        self.assertEqual(pitch_overlay.pitch_to_pixel(-60.0, -40.0, 4096, 2048), (0.0, 0.0))
        self.assertEqual(pitch_overlay.pitch_to_pixel(60.0, 40.0, 4096, 2048), (4096.0, 2048.0))

    def test_a_goal_line_sits_where_the_engine_samples_it(self):
        # x 55 of 60 is 11/12 of the way from the middle to the edge
        x, _y = pitch_overlay.pitch_to_pixel(55.0, 0.0, 4096, 2048)
        self.assertAlmostEqual(x, 2048.0 + 2048.0 * 55.0 / 60.0, places=3)


class Barycentric(unittest.TestCase):
    def test_a_corner_is_all_of_itself(self):
        w = pitch_overlay.barycentric((0.0, 0.0), (0.0, 0.0), (4.0, 0.0), (0.0, 4.0))
        self.assertAlmostEqual(w[0], 1.0, places=6)
        self.assertAlmostEqual(w[1], 0.0, places=6)
        self.assertAlmostEqual(w[2], 0.0, places=6)

    def test_the_middle_is_a_third_of_each(self):
        w = pitch_overlay.barycentric((1.0, 1.0), (0.0, 0.0), (3.0, 0.0), (0.0, 3.0))
        self.assertAlmostEqual(w[0], 1.0 / 3.0, places=6)

    def test_outside_reads_negative_somewhere(self):
        w = pitch_overlay.barycentric((-1.0, -1.0), (0.0, 0.0), (4.0, 0.0), (0.0, 4.0))
        self.assertTrue(min(w) < 0.0)

    def test_a_degenerate_triangle_is_no_triangle(self):
        self.assertIsNone(pitch_overlay.barycentric((1.0, 1.0), (0.0, 0.0), (2.0, 2.0), (4.0, 4.0)))


class Wrapping(unittest.TestCase):
    """PES's line mesh tiles its strip texture, so UVs run far outside 0..1."""

    def test_inside_is_left_alone(self):
        self.assertAlmostEqual(pitch_overlay.wrap(0.25), 0.25)

    def test_above_one_wraps_round(self):
        self.assertAlmostEqual(pitch_overlay.wrap(3.25), 0.25)

    def test_below_zero_wraps_round(self):
        self.assertAlmostEqual(pitch_overlay.wrap(-0.75), 0.25)
        self.assertAlmostEqual(pitch_overlay.wrap(-24.0), 0.0)


class WhichPassIsWhich(unittest.TestCase):
    """The engine paints its own lines, so PES's line pass is left out by default."""

    def test_the_line_pass_is_recognised(self):
        self.assertTrue(pitch_overlay.is_line_pass("line_alp.dds"))
        self.assertTrue(pitch_overlay.is_line_pass("/a/b/line_alp.tga"))

    def test_the_pitch_and_its_scuffs_are_not_lines(self):
        self.assertFalse(pitch_overlay.is_line_pass("pitch_alp.tga"))
        self.assertFalse(pitch_overlay.is_line_pass("pitch_scratch_bsm_alp.tga"))
        self.assertFalse(pitch_overlay.is_line_pass("turf_bsm_alp.ftex"))

    def test_no_texture_is_no_line_pass(self):
        self.assertFalse(pitch_overlay.is_line_pass(None))


class FittingPesArtToThisPitch(unittest.TestCase):
    """PES's field is not this engine's field, so the art is fitted to ours.

    PES marks its pitch at +/-53 x +/-34.1 m (st002's line mesh) - a real 106 x 68
    field. This engine's is 110 x 72 (gametypes.hpp: pitchHalfW 55, pitchHalfH 36)
    and it paints its own lines there. Laid down unscaled, PES's touchline would
    sit two metres inside ours and the ball would go out over open grass, so the
    import is stretched until the two rectangles agree - under 4% one way, 6% the
    other, which no crest or mowing band shows.
    """

    def test_st002s_pitch_is_stretched_onto_ours(self):
        sx, sy = pitch_overlay.fit_scale(53.0, 34.1)
        self.assertAlmostEqual(sx, 55.0 / 53.0, places=6)
        self.assertAlmostEqual(sy, 36.0 / 34.1, places=6)

    def test_a_pitch_already_our_size_is_left_alone(self):
        self.assertEqual(pitch_overlay.fit_scale(55.0, 36.0), (1.0, 1.0))

    def test_without_a_measurement_nothing_is_scaled(self):
        self.assertEqual(pitch_overlay.fit_scale(None, None), (1.0, 1.0))
        self.assertEqual(pitch_overlay.fit_scale(0.0, 0.0), (1.0, 1.0))

    def test_a_measurement_that_is_not_a_pitch_is_refused(self):
        # a stray mesh half a kilometre across is not the markings
        self.assertEqual(pitch_overlay.fit_scale(500.0, 400.0), (1.0, 1.0))


class DecalResolution(unittest.TestCase):
    """A 2048 x 4096 decal squeezed into a 2048 x 1024 overlay has to be averaged.

    Sampling it point by point takes every fourth row of the source and drops the
    other three, which turned st002's painted crests into a comb of horizontal
    stripes. Reducing the decal to the overlay's own resolution first averages
    those rows instead, and the UVs do not care what size the image is.
    """

    def test_an_oversized_decal_comes_back_at_the_overlays_resolution(self):
        from PIL import Image
        reduced = pitch_overlay.fit_texture(Image.new("RGBA", (2048, 4096)), 2048, 1024)
        self.assertEqual(reduced.size, (2048, 1024))

    def test_each_axis_is_judged_on_its_own(self):
        from PIL import Image
        reduced = pitch_overlay.fit_texture(Image.new("RGBA", (256, 4096)), 2048, 1024)
        self.assertEqual(reduced.size, (256, 1024))

    def test_a_small_decal_is_left_as_it_is(self):
        from PIL import Image
        image = Image.new("RGBA", (64, 64))
        self.assertIs(pitch_overlay.fit_texture(image, 2048, 1024), image)

    def test_averaging_is_what_it_does(self):
        # two rows, one white one black: the average is grey, not one or the other
        from PIL import Image
        image = Image.new("RGBA", (1, 2))
        image.putpixel((0, 0), (255, 255, 255, 255))
        image.putpixel((0, 1), (0, 0, 0, 255))
        reduced = pitch_overlay.fit_texture(image, 1, 1)
        self.assertEqual(reduced.size, (1, 1))
        self.assertTrue(100 < reduced.getpixel((0, 0))[0] < 155)


class Rasterise(unittest.TestCase):
    """Filling one triangle of a pitch into the overlay."""

    def setUp(self):
        self.size = (8, 8)
        self.pixels = pitch_overlay.blank(*self.size)

    def _fill(self, positions, colour=(10, 200, 30, 255)):
        pitch_overlay.rasterise_triangle(
            self.pixels, self.size[0], self.size[1], positions,
            [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)], lambda u, v: colour)

    def test_a_triangle_covering_the_middle_fills_the_middle(self):
        # a right triangle over the whole of one half of the pitch
        self._fill([(-60.0, -40.0), (60.0, -40.0), (-60.0, 40.0)])
        self.assertEqual(tuple(self.pixels[1][1]), (10, 200, 30, 255))
        # ...and not the far corner, which is on the other side of the diagonal
        self.assertEqual(tuple(self.pixels[7][7]), (0, 0, 0, 0))

    def test_nothing_outside_the_pitch_is_touched(self):
        # a triangle entirely off the end of the ground
        self._fill([(200.0, 200.0), (260.0, 200.0), (200.0, 260.0)])
        self.assertTrue(all(tuple(p) == (0, 0, 0, 0) for row in self.pixels for p in row))

    def test_a_transparent_decal_leaves_the_pitch_showing(self):
        # alpha is what the engine blends by, so a clear decal must stay clear
        self._fill([(-60.0, -40.0), (60.0, -40.0), (-60.0, 40.0)], colour=(255, 0, 0, 0))
        self.assertEqual(tuple(self.pixels[1][1])[3], 0)

    def test_a_degenerate_triangle_draws_nothing(self):
        self._fill([(0.0, 0.0), (10.0, 10.0), (20.0, 20.0)])
        self.assertTrue(all(tuple(p) == (0, 0, 0, 0) for row in self.pixels for p in row))


if __name__ == "__main__":
    unittest.main()

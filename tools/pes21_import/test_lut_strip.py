"""Tests for PES's colour grading tables, and the strip the engine samples them from.

PES grades every frame through a 3D lookup table chosen by time of day and
weather - Asset/model/bg/common/lut holds sixteen of them. They are 33x33x33
volumes of half floats (ftex pixel format 12), and they are the reason an
imported stadium looks flat next to the broadcast: measured against the VGL26
reference our picture sat 1.68x low in the midtones (median 74 against 124)
while our highlights were already hotter than its, and our shadows 13x more
crushed. That is a missing tone curve, not a missing light. PES's own day table
maps mid grey 0.5 to 0.616 and rolls 0.75..1.0 into 0.666..0.689, which lifts
our pitch from 26/92/110 to 19/132/152 against the reference's 32/139/197.

OpenGL 3.2 has no portable 3D texture path through this renderer's texture
plumbing, so the volume is unrolled into an ordinary PNG: the blue axis becomes
33 slices laid left to right, each slice a 33x33 image of red across and green
down. Several tables stack into bands down the same file so one texture covers
every time of day.

Run: python3 -m unittest test_lut_strip -v
"""

import unittest

import lut_strip


def identity_volume(size):
    """A table that returns its input, as nested [b][g][r] -> (r, g, b) floats."""
    return [[[(r / (size - 1.0), g / (size - 1.0), b / (size - 1.0))
              for r in range(size)]
             for g in range(size)]
            for b in range(size)]


class StripLayout(unittest.TestCase):
    def test_one_table_is_as_wide_as_its_slices_and_as_tall_as_one(self):
        pixels = lut_strip.strip_pixels([identity_volume(33)], 33)
        self.assertEqual(len(pixels[0]), 33 * 33)
        self.assertEqual(len(pixels), 33)

    def test_more_tables_stack_into_bands_without_getting_wider(self):
        pixels = lut_strip.strip_pixels([identity_volume(33)] * 4, 33)
        self.assertEqual(len(pixels[0]), 33 * 33)
        self.assertEqual(len(pixels), 33 * 4)

    def test_a_texel_lands_where_the_shader_looks_for_it(self):
        # The shader reads x = blue * size + red, y = band * size + green.
        volume = identity_volume(33)
        volume[5][7][11] = (1.0, 0.5, 0.25)  # [b][g][r]
        pixels = lut_strip.strip_pixels([identity_volume(33), volume], 33)
        self.assertEqual(pixels[33 + 7][5 * 33 + 11], (255, 128, 64))

    def test_an_identity_table_round_trips_every_corner(self):
        pixels = lut_strip.strip_pixels([identity_volume(33)], 33)
        for r, g, b in [(0, 0, 0), (32, 0, 0), (0, 32, 0), (0, 0, 32), (32, 32, 32), (16, 8, 24)]:
            got = pixels[g][b * 33 + r]
            want = tuple(round(c / 32.0 * 255) for c in (r, g, b))
            self.assertEqual(got, want, "at r%d g%d b%d" % (r, g, b))

    def test_values_outside_the_display_range_are_clamped_not_wrapped(self):
        volume = identity_volume(2)
        volume[0][0][0] = (-0.5, 1.5, 0.5)
        pixels = lut_strip.strip_pixels([volume], 2)
        self.assertEqual(pixels[0][0], (0, 255, 128))

    def test_tables_of_the_wrong_size_are_refused(self):
        # A mismatched band would silently shift every lookup after it.
        with self.assertRaises(ValueError):
            lut_strip.strip_pixels([identity_volume(33), identity_volume(16)], 33)


class BandSelection(unittest.TestCase):
    """Which table an engine run should use, mirroring PES's own naming."""

    def test_the_bands_are_written_in_a_documented_order(self):
        self.assertEqual(lut_strip.BAND_ORDER, ["day", "cloudy", "evening", "night"])

    def test_each_band_maps_to_a_pes_table_name(self):
        # s_ is the table PES grades an SDR frame with; h_ is its HDR pair.
        self.assertEqual(lut_strip.table_name("day"), "lut_s_day_game")
        self.assertEqual(lut_strip.table_name("night"), "lut_s_night_game")

    def test_an_unknown_band_is_an_error_rather_than_a_wrong_grade(self):
        with self.assertRaises(KeyError):
            lut_strip.table_name("monsoon")


if __name__ == "__main__":
    unittest.main()

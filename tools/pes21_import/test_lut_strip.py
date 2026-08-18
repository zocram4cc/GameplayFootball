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


def shouldered_volume(size, ceiling=0.69):
    """A table like PES's lut_s_day_game: a curve that stops climbing early.

    Its grey response reaches 0.616 by half input and 0.689 at full, so half the
    input range lands inside a 0.07 band. Used as a display transfer it costs the
    picture its whole top end.
    """
    def curve(t):
        return min(ceiling, ceiling * (t ** 0.35))
    return [[[(curve(r / (size - 1.0)), curve(g / (size - 1.0)), curve(b / (size - 1.0)))
              for r in range(size)]
             for g in range(size)]
            for b in range(size)]


class GreyResponse(unittest.TestCase):
    """What a table does to grey, which is what says whether it is a display grade.

    Eleven of the sixteen tables PES ships stop climbing around 0.69 - every one of
    the day, cloudy and evening pairs bar lut_h_day_demo - and applied as a
    display-to-display transfer that is what flattened the frame: on st011 the
    graded picture measured p98 0.659 against 0.918 ungraded and 0.918 in the
    reference broadcast, with the spread down to 0.087 from 0.132. Nothing in the
    importer looked, so a table that cannot be a display grade was written into the
    strip and shipped on by default.
    """

    def test_an_identity_table_answers_with_its_input(self):
        response = lut_strip.grey_response(identity_volume(33))
        self.assertEqual(len(response), 33)
        self.assertAlmostEqual(response[0], 0.0, places=5)
        self.assertAlmostEqual(response[-1], 1.0, places=5)

    def test_the_response_climbs(self):
        response = lut_strip.grey_response(identity_volume(9))
        self.assertEqual(response, sorted(response))

    def test_an_identity_table_spans_the_display_range(self):
        self.assertTrue(lut_strip.spans_display_range(identity_volume(33)))

    def test_a_shouldered_table_does_not(self):
        # PES's own lut_s_day_game, near enough: white comes out at 0.69
        self.assertFalse(lut_strip.spans_display_range(shouldered_volume(33)))

    def test_a_table_that_almost_reaches_white_is_accepted(self):
        # a real grade may roll its top off a little without being unusable
        self.assertTrue(lut_strip.spans_display_range(shouldered_volume(33, ceiling=0.93)))


class BandSelection(unittest.TestCase):
    """Which table an engine run should use, mirroring PES's own naming."""

    def test_the_bands_are_written_in_a_documented_order(self):
        self.assertEqual(lut_strip.BAND_ORDER, ["day", "cloudy", "evening", "night"])

    def test_each_band_offers_its_tables_in_preference_order(self):
        # PES ships four per condition: an h_ and an s_, for its presentation
        # ("demo") and for gameplay ("game").
        for band in lut_strip.BAND_ORDER:
            names = lut_strip.table_names(band)
            self.assertGreaterEqual(len(names), 2)
            for name in names:
                self.assertIn(band, name)
                self.assertTrue(name.startswith("lut_"))

    def test_an_unknown_band_is_an_error_rather_than_a_wrong_grade(self):
        with self.assertRaises(KeyError):
            lut_strip.table_names("monsoon")

    def test_a_table_that_spans_the_range_is_preferred_over_one_that_does_not(self):
        found = {"lut_h_day_demo": identity_volume(9), "lut_s_day_game": shouldered_volume(9)}
        chosen = lut_strip.choose_table("day", found,
                                        order=["lut_s_day_game", "lut_h_day_demo"])
        self.assertEqual(chosen, "lut_h_day_demo")

    def test_a_shouldered_table_is_still_used_when_it_is_all_there_is(self):
        # better PES's own colour than none; main() says so on the way past
        found = {"lut_s_day_game": shouldered_volume(9)}
        self.assertEqual(lut_strip.choose_table("day", found, order=["lut_s_day_game"]),
                         "lut_s_day_game")

    def test_nothing_to_choose_from_is_no_choice(self):
        self.assertIsNone(lut_strip.choose_table("day", {}, order=["lut_s_day_game"]))


class PlanningTheStrip(unittest.TestCase):
    """What goes in each of the four bands, given what PES actually ships.

    Of the sixteen tables only lut_h_day_demo and the four night ones span the
    display range: there is no usable cloudy or evening table at all. Writing a
    shouldered one into those bands would flatten every overcast and evening match
    the same way the day band was flattened, so a band with nothing usable borrows
    the nearest band that has something instead - which is what this already did for
    a table that was missing outright.
    """

    def _loaded(self, **kinds):
        out = {}
        for name, kind in kinds.items():
            out[name] = identity_volume(9) if kind == "good" else shouldered_volume(9)
        return out

    def test_every_band_gets_something(self):
        plan = lut_strip.plan_bands(self._loaded(lut_h_day_demo="good"))
        self.assertEqual([band for band, _name, _from in plan], lut_strip.BAND_ORDER)

    def test_a_band_with_a_usable_table_uses_its_own(self):
        plan = lut_strip.plan_bands(self._loaded(lut_h_day_demo="good",
                                                 lut_h_night_demo="good"))
        chosen = {band: name for band, name, _from in plan}
        self.assertEqual(chosen["day"], "lut_h_day_demo")
        self.assertEqual(chosen["night"], "lut_h_night_demo")

    def test_a_band_with_only_a_shouldered_table_borrows_a_usable_one(self):
        plan = lut_strip.plan_bands(self._loaded(lut_h_day_demo="good",
                                                 lut_h_cloudy_demo="shouldered"))
        borrowed = {band: borrowed_from for band, _name, borrowed_from in plan}
        self.assertEqual(borrowed["cloudy"], "day")
        self.assertIsNone(borrowed["day"])

    def test_the_borrowed_band_is_the_nearest_earlier_one(self):
        # order is day, cloudy, evening, night: evening takes cloudy's, which is
        # day's, rather than reaching forward to night
        plan = lut_strip.plan_bands(self._loaded(lut_h_day_demo="good",
                                                 lut_h_evening_demo="shouldered",
                                                 lut_h_night_demo="good"))
        borrowed = {band: borrowed_from for band, _name, borrowed_from in plan}
        self.assertEqual(borrowed["evening"], "day")

    def test_a_shouldered_table_is_used_when_nothing_usable_came_before_it(self):
        # better PES's own colour than a black frame; the caller says so out loud
        plan = lut_strip.plan_bands(self._loaded(lut_h_day_demo="shouldered"))
        day = [row for row in plan if row[0] == "day"][0]
        self.assertEqual(day[1], "lut_h_day_demo")
        self.assertIsNone(day[2])

    def test_no_tables_at_all_is_refused(self):
        with self.assertRaises(ValueError):
            lut_strip.plan_bands({})


if __name__ == "__main__":
    unittest.main()

"""Tests for importing a stadium's own lighting.

Every 4cc stadium ships light/#Win/light_st<slot>_a[fr]_fpkd_extracted/*.fox2.xml,
and it is readable XML describing exactly how PES lights that ground. Planet
Namek's says, among much else:

    latitude   -34.54313      longitude -58.44978     gmtTimeDifference -3
    year 2019  month 4  day 8   dateTimeHour 12
    northAngle 96             sunLux 150000
    shadowRange 240           hiResShadowRange 35

That is a place, a date and a time - Buenos Aires, the eighth of April, noon -
which fixes where the sun is to the degree. The engine instead calls
Match::SetRandomSunParams, which picks a direction at random every match and
jitters the colour by up to a tenth, so shadows fall a different way each time
and never the way the reference broadcast's do.

The astronomy lives here rather than in the engine: the converter writes the
direction it computes into lighting.txt beside the stadium, and the engine just
reads a vector.

Run: python3 -m unittest test_stadium_lighting -v
"""

import unittest

import stadium_lighting


class SolarPosition(unittest.TestCase):
    """Checked against positions that are true by definition."""

    def test_the_sun_is_overhead_at_the_equator_at_an_equinox(self):
        # Within a couple of degrees: the 2019 March equinox fell at 21:58 UTC,
        # so at noon that day the declination is still a little south of zero.
        elevation, _azimuth = stadium_lighting.solar_position(
            latitude=0.0, longitude=0.0, year=2019, month=3, day=20, hour=12, minute=0,
            gmt_offset=0)
        self.assertGreater(elevation, 87.0)
        self.assertLessEqual(elevation, 90.0)

    def test_the_sun_is_overhead_at_the_tropic_of_cancer_at_the_june_solstice(self):
        elevation, _azimuth = stadium_lighting.solar_position(
            latitude=23.44, longitude=0.0, year=2019, month=6, day=21, hour=12, minute=0,
            gmt_offset=0)
        self.assertGreater(elevation, 87.0)

    def test_at_southern_noon_the_sun_stands_to_the_north(self):
        # Buenos Aires, the eighth of April, noon: Namek's own settings.
        _elevation, azimuth = stadium_lighting.solar_position(
            latitude=-34.54313, longitude=-58.44978, year=2019, month=4, day=8, hour=12,
            minute=0, gmt_offset=-3)
        # azimuth is degrees clockwise from north
        self.assertTrue(azimuth < 30.0 or azimuth > 330.0, "azimuth was %.1f" % azimuth)

    def test_namek_is_lit_by_a_midday_autumn_sun(self):
        # 8 April is autumn in Buenos Aires: the sun reaches about 48 degrees.
        elevation, _azimuth = stadium_lighting.solar_position(
            latitude=-34.54313, longitude=-58.44978, year=2019, month=4, day=8, hour=12,
            minute=0, gmt_offset=-3)
        self.assertGreater(elevation, 40.0)
        self.assertLess(elevation, 58.0)

    def test_the_sun_is_below_the_horizon_at_midnight(self):
        elevation, _azimuth = stadium_lighting.solar_position(
            latitude=51.5, longitude=0.0, year=2019, month=1, day=15, hour=0, minute=0,
            gmt_offset=0)
        self.assertLess(elevation, 0.0)


class SunDirection(unittest.TestCase):
    """Turning a compass bearing into the engine's own axes (z up)."""

    def test_a_sun_directly_overhead_points_straight_up(self):
        direction = stadium_lighting.sun_direction(elevation=90.0, azimuth=0.0, north_angle=0.0)
        self.assertAlmostEqual(direction[2], 1.0, places=5)
        self.assertAlmostEqual(direction[0], 0.0, places=5)
        self.assertAlmostEqual(direction[1], 0.0, places=5)

    def test_a_sun_on_the_northern_horizon_points_along_the_stadium_axis(self):
        direction = stadium_lighting.sun_direction(elevation=0.0, azimuth=0.0, north_angle=0.0)
        self.assertAlmostEqual(direction[1], 1.0, places=5)
        self.assertAlmostEqual(direction[2], 0.0, places=5)

    def test_the_stadiums_own_north_rotates_the_bearing(self):
        # northAngle 90 turns the ground a quarter turn under the same sun.
        direction = stadium_lighting.sun_direction(elevation=0.0, azimuth=90.0, north_angle=90.0)
        self.assertAlmostEqual(direction[1], 1.0, places=5)

    def test_the_direction_is_a_unit_vector(self):
        for elevation, azimuth in [(48.0, 5.0), (10.0, 200.0), (75.0, 300.0)]:
            direction = stadium_lighting.sun_direction(elevation, azimuth, 96.0)
            length = sum(component * component for component in direction) ** 0.5
            self.assertAlmostEqual(length, 1.0, places=5)

    def test_a_sun_under_the_horizon_is_lifted_to_it(self):
        # A light below the pitch lights nothing and shadows everything; night
        # matches are lit by the floodlights, which are the engine's own affair.
        direction = stadium_lighting.sun_direction(elevation=-20.0, azimuth=0.0, north_angle=0.0)
        self.assertGreaterEqual(direction[2], 0.0)


class SidecarText(unittest.TestCase):
    def test_it_writes_what_the_engine_reads(self):
        text = stadium_lighting.sidecar_text((0.1, -0.2, 0.97), sun_lux=150000.0)
        self.assertIn("sun 0.100 -0.200 0.970", text)
        self.assertIn("sun_lux 150000", text)

    def test_every_line_is_one_fact(self):
        for line in stadium_lighting.sidecar_text((0.0, 0.0, 1.0), 100000.0).splitlines():
            if line and not line.startswith("#"):
                self.assertGreaterEqual(len(line.split()), 2)


if __name__ == "__main__":
    unittest.main()

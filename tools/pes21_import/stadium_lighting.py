"""Imports a stadium's own lighting - where PES puts its sun.

Every 4cc stadium ships light/#Win/light_st<slot>_a[fr]_fpkd_extracted/*.fox2.xml,
and it is readable XML: a TppAtmosphere and a stack of TimeOfDaySettings saying
exactly how PES lights that ground. Planet Namek's, among much else:

    latitude -34.54313  longitude -58.44978  gmtTimeDifference -3
    year 2019  month 4  day 8  dateTimeHour 12
    northAngle 96  sunLux 150000  shadowRange 240

A place, a date and a time - Buenos Aires, the eighth of April, noon - which
fixes the sun to the degree. The engine picks a direction at random every match
instead (Match::SetRandomSunParams), which is why its shadows fall a different
way in every kickoff and never the way the reference broadcast's do.

The astronomy is done here so the engine only has to read a vector:

    stadium_lighting.py <stadium pack dir> [--out <stadium dir>]

writes lighting.txt beside the stadium's .object:

    # where the sun is, in the engine's axes (z up), and how bright PES calls it
    sun 0.084 0.669 0.739
    sun_lux 150000
"""

import argparse
import glob
import math
import os
import sys
import xml.etree.ElementTree as ET


def _day_of_year(year, month, day):
    return (__import__("datetime").date(year, month, day) -
            __import__("datetime").date(year, 1, 1)).days + 1


def solar_position(latitude, longitude, year, month, day, hour, minute, gmt_offset):
    """-> (elevation, azimuth) in degrees; azimuth is clockwise from north.

    The usual low-precision solar position (NOAA's), which is good to about a
    tenth of a degree - far finer than a shadow in a football stadium needs.
    """
    n = _day_of_year(year, month, day)
    # fractional year, radians
    gamma = 2.0 * math.pi / 365.0 * (n - 1 + (hour - 12.0) / 24.0)

    equation_of_time = 229.18 * (0.000075 + 0.001868 * math.cos(gamma)
                                 - 0.032077 * math.sin(gamma)
                                 - 0.014615 * math.cos(2 * gamma)
                                 - 0.040849 * math.sin(2 * gamma))  # minutes
    declination = (0.006918 - 0.399912 * math.cos(gamma) + 0.070257 * math.sin(gamma)
                   - 0.006758 * math.cos(2 * gamma) + 0.000907 * math.sin(2 * gamma)
                   - 0.002697 * math.cos(3 * gamma) + 0.001480 * math.sin(3 * gamma))  # radians

    time_offset = equation_of_time + 4.0 * longitude - 60.0 * gmt_offset  # minutes
    true_solar_time = hour * 60.0 + minute + time_offset
    hour_angle = math.radians(true_solar_time / 4.0 - 180.0)

    lat = math.radians(latitude)
    cos_zenith = (math.sin(lat) * math.sin(declination) +
                  math.cos(lat) * math.cos(declination) * math.cos(hour_angle))
    cos_zenith = max(-1.0, min(1.0, cos_zenith))
    zenith = math.acos(cos_zenith)
    elevation = 90.0 - math.degrees(zenith)

    sin_zenith = math.sin(zenith)
    if abs(sin_zenith) < 1e-9:
        return elevation, 0.0
    cos_azimuth = ((math.sin(lat) * cos_zenith - math.sin(declination)) /
                   (math.cos(lat) * sin_zenith))
    cos_azimuth = max(-1.0, min(1.0, cos_azimuth))
    azimuth = math.degrees(math.acos(cos_azimuth))  # from south, in NOAA's form
    if hour_angle > 0:
        azimuth = -azimuth
    azimuth = (azimuth + 180.0) % 360.0  # clockwise from north
    return elevation, azimuth


def sun_direction(elevation, azimuth, north_angle):
    """-> unit vector towards the sun in the engine's axes (x, y, z up).

    With northAngle 0 the ground's own north is +y; the stadium's northAngle
    turns the ground under the same sky. A sun below the horizon is lifted onto
    it: a light under the pitch lights nothing and shadows everything, and a
    night match is lit by the floodlights, which is the engine's own business.
    """
    elevation = max(0.0, elevation)
    bearing = math.radians(azimuth - north_angle)
    horizontal = math.cos(math.radians(elevation))
    return (horizontal * math.sin(bearing),
            horizontal * math.cos(bearing),
            math.sin(math.radians(elevation)))


def sidecar_text(direction, sun_lux, fog=1.0):
    return ("# Where this ground's sun is, from the lighting PES ships with it\n"
            "# (tools/pes21_import/stadium_lighting.py). The engine reads this\n"
            "# instead of picking a direction at random - see scenelighting.hpp.\n"
            "sun %.3f %.3f %.3f\n"
            "sun_lux %g\n"
            "# ...and how much fog it wants, from the atmosphere's influenceOfFog.\n"
            "# Namek asks for none, which is why its rocks keep their own colour\n"
            "# instead of being washed with the green of its horizon.\n"
            "fog %g\n" % (direction[0], direction[1], direction[2], sun_lux, fog))


def _scalars(entity):
    values = {}
    for prop in entity.iter("property"):
        found = [v.text for v in prop.findall("value")]
        if len(found) == 1 and found[0] is not None:
            values[prop.get("name")] = found[0]
    return values


def read_lighting(fox2_path):
    """-> the atmosphere settings that matter, as floats."""
    root = ET.parse(fox2_path).getroot()
    wanted = {}
    for entity in root.iter("entity"):
        if entity.get("class") not in ("TppAtmosphere", "TimeOfDaySettings"):
            continue
        values = _scalars(entity)
        if entity.get("class") == "TimeOfDaySettings" and values.get("name") != "TimeOfDaySettings":
            continue
        for key in ("latitude", "longitude", "gmtTimeDifference", "year", "month", "day",
                    "northAngle", "sunLux", "dateTimeHour", "dateTimeMinute",
                    "influenceOfFog"):
            if key in values and key not in wanted:
                try:
                    wanted[key] = float(values[key])
                except ValueError:
                    pass
    return wanted


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("pack", help="a 4cc stadium pack directory")
    parser.add_argument("--out", default=None,
                        help="where to write lighting.txt (the converted stadium's directory)")
    args = parser.parse_args()

    candidates = sorted(glob.glob(os.path.join(args.pack, "**", "*.fox2.xml"), recursive=True))
    if not candidates:
        print("no lighting found under %s" % args.pack)
        return 1
    # The _af pack is the fine-weather one; _ar is rain.
    source = next((c for c in candidates if "_af" in os.path.basename(c)), candidates[0])
    settings = read_lighting(source)
    print("lighting: %s" % os.path.relpath(source, args.pack))
    for key in sorted(settings):
        print("   %-18s %s" % (key, settings[key]))

    elevation, azimuth = solar_position(
        latitude=settings.get("latitude", 51.5), longitude=settings.get("longitude", 0.0),
        year=int(settings.get("year", 2019)), month=int(settings.get("month", 6)),
        day=int(settings.get("day", 21)), hour=int(settings.get("dateTimeHour", 15)),
        minute=int(settings.get("dateTimeMinute", 0)),
        gmt_offset=settings.get("gmtTimeDifference", 0.0))
    direction = sun_direction(elevation, azimuth, settings.get("northAngle", 0.0))
    print("sun: elevation %.1f deg, azimuth %.1f deg -> %.3f %.3f %.3f"
          % (elevation, azimuth, direction[0], direction[1], direction[2]))

    if args.out:
        os.makedirs(args.out, exist_ok=True)
        path = os.path.join(args.out, "lighting.txt")
        open(path, "w").write(sidecar_text(direction, settings.get("sunLux", 100000.0),
                                          settings.get("influenceOfFog", 1.0)))
        print("wrote %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main())

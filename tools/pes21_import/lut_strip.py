"""Turns PES's colour grading tables into one PNG the engine can sample.

PES grades every frame through a 33x33x33 lookup table picked by time of day and
weather (Asset/model/bg/common/lut, ftex pixel format 12: half floats). Without
that grade an imported stadium comes out flat - measured against the VGL26
reference our midtones sat 1.68x low while our highlights were already hotter,
which is the signature of a missing tone curve rather than a missing light.

The volume is unrolled into an ordinary 8-bit PNG, because a PNG is something
anyone can look at and replace (see docs/ASSETS.md) and because it needs no
3D-texture path through the renderer:

    x = blue * 33 + red        (33 slices of 33 laid left to right)
    y = band * 33 + green      (one band per time of day, stacked downwards)

    lut_strip.py <cpk-or-extracted-dir> [--out data/media/textures/lut/grade.png]

The engine reads the band from "graphics_lut_band" and the mix from
"graphics_lut_strength"; nothing here touches the repository's own art.
"""

import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import ftex  # noqa: E402

# The order the bands are written in, and the order the engine indexes them by.
BAND_ORDER = ["day", "cloudy", "evening", "night"]

# PES ships four tables per condition: an h_ and an s_ pair, each in a version for
# its own presentation ("demo") and one for gameplay ("game"). Which of them is a
# usable display grade is not something the names tell you - eleven of the sixteen
# stop climbing around 0.69, so half their input range lands in a 0.07 band, and
# using one of those as a display transfer costs the picture its whole top end.
# Only lut_h_day_demo and the four night tables span the range.
#
# So the choice is made by looking (choose_table): the tables are tried in this
# order and the first one whose grey response actually reaches white is taken.
# Measured against the reference broadcast's ladder, grading st011 and Planet Namek
# through lut_h_day_demo lands within 0.15 and 0.26 of it, against 0.55 and 0.48
# for lut_s_day_game - which was the table this used to take on faith.
TABLE_NAMES = {
    "day": ["lut_h_day_demo", "lut_s_day_demo", "lut_h_day_game", "lut_s_day_game"],
    "cloudy": ["lut_h_cloudy_demo", "lut_s_cloudy_demo", "lut_h_cloudy_game",
               "lut_s_cloudy_game"],
    "evening": ["lut_h_evening_demo", "lut_s_evening_demo", "lut_h_evening_game",
                "lut_s_evening_game"],
    "night": ["lut_h_night_demo", "lut_s_night_demo", "lut_h_night_game",
              "lut_s_night_game"],
}

LUT_SIZE = 33

# How near white a table's grey response has to come before it can be a display
# grade. A real grade may roll its top off; PES's shouldered tables stop at 0.69.
MIN_WHITE = 0.9


def table_names(band):
    """The PES tables a band may come from, best first. KeyError if unknown."""
    return list(TABLE_NAMES[band])


def grey_response(volume):
    """-> what the table does to grey, along its own diagonal.

    This is the curve that decides whether a table can be a display transfer at
    all, and it is the one thing nothing looked at before.
    """
    size = len(volume)
    return [float(volume[i][i][i][0]) for i in range(size)]


def spans_display_range(volume):
    """Whether a table's grey response reaches white (MIN_WHITE)."""
    response = grey_response(volume)
    return bool(response) and response[-1] >= MIN_WHITE


def choose_table(band, volumes_by_name, order=None):
    """-> the name of the table to use for `band`, or None if none were found.

    The first candidate whose grey response spans the display range; failing that,
    the first one present at all, because PES's own colour beats none.
    """
    names = order if order is not None else table_names(band)
    present = [name for name in names if name in volumes_by_name]
    for name in present:
        if spans_display_range(volumes_by_name[name]):
            return name
    return present[0] if present else None


def _to_byte(value):
    return max(0, min(255, int(round(value * 255.0))))


def strip_pixels(volumes, size):
    """Unrolls [b][g][r] -> (r, g, b) tables into rows of (r, g, b) bytes."""
    rows = []
    for volume in volumes:
        if len(volume) != size or len(volume[0]) != size or len(volume[0][0]) != size:
            raise ValueError("every table must be %d cubed" % size)
        for g in range(size):
            row = []
            for b in range(size):
                for r in range(size):
                    row.append(tuple(_to_byte(c) for c in volume[b][g][r]))
            rows.append(row)
    return rows


def read_table(path):
    """Reads one ftex grading table as a [b][g][r] -> (r, g, b) volume."""
    pixel_format, width, height, depth, _type, frames = ftex.parse(open(path, "rb").read())
    if pixel_format != 12:
        raise ValueError("%s is pixel format %d, not the half-float volume format 12"
                         % (os.path.basename(path), pixel_format))
    if not (width == height == depth):
        raise ValueError("%s is %dx%dx%d, not a cube" % (os.path.basename(path), width, height, depth))
    texels = struct.unpack("<%de" % (width * height * depth * 4), frames[0])
    volume = []
    at = 0
    for _b in range(depth):
        plane = []
        for _g in range(height):
            line = []
            for _r in range(width):
                line.append((texels[at], texels[at + 1], texels[at + 2]))
                at += 4
            plane.append(line)
        volume.append(plane)
    return volume, width


def plan_bands(volumes_by_name):
    """-> [(band, table name, band borrowed from or None)] for BAND_ORDER.

    A band whose only tables shoulder borrows the nearest earlier band that has a
    usable one: of the sixteen tables PES ships, only lut_h_day_demo and the four
    night ones reach white, so cloudy and evening have nothing of their own and
    writing what they do have would flatten every overcast match the way the day
    band was flattened. A band with nothing usable behind it keeps its own table
    anyway - PES's own colour beats a strip with a hole in it - and main() says so.
    """
    if not volumes_by_name:
        raise ValueError("no grading tables to plan a strip from")
    plan = []
    last_usable = None      # (band, name) of the last band that spans the range
    last_written = None     # (band, name) whatever went in last, usable or not
    for band in BAND_ORDER:
        chosen = choose_table(band, volumes_by_name)
        usable = chosen is not None and spans_display_range(volumes_by_name[chosen])
        if usable:
            last_usable = last_written = (band, chosen)
            plan.append((band, chosen, None))
        elif last_usable is not None:
            plan.append((band, last_usable[1], last_usable[0]))
        elif chosen is not None:
            # Nothing usable anywhere yet: PES's own colour beats a hole.
            last_written = (band, chosen)
            plan.append((band, chosen, None))
        elif last_written is not None:
            plan.append((band, last_written[1], last_written[0]))
        else:
            raise ValueError("no table for band %s and nothing to borrow" % band)
    return plan


def write_strip(rows, out_path):
    from PIL import Image
    image = Image.new("RGB", (len(rows[0]), len(rows)))
    image.putdata([texel for row in rows for texel in row])
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    image.save(out_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("source", help="a .cpk holding bg/common/lut, or a directory of it")
    parser.add_argument("--out", default="data/media/textures/lut/grade.png")
    args = parser.parse_args()

    root = args.source
    if root.lower().endswith(".cpk"):
        import subprocess
        extracted = os.path.join(os.path.dirname(os.path.abspath(args.out)), "pes_lut_extract")
        subprocess.run([sys.executable, os.path.join(HERE, "cpk.py"), root, extracted,
                        "--filter=bg/common/lut"], check=True, stdout=subprocess.DEVNULL)
        root = extracted

    wanted = {name for names in TABLE_NAMES.values() for name in names}
    paths = {}
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            stem = os.path.splitext(name)[0]
            if name.lower().endswith(".ftex") and stem in wanted:
                paths[stem] = os.path.join(dirpath, name)

    # Read every candidate once, then pick per band by what it does to grey rather
    # than by its name.
    loaded = {}
    for name, path in sorted(paths.items()):
        volume, size = read_table(path)
        if size != LUT_SIZE:
            print("%s is %d cubed, not %d - left out" % (name, size, LUT_SIZE))
            continue
        loaded[name] = volume

    if not loaded:
        raise SystemExit("no grading tables found under %s" % root)
    volumes = []
    for band, chosen, borrowed_from in plan_bands(loaded):
        volume = loaded[chosen]
        volumes.append(volume)
        response = grey_response(volume)
        if borrowed_from:
            print("%-8s %-18s borrowed from %s: nothing it ships reaches white"
                  % (band + ":", chosen, borrowed_from))
        elif spans_display_range(volume):
            print("%-8s %-18s grey 0.5 -> %.3f, white -> %.3f"
                  % (band + ":", chosen, response[len(response) // 2], response[-1]))
        else:
            print("%-8s %-18s grey 0.5 -> %.3f, white -> %.3f  <- stops short, so this "
                  "band will flatten the picture"
                  % (band + ":", chosen, response[len(response) // 2], response[-1]))

    write_strip(strip_pixels(volumes, LUT_SIZE), args.out)
    print("wrote %s (%d bands of %d cubed)" % (args.out, len(volumes), LUT_SIZE))
    return 0


if __name__ == "__main__":
    sys.exit(main())

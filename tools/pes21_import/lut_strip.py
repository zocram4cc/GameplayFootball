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

# PES ships an SDR table and an HDR one per condition; s_ is the SDR pair, which
# is what an 8-bit framebuffer wants.
TABLE_NAMES = {
    "day": "lut_s_day_game",
    "cloudy": "lut_s_cloudy_game",
    "evening": "lut_s_evening_game",
    "night": "lut_s_night_game",
}

LUT_SIZE = 33


def table_name(band):
    """The PES table each band comes from. Raises KeyError on an unknown band."""
    return TABLE_NAMES[band]


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

    found = {}
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            stem = os.path.splitext(name)[0]
            if name.lower().endswith(".ftex") and stem in TABLE_NAMES.values():
                found[stem] = os.path.join(dirpath, name)

    volumes = []
    for band in BAND_ORDER:
        wanted = table_name(band)
        if wanted not in found:
            print("%-8s %s missing, writing the previous band again" % (band + ":", wanted))
            if not volumes:
                raise SystemExit("no grading tables found under %s" % root)
            volumes.append(volumes[-1])
            continue
        volume, size = read_table(found[wanted])
        if size != LUT_SIZE:
            raise SystemExit("%s is %d cubed, expected %d" % (wanted, size, LUT_SIZE))
        volumes.append(volume)
        neutral = volume[size // 2][size // 2][size // 2]
        print("%-8s %s  mid grey -> %.3f/%.3f/%.3f" % (band + ":", wanted, *neutral))

    write_strip(strip_pixels(volumes, LUT_SIZE), args.out)
    print("wrote %s (%d bands of %d cubed)" % (args.out, len(volumes), LUT_SIZE))
    return 0


if __name__ == "__main__":
    sys.exit(main())

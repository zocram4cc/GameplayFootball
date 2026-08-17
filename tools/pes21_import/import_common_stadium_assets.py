"""Imports PES's common stadium assets - the ones every ground shares.

PES does not put this material in a stadium. Asset/model/bg/common holds one copy
of everything that is the same everywhere, and each ground gets it:

    ad/       the advertising hoardings, faces assigned at run time
    goal/     the net patterns
    pitch/    the pitch's detail, grain and wear maps
    lut/      colour grading tables, one per time of day and weather
    cheer/    the crowd's banner and tifo art
    effect/   a sky sample and a lens filter

The fork follows the same pattern: these install into the engine's shared media
folders, so every converted stadium gets them without carrying its own copy. Where
the engine already has a hook the import lands on it directly - the goal's netting
texture, the adboard randomiser's pool - and where it does not, the art is
installed alongside for the feature that will use it.

    import_common_stadium_assets.py <cpk-or-extracted-dir> [--out data] [--net N]

With a .cpk it extracts bg/common first. --net converts one of PES's net patterns into goalnetting_pes.png, ready for the
engine's "goal_netting_texture" setting; the engine's own goalnetting.png is never
touched, because nothing PES-derived belongs in the repository.
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import ftex  # noqa: E402
import lut_strip  # noqa: E402


# where each common set lands, relative to the data root
DESTINATIONS = {
    "adboards": "media/textures/adboards",
    "nets": "media/textures/stadium",
    "pitch": "media/textures/pitch/pes",
    "lut": "media/textures/lut",
    "banners": "media/textures/crowd/banners",
    "sky": "media/textures/sky",
}


def convert_texture(src, dest):
    """ftex or png -> png at dest. Returns True when something was written."""
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    if src.lower().endswith(".png"):
        shutil.copyfile(src, dest)
        return True
    try:
        ftex.convert(src, dest)
        return True
    except Exception as exc:
        print("  could not convert %s: %s" % (os.path.basename(src), exc))
        return False


def gather(root, relative_glob):
    return sorted(glob.glob(os.path.join(root, "**", relative_glob), recursive=True))


def import_set(root, out_root, label, pattern, destination, prefix):
    """Converts one common set into the engine's media tree."""
    sources = gather(root, pattern)
    if not sources:
        print("%-9s nothing found (%s)" % (label + ":", pattern))
        return 0
    written = 0
    for src in sources:
        stem = os.path.splitext(os.path.basename(src))[0]
        stem = stem[:-4] if stem.endswith("_bsm") else stem
        dest = os.path.join(out_root, destination, prefix + stem + ".png")
        if convert_texture(src, dest):
            written += 1
    print("%-9s %d file(s) -> %s" % (label + ":", written, destination))
    return written


def import_grading_tables(root, out_root):
    """Unrolls PES's colour grading tables into the strip the engine samples.

    These are 33-cubed volumes of half floats, so there is no PNG to convert them
    to one at a time; lut_strip.py lays the whole set out as one ordinary image
    (see its docstring, and src/systems/graphics/scenegrade.hpp).
    """
    out = os.path.join(out_root, DESTINATIONS["lut"], "grade.png")
    volumes = []
    for band in lut_strip.BAND_ORDER:
        wanted = lut_strip.table_name(band)
        matches = gather(root, wanted + ".ftex")
        if not matches:
            print("lut:      %s missing" % wanted)
            if volumes:
                volumes.append(volumes[-1])
            continue
        volume, size = lut_strip.read_table(matches[0])
        if size != lut_strip.LUT_SIZE:
            print("lut:      %s is %d cubed, expected %d" % (wanted, size, lut_strip.LUT_SIZE))
            return 0
        volumes.append(volume)
    if not volumes:
        print("lut:      no grading tables found")
        return 0
    lut_strip.write_strip(lut_strip.strip_pixels(volumes, lut_strip.LUT_SIZE), out)
    print("lut:      %d band(s) -> %s" % (len(volumes), os.path.relpath(out, out_root)))
    return len(volumes)


def install_netting(source, target):
    """Turns a PES net pattern into the engine's netting texture.

    PES stores the pattern as dark threads on white with an empty alpha channel;
    the engine's netting is white threads carried entirely by alpha, so a straight
    copy would hang an opaque panel across the goal. The alpha is the inverse of
    the pattern's luminance - dark thread becomes opaque - and the colour is left
    white, which is what a net is.
    """
    try:
        from PIL import Image
    except ImportError:
        print("goal netting: needs Pillow to derive the alpha channel")
        return False
    pattern = Image.open(source).convert("L")
    alpha = pattern.point(lambda v: 255 - v)
    white = Image.new("RGB", pattern.size, (255, 255, 255))
    out = Image.merge("RGBA", (*white.split(), alpha))
    out.save(target)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("source", help="a .cpk holding bg/common, or a directory of it")
    parser.add_argument("--out", default="data", help="the engine's data root")
    parser.add_argument("--net", default=None,
                        help="net pattern to install as the goal netting, e.g. x_netPat04")
    args = parser.parse_args()

    root = args.source
    if root.lower().endswith(".cpk"):
        extracted = os.path.join(os.path.dirname(os.path.abspath(args.out)),
                                 "pes_common_extract")
        print("extracting bg/common from %s" % os.path.basename(root))
        subprocess.run([sys.executable, os.path.join(HERE, "cpk.py"), root, extracted,
                        "--filter=bg/common"], check=True, stdout=subprocess.DEVNULL)
        root = extracted

    total = 0
    # The hoardings' faces: PES assigns these at run time, and so does the engine -
    # RandomizeAdboards swaps any mesh whose texture ident begins ad_placeholder.
    total += import_set(root, args.out, "adboards", "bill_*_bsm.ftex",
                        DESTINATIONS["adboards"], "ad_4cc_")
    total += import_set(root, args.out, "nets", "x_netPat*.png",
                        DESTINATIONS["nets"], "pes_")
    total += import_set(root, args.out, "pitch", "pitch_*.ftex",
                        DESTINATIONS["pitch"], "")
    total += import_grading_tables(root, args.out)
    total += import_set(root, args.out, "banners", "b*_txt_*.ftex",
                        DESTINATIONS["banners"], "")
    total += import_set(root, args.out, "sky", "sample_sky.ftex", DESTINATIONS["sky"], "pes_")

    if args.net:
        source = os.path.join(args.out, DESTINATIONS["nets"], "pes_%s.png" % args.net)
        # Installed alongside, never over the engine's own: goalnetting.png is
        # tracked, and no PES-derived art belongs in the repository.
        target = os.path.join(args.out, "media/textures/stadium/goalnetting_pes.png")
        if os.path.isfile(source):
            if install_netting(source, target):
                print("goal netting: %s" % args.net)
        else:
            print("goal netting: %s not among the imported patterns" % args.net)

    print("imported %d shared file(s)" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())

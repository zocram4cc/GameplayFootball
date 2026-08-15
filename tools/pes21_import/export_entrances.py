"""Pre-exports PES match-entrance camerawork into the engine's .camtrack format.

PES picks a match entrance by competition: the cut data ships as families
``ent_<id>_*`` under ``common/demo/fixdemo/ent/cut_data/`` in ``dt12_g4.cpk``,
each family being one entrance presentation. Within a family the variants are

  ``*_cam*.fdc``   camera cuts       -> what this script exports
  ``*_pl*.fdc``    player animation packs (see entrance_pl.py)
  ``*_mob*.fdc``   crowd

The engine selects at runtime on the "entrance_id" config key plus the stadium,
so the layout written here is

  <out>/<id>/<fdc basename>.camtrack

and the stadium token (``st060``) stays in the file name for the engine to match
on. Download-pack sources override stock ones of the same name, matching how
PES itself layers ``cuts_dl`` over the shipped data.

  python3 export_entrances.py <cut_data-dir> [<cut_data-dir>...] <out-dir>
                              [--ids 001,020] [--stadiums st002,st060]
                              [--max-per-family N]

Later directories win, so pass stock first and the download pack last.
"""

import argparse
import os
import re

import canm_to_camtrack

FAMILY_RE = re.compile(r"^(ent_\d{3})_")
STADIUM_RE = re.compile(r"_(st\d{3})")


def is_camera_pack(name):
    """Camera variants carry a _cam token; _pl and _mob packs must not match."""
    stem = os.path.splitext(name)[0]
    return any(part.startswith("cam") for part in stem.split("_"))


def collect(dirs, ids=None, stadiums=None):
    """{family: {basename: path}}, later dirs overriding earlier ones."""
    found = {}
    unfiltered = {}
    for directory in dirs:
        if not os.path.isdir(directory):
            continue
        for name in sorted(os.listdir(directory)):
            if not name.endswith(".fdc") or not is_camera_pack(name):
                continue
            match = FAMILY_RE.match(name)
            if not match:
                continue
            family = match.group(1)
            if ids and family[-3:] not in ids:
                continue
            unfiltered.setdefault(family, {})[name] = os.path.join(directory, name)
            if stadiums:
                stadium = STADIUM_RE.search(name)
                # Keep stadium-agnostic shots regardless: they are the fallback.
                if stadium and stadium.group(1) not in stadiums:
                    continue
            found.setdefault(family, {})[name] = os.path.join(directory, name)

    # A family the stadium filter emptied out would be unselectable at runtime,
    # so it keeps its shots anyway — the engine falls back to any track in the
    # family when none names the stadium being played in.
    for family, entries in unfiltered.items():
        if not found.get(family):
            found[family] = entries
    return found


def export(dirs, out_dir, ids=None, stadiums=None, max_per_family=0):
    families = collect(dirs, ids, stadiums)
    written = skipped = 0
    for family in sorted(families):
        entries = sorted(families[family].items())
        if max_per_family > 0:
            entries = entries[:max_per_family]
        dest_dir = os.path.join(out_dir, family[-3:])
        os.makedirs(dest_dir, exist_ok=True)
        for name, path in entries:
            dest = os.path.join(dest_dir, os.path.splitext(name)[0] + ".camtrack")
            try:
                cuts, frames = canm_to_camtrack.export(path, dest)
            except Exception as exc:  # a pack with no usable camera stream
                print("SKIP %s: %s" % (name, exc))
                skipped += 1
                continue
            if frames == 0:
                os.remove(dest)
                skipped += 1
                continue
            written += 1
            print("  %s -> %s (%d cuts, %d frames)"
                  % (name, os.path.relpath(dest, out_dir), cuts, frames))
    print("exported %d tracks, skipped %d -> %s" % (written, skipped, out_dir))
    return written


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+",
                        help="one or more cut_data directories, then the out dir")
    parser.add_argument("--ids", default="",
                        help="comma-separated entrance ids to export (default: all)")
    parser.add_argument("--stadiums", default="",
                        help="comma-separated stadium tokens to keep (default: all)")
    parser.add_argument("--max-per-family", type=int, default=0)
    args = parser.parse_args()

    if len(args.paths) < 2:
        parser.error("need at least one source directory and an output directory")
    dirs, out_dir = args.paths[:-1], args.paths[-1]
    ids = set(i.strip() for i in args.ids.split(",") if i.strip())
    stadiums = set(s.strip() for s in args.stadiums.split(",") if s.strip())
    export(dirs, out_dir, ids, stadiums, args.max_per_family)


if __name__ == "__main__":
    main()

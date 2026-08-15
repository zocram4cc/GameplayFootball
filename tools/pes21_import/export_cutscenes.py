"""Exports every fixdemo cutscene category's camerawork into .camtrack pools.

PES keeps its hand-authored match camerawork in ``dt12_g4.cpk`` under
``common/demo/fixdemo/<category>/cut_data/*.fdc``. The entrance category has
its own exporter (export_entrances.py) because the engine selects entrances by
competition; the remaining categories are pools the engine draws from when a
moment happens:

  goal    a goal was scored        change  a substitution
  foul    a foul was given         timeup  the end of a period
  pk      penalties                result  the final result
  end     end-of-match sequences   mode    mode/menu presentation

  python3 export_cutscenes.py <fixdemo-dir> <out-dir>
                              [--categories goal,foul] [--max-per-category N]

``<fixdemo-dir>`` is the directory holding the per-category folders, e.g.
``.../common/demo/fixdemo``. Output is ``<out>/<category>/<name>.camtrack``,
matching the pools Match::StartCutscene loads.
"""

import argparse
import os

import canm_to_camtrack

# the entrance has its own competition-aware exporter
DEFAULT_CATEGORIES = ["goal", "foul", "change", "timeup", "pk", "result", "end", "mode"]


def is_camera_pack(name):
    """Camera variants carry a _cam token; _pl and _mob packs must not match."""
    stem = os.path.splitext(name)[0]
    return any(part.startswith("cam") for part in stem.split("_"))


def export_category(cut_dir, dest_dir, max_per_category=0):
    if not os.path.isdir(cut_dir):
        return 0, 0
    names = [n for n in sorted(os.listdir(cut_dir))
             if n.endswith(".fdc") and is_camera_pack(n)]
    if max_per_category > 0:
        names = names[:max_per_category]
    written = skipped = 0
    for name in names:
        os.makedirs(dest_dir, exist_ok=True)
        dest = os.path.join(dest_dir, os.path.splitext(name)[0] + ".camtrack")
        try:
            canm_to_camtrack.export(os.path.join(cut_dir, name), dest)
            written += 1
        except Exception as exc:  # a pack with no usable camera stream
            print("SKIP %s/%s: %s" % (os.path.basename(cut_dir), name, exc))
            skipped += 1
    return written, skipped


def export(fixdemo_dir, out_dir, categories=None, max_per_category=0):
    total_written = total_skipped = 0
    for category in categories or DEFAULT_CATEGORIES:
        cut_dir = os.path.join(fixdemo_dir, category, "cut_data")
        written, skipped = export_category(
            cut_dir, os.path.join(out_dir, category), max_per_category)
        if written or skipped:
            print("%-7s %4d tracks (%d skipped)" % (category, written, skipped))
        total_written += written
        total_skipped += skipped
    return total_written, total_skipped


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixdemo_dir")
    parser.add_argument("out_dir")
    parser.add_argument("--categories", default="")
    parser.add_argument("--max-per-category", type=int, default=0)
    args = parser.parse_args()
    categories = [c for c in args.categories.split(",") if c] or None
    written, skipped = export(args.fixdemo_dir, args.out_dir, categories,
                              args.max_per_category)
    print("exported %d tracks, %d skipped" % (written, skipped))


if __name__ == "__main__":
    main()

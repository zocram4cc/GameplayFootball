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

# A referee's decision is not one cutscene: PES names each foul pack after what
# the official actually did, so the packs sort into pools the engine can pick
# from once it knows the sanction. The card letter is the strongest signal
# (card_y yellow, card_r red, card_w a spoken warning, card_n no card at all);
# the surrounding words say what the incident was.
FOUL_SUBCATEGORIES = [
    # (subdirectory, [tokens that must all appear in the stem])
    ("card_red", ["card_r"]),
    ("card_yellow", ["card_y"]),
    ("warning", ["card_w"]),         # told off, no card shown
    ("injury", ["injury"]),
    ("protest", ["bejudged"]),       # the player pleads his case
    ("referee_run", ["referee_oncoming"]),
    ("no_card", ["card_n"]),
]

# Offside is signalled by the assistant, and PES files those shots with the
# goal camerawork rather than the fouls.
GOAL_SUBCATEGORIES = [
    ("offside", ["offside"]),
    ("disallowed", ["flag_up"]),
]

SUBCATEGORIES = {"foul": FOUL_SUBCATEGORIES, "goal": GOAL_SUBCATEGORIES}


def is_camera_pack(name):
    """Camera variants carry a _cam token; _pl and _mob packs must not match.

    Only the entrance packs are consistently tagged that way. Elsewhere a pack
    is a camera pack unless it is explicitly one of the other kinds, so the
    decision shots (foul_card_y01 and friends) are not filtered away.
    """
    stem = os.path.splitext(name)[0]
    parts = stem.split("_")
    if any(part.startswith("cam") for part in parts):
        return True
    return not any(part.startswith(("pl", "mob")) for part in parts)


def classify(category, name):
    """Subdirectory for one pack, or None to leave it in the category root."""
    stem = os.path.splitext(name)[0].lower()
    for subdirectory, tokens in SUBCATEGORIES.get(category, []):
        if all(token in stem for token in tokens):
            return subdirectory
    return None


def export_category(cut_dir, dest_dir, max_per_category=0, category=""):
    if not os.path.isdir(cut_dir):
        return 0, 0
    names = [n for n in sorted(os.listdir(cut_dir))
             if n.endswith(".fdc") and is_camera_pack(n)]
    # the cap applies per pool, so a big category does not starve its subpools
    if max_per_category > 0:
        per_pool = {}
        kept = []
        for name in names:
            pool = classify(category, name) or ""
            per_pool[pool] = per_pool.get(pool, 0) + 1
            if per_pool[pool] <= max_per_category:
                kept.append(name)
        names = kept
    written = skipped = 0
    for name in names:
        subdirectory = classify(category, name)
        target_dir = os.path.join(dest_dir, subdirectory) if subdirectory else dest_dir
        os.makedirs(target_dir, exist_ok=True)
        dest = os.path.join(target_dir, os.path.splitext(name)[0] + ".camtrack")
        try:
            _cuts, frames = canm_to_camtrack.export(os.path.join(cut_dir, name), dest)
            # Actor packs carry choreography, not camerawork (entrance_pl.py and
            # export_actors.py handle those). They parse cleanly but yield no
            # camera frames, so they must not leave an empty track behind.
            if frames == 0:
                os.remove(dest)
                skipped += 1
                continue
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
            cut_dir, os.path.join(out_dir, category), max_per_category, category)
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

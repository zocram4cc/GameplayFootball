"""Exports the actor choreography of any fixdemo cutscene category.

A PES cutscene is not just a camera move: alongside the camera packs each
category ships *actor* packs staging what the people on the pitch do — the
scorer's celebration, the referee producing a card, the substituted player
walking off. Those packs hold the same tag-0x04 actor records the entrance
packs use (documented in docs/PES21_CAMERA_TRACE.md section 3), so the same
conversion applies: a text ``.chor`` naming each actor's slot, its baked world
root track and the in-place clip it plays, plus the clips themselves as ``.anim``.

entrance_pl.py stays the entrance's own exporter because entrances are chosen
by competition; this one walks the remaining categories.

  python3 export_actors.py <fixdemo-dir> <anims-dir> <out-dir>
                           [--categories goal,foul] [--max-per-category N]

``<anims-dir>`` holds the loose cutscene ganis (dt12 ``FoxAnim/FixDemo/
Animations``). Output mirrors the camera pools:

  <out>/<category>/<pack>.chor
  <out>/<category>/anims/<clip>.anim
"""

import argparse
import os

import camera_cut
import entrance_pl
import export_cutscenes

DEFAULT_CATEGORIES = ["goal", "foul", "change", "timeup", "pk", "result", "end"]


def is_actor_pack(path):
    """True when the pack stages actors rather than (or besides) a camera."""
    try:
        fdc = camera_cut.load(path)
    except Exception:
        return False
    return bool(fdc.actors)


def export_category(cut_dir, anims_dir, dest_dir, max_per_category=0, category=""):
    if not os.path.isdir(cut_dir):
        return 0, 0
    names = [n for n in sorted(os.listdir(cut_dir)) if n.endswith(".fdc")]
    written = skipped = 0
    clip_cache = {}
    for name in names:
        if max_per_category and written >= max_per_category:
            break
        path = os.path.join(cut_dir, name)
        try:
            # Same subcategory split the camera pools use: a pack staging an
            # offside belongs in goal/offside, not loose in goal/. Left flat,
            # the runtime found goal/offside empty and fell back to the parent
            # pool - so an offside was choreographed as a goal celebration.
            subdirectory = export_cutscenes.classify(category, name)
            target_dir = os.path.join(dest_dir, subdirectory) if subdirectory else dest_dir
            os.makedirs(target_dir, exist_ok=True)
            entrance_pl.export_pack(path, anims_dir, target_dir, clip_cache)
            written += 1
        except Exception as exc:
            # camera-only packs and packs whose clips are not installed
            skipped += 1
            if "no actor records" not in str(exc):
                print("SKIP %s/%s: %s" % (os.path.basename(cut_dir), name, exc))
    return written, skipped


def export(fixdemo_dir, anims_dir, out_dir, categories=None, max_per_category=0):
    total_written = total_skipped = 0
    for category in categories or DEFAULT_CATEGORIES:
        cut_dir = os.path.join(fixdemo_dir, category, "cut_data")
        written, skipped = export_category(
            cut_dir, anims_dir, os.path.join(out_dir, category), max_per_category, category)
        if written or skipped:
            print("%-7s %4d choreographies (%d skipped)" % (category, written, skipped))
        total_written += written
        total_skipped += skipped
    return total_written, total_skipped


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixdemo_dir")
    parser.add_argument("anims_dir")
    parser.add_argument("out_dir")
    parser.add_argument("--categories", default="")
    parser.add_argument("--max-per-category", type=int, default=0)
    args = parser.parse_args()
    categories = [c for c in args.categories.split(",") if c] or None
    written, skipped = export(args.fixdemo_dir, args.anims_dir, args.out_dir,
                              categories, args.max_per_category)
    print("exported %d choreographies, %d skipped" % (written, skipped))


if __name__ == "__main__":
    main()

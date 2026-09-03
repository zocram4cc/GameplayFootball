"""Rescales the vertical of already-installed cutscene clips.

The fixdemo set's two position channels are not in the same unit. The world
path reads right at retarget.PES_POS_TO_M - goal_2018_run_30's choreography
covers 6.0 m in 2.5 s - while the mover, which carries the vertical and
nothing else, reads right at retarget.PES_POS_TO_M_GAMEPLAY, the scale
calibrate_pos_scale.py measured against ankle and pelvis heights.

Read at the path's scale the vertical is flattened by 6.25x, and a clip that
leaves the ground stops leaving it: goal_celebrate_0057 is a somersault whose
pelvis should swing between 0.79 m and 1.75 m, and instead it stays between
1.05 m and 1.20 m - the body turns upside down at standing height, so the
actor appears to lie down in mid-air. That is what a showcase shows on every
goal that draws a flip celebration.

gani_to_anim now takes the two scales separately, so a fresh import is right.
This is the reproducible way to bring an existing install up to it without the
fixdemo ganis, which are not kept on disk: the clips' `player` track holds
x, y and z, the vertical is entirely the mover's (RIG_ROOT's own y is flat in
every clip measured), so multiplying z alone by the ratio of the two scales is
exactly what re-converting would produce. Stamped, so it cannot run twice.

  python3 migrate_cutscene_mover.py [--data ../../data] [--dry-run]
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import retarget

RATIO = retarget.PES_POS_TO_M_GAMEPLAY / retarget.PES_POS_TO_M   # 6.25
STAMP = "<mover_scale>"


def migrate_text(text):
    """-> (new text, keys rescaled) or (text, 0) if it is already stamped."""
    if STAMP in text:
        return text, 0
    lines = text.split("\n")
    rescaled = 0
    for index, line in enumerate(lines):
        if not line.startswith("player,"):
            continue
        parts = line.split(",")
        # frame, x, y, z per key
        for at in range(4, len(parts), 4):
            parts[at] = "%f" % (float(parts[at]) * RATIO)
            rescaled += 1
        lines[index] = ",".join(parts)
        break
    if not rescaled:
        return text, 0
    tail = "%s\n\t%s\n</mover_scale>\n" % (STAMP, "gameplay")
    return "\n".join(lines).rstrip("\n") + "\n" + tail, rescaled


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--data", default=os.path.join(here, "..", "..", "data"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    root = os.path.join(args.data, "media", "cutscenes")
    done = skipped = keys = 0
    for base, _, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith(".anim"):
                continue
            path = os.path.join(base, name)
            text = open(path).read()
            new, rescaled = migrate_text(text)
            if not rescaled:
                skipped += 1
                continue
            if not args.dry_run:
                open(path, "w").write(new)
            done += 1
            keys += rescaled
    print("cutscene mover: %d clip(s) rescaled x%.2f (%d keys), %d already stamped%s"
          % (done, RATIO, keys, skipped, "  (dry run)" if args.dry_run else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

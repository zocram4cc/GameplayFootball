"""Which rig joints an imported model's skin actually binds to.

body_coverage.py asks whether geometry sits *near* each joint. This asks the
different question that caught a whole class of broken imports: does any vertex
actually move when that joint does?

A model can pass every other check and still have a dead limb. It parses, its
mesh and face counts are healthy, every texture resolves, the figure is
connected and correctly proportioned in a still - and then the arm never bends,
because no vertex is weighted to the elbow or the wrist. A turntable render
cannot show it; only animation can, which is why it survived into a showcase.

Measured over the 107 imported models on disk, 34 have at least one unbound arm
joint and nine have no arm chain at all. The commonest case by far is both
wrists: the hand geometry sits far enough from the rig's wrist that
fmdl_to_fullbody's nearest-joint falloff gives those vertices to the elbow, and
the hand then travels rigidly with the forearm instead of articulating.

  python3 joint_binding.py <model.weights> [more.weights ...]
  python3 joint_binding.py --all          # every model under data/media/players/custom

Exits non-zero if any model is missing a joint, so it can gate an import.
"""

import argparse
import collections
import glob
import os
import sys

import retarget

# A vertex counts as bound to a joint when the joint actually drives it. Below
# this the influence is a rounding contribution and the limb still will not move
# with the joint in any visible way.
BOUND_WEIGHT = 0.3

# The joints a footballer cannot do without. Fingers are deliberately excluded:
# retarget notes they are parentless in the source skeleton, and body_coverage
# already found they produce false positives.
REQUIRED = ("left_shoulder", "left_elbow", "left_hand",
            "right_shoulder", "right_elbow", "right_hand",
            "left_thigh", "left_knee", "left_ankle",
            "right_thigh", "right_knee", "right_ankle")


def bound_counts(path, threshold=BOUND_WEIGHT):
    """-> {joint id: how many vertices that joint actually drives}."""
    counts = collections.Counter()
    total = 0
    for line in open(path, "r", errors="replace"):
        if line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 4:
            continue
        total += 1
        for token in parts[3:]:
            joint, _, weight = token.partition(":")
            try:
                if float(weight) > threshold:
                    counts[int(joint)] += 1
            except ValueError:
                continue
    return counts, total


def unbound_joints(path, required=REQUIRED, threshold=BOUND_WEIGHT):
    """-> the required joints no vertex is bound to, in rig order."""
    counts, _ = bound_counts(path, threshold)
    ids = retarget.JOINT_ID
    return [name for name in required
            if name in ids and counts[ids[name]] == 0]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("weights", nargs="*")
    parser.add_argument("--all", action="store_true",
                        help="every model under data/media/players/custom")
    parser.add_argument("--threshold", type=float, default=BOUND_WEIGHT)
    args = parser.parse_args()

    paths = list(args.weights)
    if args.all:
        here = os.path.dirname(os.path.abspath(__file__))
        root = os.path.join(here, "..", "..", "data", "media", "players", "custom")
        paths += sorted(glob.glob(os.path.join(root, "*", "*.weights")))
    if not paths:
        parser.error("give a .weights file or --all")

    broken = 0
    for path in paths:
        missing = unbound_joints(path, threshold=args.threshold)
        name = os.path.basename(os.path.dirname(path)) or os.path.basename(path)
        if missing:
            broken += 1
            print("%-14s UNBOUND  %s" % (name, ", ".join(missing)))
    print("\n%d model(s) checked, %d with a dead joint" % (len(paths), broken))
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())

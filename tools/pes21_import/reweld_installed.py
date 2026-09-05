"""Welds coincident skin weights in every installed model.

Run after a change to the weld/reconcile passes (seams.py): the installed
`.weights` sidecars are derived data, and re-importing needs the source packs
while this needs nothing but what is already on disk.

Why it exists: a UV seam duplicates a vertex - same place, two entries, because
the two faces need different texture coordinates - and the importer guessed
each entry's weights on its own. Where two joints tie (a shoulder and the
clavicle above it) the tie is broken by the last bits of a float, so the two
halves of one seam skinned to different joints and the bind-pose bake pulled
them apart. Measured over the 152 installed models: 144 of them tear at the
BIND pose, worst 1293x on a 2hug body, and on screen that is the long shard
fanning out of a player (`skin_probe.py`).

The sidecar is keyed by exact vertex position, so the positions are written
back verbatim - never re-formatted - and only the influence list changes.
Idempotent: welded weights weld to themselves.

  python3 reweld_installed.py [--data ../../data] [--dry-run] [--quiet]
"""

import argparse
import glob
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import retarget
import seams
import skin_probe

# An edge stretching more than this at the BIND pose is torn: at the bind the
# skin should reproduce the mesh exactly, so anything past rounding is a
# vertex disagreeing with its neighbour about which joint owns it.
TEAR = 2.0

# Two repairs were tried on top of the weld and both measured WORSE than doing
# nothing, so neither is here: smoothing a torn vertex over its neighbours
# (hdg_XXX23 373x -> 418x, because the average of a hand and a chest is itself
# a cross-limb blend), and rebinding it to its nearest bone (2hug_1869 88x ->
# 2502x, because at the bind pose the bake is the only thing that moves a
# vertex, so changing one vertex's weights moves it away from neighbours that
# were not changed). Weights have to be right BEFORE the bake, which is the
# importer's job (fmdl_to_fullbody.nearest_bone); what is left here is the weld,
# which needs no bake to be correct, and the measurement.


def weld_text(text, radius=seams.COINCIDENT_RADIUS):
    """-> (new text, how many vertices changed hands).

    Position tokens pass through as written: the engine looks a weight up by
    float equality on the position, and re-printing 0.123456 is a chance to
    change it.

    Exact duplicates only, by default. A sidecar is keyed by position and
    carries no faces, so this pass cannot tell a seam's two halves from the
    mesh's own spacing - and over the wider band it flattened whole hands
    (seams.weld). The importer welds with the faces in hand; this is the repair
    for what is already on disk, and it only agrees vertices that are the same
    coordinate.
    """
    lines = text.splitlines()
    head = [line for line in lines[:1] if line.startswith("#")]
    body = lines[len(head):]
    part = []
    keep = []
    for line in body:
        parts = line.split()
        if len(parts) < 4:
            keep.append((line, None))
            continue
        joints = []
        for token in parts[3:]:
            joint, _, weight = token.partition(":")
            try:
                joints.append((int(joint), float(weight)))
            except ValueError:
                continue
        keep.append((parts[:3], len(part)))
        part.append((tuple(float(v) for v in parts[:3]), joints))
    if not part:
        return text, 0
    welded = seams.weld([part], radius=radius)[0]
    changed = 0
    out = list(head)
    for tokens, index in keep:
        if index is None:
            out.append(tokens)
            continue
        before = part[index][1]
        after = welded[index][1]
        # A weld that keeps the joint order and moves the weights is still a
        # change; counting only the joint set meant the new file was computed
        # and then not written, because the caller writes `if changed`.
        if ["%d:%.6f" % jw for jw in before] != ["%d:%.6f" % jw for jw in after]:
            changed += 1
        out.append(" ".join(tokens) + " " +
                   " ".join("%d:%.6f" % (j, w) for j, w in after))
    return "\n".join(out) + "\n", changed


def bind_pose_skin(positions, influences):
    """-> the positions the engine's bake produces at the bind pose."""
    names = {i: n for n, i in retarget.JOINT_ID.items()}
    order = [n for n, _, _ in retarget.GF_NODES]
    world = skin_probe.joint_transforms(retarget.gf_world_bind(), retarget.GF_PARENT, {}, order)
    return skin_probe.skin(positions, influences, skin_probe.base_pose(), world, names)


def torn_edges(positions, influences, edges, threshold=TEAR):
    """-> ([(stretch, i, j)], worst stretch) at the bind pose."""
    posed = bind_pose_skin(positions, influences)
    torn = []
    worst = 0.0
    for i, j in edges:
        rest = math.dist(positions[i], positions[j])
        if rest < 1e-4:
            continue
        stretch = math.dist(posed[i], posed[j]) / rest
        worst = max(worst, stretch)
        if stretch > threshold:
            torn.append((stretch, i, j))
    return torn, worst


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--data", default=os.path.join(here, "..", "..", "data"))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    # Imported models only. media/objects/players/models/*.weights is PES's own
    # body, generated by pes_base_body.py from the user's install: rewriting
    # PES-authored weights with a heuristic here is the wrong direction
    # (AGENTS.md - PES is the reference), and it is regenerated, not repaired.
    paths = sorted(glob.glob(os.path.join(
        args.data, "media", "players", "custom", "*", "*.weights")))
    if not paths:
        print("no installed weights under %s" % args.data)
        return 1
    touched = 0
    total = 0
    for path in paths:
        text = open(path, "r", errors="replace").read()
        welded, changed = weld_text(text)
        if changed:
            touched += 1
            total += changed
            if not args.quiet:
                print("  %-40s %6d vertex/vertices welded"
                      % (os.path.basename(path), changed))
            if not args.dry_run:
                open(path, "w").write(welded)
    print("%d model(s) checked, %d rewritten, %d vertex weight(s) welded%s"
          % (len(paths), touched, total, " (dry run)" if args.dry_run else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Skins a model by an animation frame offline, and reports what tears.

Why this exists: the arms deform wrongly in cutscenes and seven explanations
have been argued from source and killed by measurement - bind-pose mismatch,
quaternion axis conversion, finger joints stealing forearm weights, stale
offsets, animation smoothing, clavicle displacement, and proximity-guessed
weights (PES ships weights for 100% of vertices and every bone maps, so that
last one never even runs). Each was plausible on the page.

So this does the skinning the engine does - linear blend, one affine transform
per joint - and measures the result instead of predicting it. A limb that
balloons is edges stretched far past their rest length, and every stretched
edge names the joints driving its ends. That points at a joint rather than at
a hypothesis.

  python3 skin_probe.py <model.weights> <anim> [--frame N] [--worst N]

Reports the worst edge stretch in the frame and which joints own it.
"""

import argparse
import collections
import math
import os
import sys

import retarget


def read_weights(path):
    """-> ([(x, y, z)], [[(joint id, weight)]]) in file order."""
    positions = []
    influences = []
    for line in open(path, "r", errors="replace"):
        if line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 4:
            continue
        positions.append(tuple(float(v) for v in parts[:3]))
        binds = []
        for token in parts[3:]:
            joint, _, weight = token.partition(":")
            try:
                binds.append((int(joint), float(weight)))
            except ValueError:
                continue
        influences.append(binds)
    return positions, influences



def read_anim(path):
    """-> {joint name: {frame: (x, y, z, w)}}.

    One line per joint, not per frame: the name, then repeating groups of
    frame and value. Rotation tracks carry a quaternion, so their group is
    five long; `player` carries the root translation and its group is four.
    Reading the line as a single keyframe - which is what the first version of
    this did - measures the rest pose however high a frame is asked for.
    """
    tracks = {}
    for line in open(path, "r", errors="replace"):
        parts = line.strip().split(",")
        if len(parts) < 5:
            continue
        name = parts[0]
        stride = 4 if name == "player" else 5
        frames = {}
        for at in range(1, len(parts) - stride + 1, stride):
            try:
                frame = int(parts[at])
                values = tuple(float(v) for v in parts[at + 1:at + stride])
            except ValueError:
                continue
            frames[frame] = values if stride == 5 else values + (0.0,)
        if frames:
            tracks[name] = frames
    return tracks


def pose_at(frames, wanted):
    """The keyframe at or before `wanted` - the tracks are sparse."""
    keys = [k for k in frames if k <= wanted]
    return frames[max(keys)] if keys else frames[min(frames)]


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qrot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx))


def joint_transforms(bind, parents, local, order):
    """-> {joint: (world rotation, world position)} by walking the hierarchy.

    The engine's own arithmetic: a joint's world rotation is its parent's times
    its own, and its world position is the parent's plus the bind offset turned
    by that rotation.
    """
    world = {}
    for name in order:
        parent = parents.get(name)
        rotation = local.get(name, (0.0, 0.0, 0.0, 1.0))
        if parent is None or parent not in world:
            world[name] = (rotation, bind.get(name, (0.0, 0.0, 0.0)))
            continue
        parent_rotation, parent_position = world[parent]
        combined = qmul(parent_rotation, rotation)
        offset = tuple(bind[name][i] - bind[parent][i] for i in range(3))
        turned = qrot(parent_rotation, offset)
        world[name] = (combined,
                       tuple(parent_position[i] + turned[i] for i in range(3)))
    return world


def skin(positions, influences, bind, world, names):
    """Linear blend skinning, as humanoidbase does it."""
    out = []
    for position, binds in zip(positions, influences):
        x = y = z = 0.0
        total = 0.0
        for joint, weight in binds:
            name = names.get(joint)
            if name is None or name not in world or name not in bind:
                continue
            rotation, origin = world[name]
            local = tuple(position[i] - bind[name][i] for i in range(3))
            moved = qrot(rotation, local)
            x += weight * (origin[0] + moved[0])
            y += weight * (origin[1] + moved[1])
            z += weight * (origin[2] + moved[2])
            total += weight
        out.append((x / total, y / total, z / total) if total > 0 else position)
    return out


def default_ase(weights_path):
    """The .ase a .weights file was written beside."""
    stem, _ = os.path.splitext(weights_path)
    return stem + ".ase"


def mesh_edges(ase_path, positions):
    """-> {(i, j)} into `positions`, for every edge a triangle owns.

    The weights file lists vertices and the .ase lists the faces over them, in
    the same order per mesh but with the meshes concatenated - so vertices are
    matched by position, which also welds the seams PES leaves unwelded and is
    what the engine skins anyway.
    """
    if not os.path.exists(ase_path):
        return set()
    lookup = {}
    for index, p in enumerate(positions):
        lookup.setdefault((round(p[0], 4), round(p[1], 4), round(p[2], 4)), index)

    edges = set()
    verts = []
    for line in open(ase_path, "r", errors="replace"):
        if "*MESH_VERTEX" in line:
            parts = line.split()
            try:
                verts.append((round(float(parts[2]), 4), round(float(parts[3]), 4),
                              round(float(parts[4]), 4)))
            except (IndexError, ValueError):
                continue
        elif "*NODE_NAME" in line and verts:
            verts = []
        elif "*MESH_FACE" in line and "NORMAL" not in line:
            parts = line.replace(":", " ").split()
            try:
                a, b, c = (int(parts[parts.index(k) + 1]) for k in ("A", "B", "C"))
            except (ValueError, IndexError):
                continue
            for p, q in ((a, b), (b, c), (c, a)):
                if p >= len(verts) or q >= len(verts):
                    continue
                i = lookup.get(verts[p])
                j = lookup.get(verts[q])
                if i is not None and j is not None and i != j:
                    edges.add((min(i, j), max(i, j)))
    return edges


def dominant(binds, names):
    if not binds:
        return "?"
    return names.get(max(binds, key=lambda kv: kv[1])[0], "?")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("weights")
    parser.add_argument("anim")
    parser.add_argument("--frame", type=int, default=16)
    parser.add_argument("--worst", type=int, default=12)
    parser.add_argument("--ase", default="",
                        help="the mesh whose faces give the edges (default: beside the weights)")
    args = parser.parse_args()

    positions, influences = read_weights(args.weights)
    if not positions:
        print("no weighted vertices in", args.weights)
        return 1
    tracks = read_anim(args.anim)
    if not tracks:
        print("no tracks in", args.anim)
        return 1

    bind = retarget.gf_world_bind()
    parents = retarget.GF_PARENT
    names = {i: n for n, i in retarget.JOINT_ID.items()}
    order = [n for n, _, _ in retarget.GF_NODES]

    local = {}
    for name, frames in tracks.items():
        if frames:
            local[name] = pose_at(frames, args.frame)
    world = joint_transforms(bind, parents, local, order)
    posed = skin(positions, influences, bind, world, names)

    # The mesh's own edges, read out of the .ase beside the weights.
    #
    # Nearest-neighbour pairs will not do. Two surfaces that merely lie close -
    # the inside of an arm and the ribs it rests against - separate the moment
    # the arm moves, and counted as edges that reads as a 5.6x tear when
    # nothing has torn at all. Only an edge a triangle actually owns can be
    # stretched.
    edges = mesh_edges(args.ase or default_ase(args.weights), positions)
    if not edges:
        print("no mesh edges found beside %s" % args.weights)
        return 1
    worst = []
    for i, j in edges:
        rest = math.dist(positions[i], positions[j])
        if rest < 1e-4:
            continue
        worst.append((math.dist(posed[i], posed[j]) / rest, rest, i, j))
    if not worst:
        print("nothing to measure")
        return 1
    worst.sort(reverse=True)

    stretches = [w[0] for w in worst]
    print("%s @ %s frame %d" % (os.path.basename(args.weights),
                                os.path.basename(args.anim), args.frame))
    print("  %d edges, median stretch %.3f, worst %.1fx"
          % (len(worst), stretches[len(stretches) // 2], stretches[0]))
    blame = collections.Counter()
    for ratio, _, i, j in worst[:2000]:
        if ratio < 2.0:
            break
        blame[dominant(influences[i], names)] += 1
        blame[dominant(influences[j], names)] += 1
    if blame:
        print("  joints owning the edges stretched more than 2x:")
        for name, count in blame.most_common(args.worst):
            print("    %-18s %5d" % (name, count))
    else:
        print("  nothing stretched past 2x")
    return 0


if __name__ == "__main__":
    sys.exit(main())

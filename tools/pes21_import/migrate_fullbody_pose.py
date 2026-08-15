"""Migrates a fullbody ASE from joint-local storage to base-pose world space.

The engine used to skin by rotating each vertex by its joint's ABSOLUTE
orientation, which only reconstructs the right shape if the geometry is stored
pre-counter-rotated - each vertex's offset expressed in its joint's own frame.
That convention cannot represent a vertex influenced by several joints (each
would need its own version of the same point), so imported PES models had to be
flattened onto one joint each to survive it, losing their skin weights.

HumanoidBase now rotates by the change since the base pose, which is ordinary
linear blend skinning and keeps multi-influence weights intact. Geometry
authored for the old convention has to be brought forward once:

    world = base_rot[joint] * (stored - base_pos[joint]) + base_pos[joint]

using each vertex's dominant joint, read back out of its vertex colour.

  python3 migrate_fullbody_pose.py <fullbody.ase> [--out <path>] [--dry-run]

Writes in place unless --out is given; the original is kept as <name>.preLBS.
"""

import argparse
import os
import shutil
import sys

import fmdl_to_fullbody as F


def decode_joint(color):
    """Dominant joint id from an ASE vertex colour (see encode_color)."""
    best_id, best_weight = 0, -1.0
    for channel in color:
        value = channel * 255.0
        if value <= 0.01:
            continue
        joint = int(value * 0.1)
        weight = (value - joint * 10.0) / 9.0
        if 0 <= joint < len(F.GF_JOINT_ORDER) and weight > best_weight:
            best_id, best_weight = joint, weight
    return best_id


def migrate(path, out_path=None, dry_run=False):
    base_pos, base_rot = F.gf_base_pose(want_rotations=True)

    lines = open(path).read().splitlines()
    vertices = {}   # index -> (x, y, z)
    colors = {}     # index -> (r, g, b)
    for line in lines:
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "*MESH_VERTEX":
            vertices[int(parts[1])] = tuple(float(x) for x in parts[2:5])
        elif parts[0] == "*MESH_VERTCOL":
            colors[int(parts[1])] = tuple(float(x) for x in parts[2:5])

    if not vertices or not colors:
        raise ValueError("no vertices or vertex colours in %s" % path)

    moved = {}
    for index, position in vertices.items():
        color = colors.get(index)
        if color is None:
            moved[index] = position
            continue
        joint = F.GF_JOINT_ORDER[decode_joint(color)]
        offset = tuple(p - o for p, o in zip(position, base_pos[joint]))
        rotated = F._quat_rot(base_rot[joint], offset)
        moved[index] = tuple(o + r for o, r in zip(base_pos[joint], rotated))

    shifted = sum(1 for i in vertices
                  if max(abs(a - b) for a, b in zip(vertices[i], moved[i])) > 1e-4)
    print("%s: %d vertices, %d moved" % (os.path.basename(path), len(vertices), shifted))
    if dry_run:
        return shifted

    out = []
    for line in lines:
        parts = line.split()
        if parts and parts[0] == "*MESH_VERTEX":
            index = int(parts[1])
            x, y, z = moved[index]
            indent = line[:len(line) - len(line.lstrip())]
            out.append("%s*MESH_VERTEX %d\t%.4f\t%.4f\t%.4f" % (indent, index, x, y, z))
        else:
            out.append(line)

    destination = out_path or path
    if destination == path and not os.path.exists(path + ".preLBS"):
        shutil.copy2(path, path + ".preLBS")
    open(destination, "w").write("\n".join(out) + "\n")
    return shifted


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ase", nargs="+")
    parser.add_argument("--out")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    for path in args.ase:
        migrate(path, args.out if len(args.ase) == 1 else None, args.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())

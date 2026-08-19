"""Judges whether an imported body actually clothes the rig.

A 4cc aesthetic export is not always a body. The packs override PES's slots -
boots, gloves, face - and lean on the invisible-kit trick: the character you see
is those pieces drawn over PES's own base body, with a transparent kit texture
hiding the body itself. Read such an export as a whole body and you get what a
showcase shows - a fan of wing blades with no player attached, a headless torso,
a figure framed down to a dot by a backdrop mesh 362 m across.

So the question is asked of the geometry, not of the mesh names, which are the
pack author's business: for each joint of the native rig, is there geometry near
it? Over the models that do render whole - the stock body, lcg_2709,
ateam_70201, hdg_2402 - the furthest any joint sits from its nearest vertex is
0.18 m. lcg_2702 leaves 17 of its 20 joints bare. Nothing measured falls between.

  python3 body_coverage.py <model.ase> [more.ase ...]

prints one line per model: its verdict, the joints left bare, and any geometry
outside a footballer's envelope.
"""

import argparse
import math
import os
import re
import sys

import retarget

# Geometry this far from a joint still counts as covering it. Measured: 0.18 m is
# the worst joint on a body that renders whole.
BARE_RADIUS = 0.20

# Past this from the origin, geometry is not part of a footballer. The tallest
# whole import (hdg_2421) stands 2.20 m and reaches 2.2 m out; lcg_2718's
# backdrop reaches 362 m.
STRAY_LIMIT = 4.0

VERTEX = re.compile(r'\*MESH_VERTEX\s+\d+\s+([-\d.eE+]+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)')
NODE = re.compile(r'\*NODE_NAME "([^"]+)"')


def bare_joints(vertices, bind, radius=BARE_RADIUS):
    """-> the joints with no geometry within radius, in the rig's own order."""
    order = [n for n in retarget.GF_JOINT_ORDER if n in bind] or list(bind)
    bare = []
    for name in order:
        joint = bind[name]
        if not any(math.dist(v, joint) <= radius for v in vertices):
            bare.append(name)
    return bare


def strays(vertices, limit=STRAY_LIMIT):
    """-> how many vertices sit outside a footballer's envelope."""
    return sum(1 for v in vertices if max(abs(c) for c in v) > limit)


def verdict(vertices, bind, radius=BARE_RADIUS, limit=STRAY_LIMIT):
    """-> (verdict, detail).

    "carries scenery" comes first: while a backdrop is in the file the model's
    bounds are the backdrop's, so nothing else about it can be judged on screen.
    """
    bare = bare_joints(vertices, bind, radius)
    stray = strays(vertices, limit)
    if stray:
        return "carries scenery", "%d vertex/vertices past %.0f m%s" % (
            stray, limit, "; also bare: " + ", ".join(bare) if bare else "")
    if bare:
        return "needs base", "bare: " + ", ".join(bare)
    return "whole", "every joint clothed"


def read_vertices(path):
    """-> ([(x, y, z)], [mesh names]) from an .ase."""
    vertices = []
    meshes = []
    for line in open(path, "r", errors="replace"):
        match = VERTEX.search(line)
        if match:
            vertices.append(tuple(float(g) for g in match.groups()))
            continue
        match = NODE.search(line)
        if match and (not meshes or meshes[-1] != match.group(1)):
            meshes.append(match.group(1))
    return vertices, meshes


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ase", nargs="+")
    parser.add_argument("--radius", type=float, default=BARE_RADIUS)
    args = parser.parse_args()

    bind = retarget.gf_world_bind()
    counts = {}
    for path in args.ase:
        vertices, meshes = read_vertices(path)
        call, detail = verdict(vertices, bind, args.radius)
        counts[call] = counts.get(call, 0) + 1
        print("%-16s %-15s %7d verts  %s"
              % (os.path.basename(os.path.dirname(path)) or os.path.basename(path),
                 call, len(vertices), detail))
    print("\n%s" % ", ".join("%d %s" % (n, c) for c, n in sorted(counts.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

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
ateam_70201 - the furthest any joint sits from its nearest vertex is 0.18 m.
lcg_2702 leaves 17 of its 20 joints bare. Nothing measured falls between.

hdg_2402 used to be cited here as a fourth whole-body reference and it is not
one: its pack folder is "k2402 - Helldiver Headless" and it ships no head at
all. Proximity did not notice, because a collar ring sits 0.083 m under the
head joint - so the model calibrating the threshold was itself the case the
threshold misses. See HEAD_MIN_VERTICES for the check that does catch it.

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

# Whether a head is actually there cannot be asked by proximity, which is why
# this check exists separately. hdg_2402 ships no head at all - its pack folder
# is "k2402 - Helldiver Headless" and the head comes from PES's base body - yet
# its nearest vertex to the head joint is 0.083 m, well inside BARE_RADIUS,
# because an open collar ring sits right under the joint. It passed as "whole".
#
# A head is geometry ABOVE the head joint; a collar is not. Counting vertices
# past the joint along the neck->head axis separates them cleanly: measured,
# hdg_2402 has 0 and hdg_2411 - the same pack plus three head meshes - has
# 1277, with dbg_2009 at 11190 and dbg_2014 at 9666.
#
# The same trick does not work for hands and is deliberately not attempted: the
# wrist axis runs down the arm, so a sleeve scores the same as a fist (hdg_2402
# and hdg_2411 both report 3069 past the left wrist, and only one of them has
# hands). The finger joints cannot help either - retarget notes they are
# parentless in the source skeleton.
HEAD_DISTAL_MARGIN = 0.04
HEAD_MIN_VERTICES = 32

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


def head_vertex_count(vertices, bind, margin=HEAD_DISTAL_MARGIN):
    """-> how many vertices sit above the head joint, along the neck->head axis.

    Zero means the model has no head of its own and is drawn over PES's base
    body. Proximity cannot answer this: a collar sits at the joint too.
    """
    if "head" not in bind:
        return None
    parent = retarget.GF_PARENT.get("head")
    if not parent or parent not in bind:
        return None
    base, tip = bind[parent], bind["head"]
    axis = [tip[i] - base[i] for i in range(3)]
    length = math.sqrt(sum(c * c for c in axis))
    if not length:
        return None
    axis = [c / length for c in axis]
    return sum(1 for v in vertices
               if sum((v[i] - tip[i]) * axis[i] for i in range(3)) > margin)


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
    # Asked after the joint sweep because it is the case the sweep cannot see:
    # every joint can be clothed while the head is missing entirely.
    heads = head_vertex_count(vertices, bind)
    if heads is not None and heads < HEAD_MIN_VERTICES:
        return "needs base", ("no head of its own (%d vertices above the head "
                              "joint); drawn over PES's base body" % heads)
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

    bind = retarget.gf_world_render_bind()
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

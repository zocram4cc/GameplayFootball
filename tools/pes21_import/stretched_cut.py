"""Deciding which triangles are genuinely stretched, per mesh rather than per metre.

The cut used to be absolute: any face with an edge over --max-edge went, and
import_team passed 0.15 m. That works on a fine mesh and destroys a coarse one, which
is what the imported models show. Over the 90 player models in the tree, 44 have their
longest surviving edge sitting exactly on 0.15 - a distribution pressed flat against a
threshold is one that was cut - and nine are coarse meshes where 0.15 m is only 1.6x to
3.6x their median edge. lcg_2715's median edge is 0.0955 m: at 1.6x the median you
cannot remove outliers, only geometry.

The shards the cut was written for are a different order of thing: up to 1.25 m on a
1.8 m body against a median of 1.9 cm, sixty-five times the median. A triangle joining
a hand to a hairtip is not 1.6x the ordinary edge length.

So the threshold follows the mesh. Twenty times its own median edge is far past any
coarse mesh's spread and far under a shard, and a floor keeps a mesh of millimetre
detail from having its ordinary features called shards.
"""

import math

# Edges beyond this many median edges are shards, not geometry.
RATIO = 20.0
# A shard also spans a large part of the thing it is wrongly joining up. On a coarse
# mesh the ratio alone is not enough - a 0.1 m median puts a 1.4 m shard at only 14x -
# so an edge past this fraction of the model's own diagonal is stretched too.
SPAN_FRACTION = 0.35
# And never cut below this, whatever the median: a mesh of millimetre triangles has
# ordinary features a centimetre or two long.
FLOOR = 0.08
# The invariant that matters more than either test: a cut must never fall below a
# mesh's own ordinary edge length. The span test alone breaks this - 0.35 of a small
# patch's diagonal is under its own edges - and a cut that removes typical geometry is
# the bug this whole module exists to fix.
MIN_MULTIPLE = 2.0


def threshold(edges, span=0.0):
    """-> the edge length beyond which a face is stretched, or 0 to cut nothing.

    `span` is the model's bounding-box diagonal, when it is known. Both tests apply
    and the tighter wins, because a shard is either far longer than the mesh's own
    edges or a large part of the whole model, and on a coarse mesh only the second
    catches it.
    """
    lengths = sorted(e for e in edges if e > 0.0)
    if not lengths:
        return 0.0
    median = lengths[len(lengths) // 2]
    if median <= 0.0:
        return 0.0
    cut = median * RATIO
    if span > 0.0:
        cut = min(cut, span * SPAN_FRACTION)
    return max(cut, median * MIN_MULTIPLE, FLOOR)


def limit_for(faces):
    """-> the threshold for this list of triangles, or 0 to cut nothing.

    The same decision keep() makes, for a caller that already has its own face list to
    filter and only wants the number.
    """
    if not faces:
        return 0.0
    edges = []
    for a, b, c in faces:
        edges.append(math.dist(a, b))
        edges.append(math.dist(b, c))
        edges.append(math.dist(c, a))
    points = [p for tri in faces for p in tri]
    lo = [min(p[i] for p in points) for i in range(3)]
    hi = [max(p[i] for p in points) for i in range(3)]
    return threshold(edges, math.dist(lo, hi))


def keep(faces):
    """-> (faces worth keeping, how many were dropped, the threshold used).

    `faces` is a list of triangles, each three (x, y, z) points.
    """
    if not faces:
        return [], 0, 0.0
    cut = limit_for(faces)
    if cut <= 0.0:
        return list(faces), 0, 0.0
    kept = [t for t in faces
            if max(math.dist(t[0], t[1]), math.dist(t[1], t[2]),
                   math.dist(t[2], t[0])) <= cut]
    return kept, len(faces) - len(kept), cut

"""Making the parts of a body agree where they cover the same place.

A PES player is not one skin. It is a dozen shells - shirt, sleeves, arms, hands,
shorts, thighs, socks, boots, neck, face - that overlap rather than meet, and each is
weighted on its own. Measured on the stock body, `shirt` and `sleeves` share not one
vertex position, but 328 of the shirt's vertices lie within 20 mm of a sleeve vertex:
the sleeve is nested inside the shirt across the shoulder.

Over that overlap they disagree. 198 of those 328 pairs name a different set of
joints - the shirt at the shoulder is chest 0.89 plus clavicle 0.11 where the sleeve
at the same place is chest 0.44 plus shoulder 0.56 - so when the shoulder turns, the
sleeve swings, the shirt stays, and one surface comes through the other. Under the
arm it is 45% of the pairs at the right wrist and 12% where the boots meet the socks.

PES gets away with it because it is not this engine's skinning, and because a shell
that pokes through another for a frame is cheap to hide with art. Here it reads as a
seam between the arm and the body.

The rule is that a skin weight belongs to a place on the body rather than to the
garment covering it. Two parts at the same place therefore have to say the same thing,
and the least destructive way to make them is to blend what they already say, by
distance, and only where they actually overlap. Inside a part nothing is touched: how
an artist weighted a surface across itself is the artist's business.
"""

import math

# How many bones a blended vertex may end up on. PES's own maximum, measured over
# the base package's parts (14,175 vertices: 1, 2, 3 or 4 non-zero bone weights,
# never a fifth), and what the sidecar weight file carries
# (fmdl_to_fullbody.MAX_INFLUENCES). The vertex colours still take the strongest
# three of them, so a blend that produces four is not truncated by surprise at the
# wrong end - encode_color picks, and the sidecar keeps all four.
MAX_INFLUENCES = 4

# How far apart two surfaces can be and still be the same place on the body. The
# shoulder's shells sit 3 to 20 mm apart, and 20 mm is under half the thickness of the
# arm, so nothing on the far side of a limb is reachable.
DEFAULT_RADIUS = 0.02

# A surface's own say, against a neighbour touching it. Equal: at the same place they
# have the same claim on what that place does.
_OWN_SHARE = 1.0


def _cells(position, radius):
    return (int(math.floor(position[0] / radius)),
            int(math.floor(position[1] / radius)),
            int(math.floor(position[2] / radius)))


def _index(parts, radius):
    """-> {cell: [(part, vertex)]} for every vertex, at the search radius."""
    grid = {}
    for p, part in enumerate(parts):
        for v, (position, _) in enumerate(part):
            grid.setdefault(_cells(position, radius), []).append((p, v))
    return grid


def _shares_a_joint(joints, other):
    """Whether two vertices are driven by any of the same bones.

    The guard on the whole idea. Being 2 cm apart is not enough to be the same place
    on a body: in the bind pose a player's arms hang beside his ribs, so the `arms`
    shell is within a centimetre or two of the `shirt` all down his side, and blending
    those would drag his chest along with his elbow. Two surfaces that genuinely cover
    one place always have something in common to start with - the shoulder's two
    shells share `chest`, the wrist's share the elbow - and two that share nothing are
    different limbs passing close.
    """
    if not joints or not other:
        return False
    return bool({joint for joint, _ in joints} & {joint for joint, _ in other})


def _neighbours(parts, grid, radius, p, position, joints=None):
    """-> [(distance, joints)] from every OTHER part within the radius.

    With `joints`, only those sharing an influence with it (see _shares_a_joint).
    """
    found = []
    base = _cells(position, radius)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                cell = (base[0] + dx, base[1] + dy, base[2] + dz)
                for (op, ov) in grid.get(cell, ()):
                    if op == p:
                        continue
                    other, other_joints = parts[op][ov]
                    distance = math.dist(position, other)
                    if distance > radius:
                        continue
                    if joints is not None and not _shares_a_joint(joints, other_joints):
                        continue
                    found.append((distance, other_joints))
    return found


def agree(parts, radius=DEFAULT_RADIUS):
    """-> the same parts, with overlapping vertices sharing one blend.

    `parts` is a list of meshes, each a list of (position, [(jointID, weight)]).
    Returned in the same shape, so a caller can hand over what it was going to write
    and write the result instead. A vertex with no neighbour in another part comes
    back untouched, which is all of them away from a seam.
    """
    grid = _index(parts, radius)
    out = []
    for p, part in enumerate(parts):
        blended = []
        for position, joints in part:
            near = _neighbours(parts, grid, radius, p, position, joints)
            if not near:
                blended.append((position, joints))
                continue
            # Its own say counts as much as a neighbour sitting on top of it, and the
            # rest fall away linearly to nothing at the radius: a surface 2 cm off is
            # not the same place and should not move this one.
            #
            # The falloff has to be bounded. An inverse-distance one drowns every
            # neighbour in the vertex's own say - measured on the stock body it moved
            # no weight by more than 0.09 and left two thirds of the disagreement
            # standing.
            total = {}
            for joint, weight in joints:
                total[joint] = total.get(joint, 0.0) + weight * _OWN_SHARE
            for distance, other in near:
                share = max(0.0, 1.0 - distance / radius)
                for joint, weight in other:
                    total[joint] = total.get(joint, 0.0) + weight * share
            blended.append((position, _top(total)))
        out.append(blended)
    return out


def _top(total):
    """-> the strongest MAX_INFLUENCES joints, renormalised to sum to one."""
    ranked = sorted(total.items(), key=lambda item: (-item[1], item[0]))[:MAX_INFLUENCES]
    scale = sum(weight for _, weight in ranked)
    if scale <= 0.0:
        return []
    return [(joint, weight / scale) for joint, weight in ranked]


# A UV seam duplicates a vertex: same position, two entries, because the two
# faces need different texture coordinates. They are the same place on the body
# and must skin identically - and did not, because the weights are guessed per
# vertex and a tie between two joints is broken by the last bit of a float.
# Measured on lcg_2709: v16033 and v16034 sit at the same millimetre and were
# weighted `right_shoulder 0.29` and `right_clavicle 0.29`, so the bake moved
# one 0.15 m and left the other - a 0.5 mm edge stretched 315x, which on screen
# is the long white shard fanning out of the model. reconcile() could not see
# it: it agrees a vertex with its neighbours in OTHER parts, and a seam
# duplicate is in the same part.
#
# Five millimetres, measured rather than assumed. A duplicate is nominally the
# same coordinate, but the conversion scales and rounds, so the two halves of
# one seam arrive up to 3 mm apart (2hug_1869: 1.1 mm, lcg_2709: 2.4 and
# 3.2 mm). At 1 mm the pass welded a third of what tears; 5 mm covers every
# pair measured and is still a quarter of the radius the reconcile side treats
# as "the same place" (20 mm). Vertices this close that share no bone at all
# are left alone - see _shares_a_joint, the same guard reconcile uses.
WELD_RADIUS = 0.005

# Within this, two vertices are the same coordinate and nothing more needs to be
# true about them. Past it, up to WELD_RADIUS, they must already share a bone.
COINCIDENT_RADIUS = 0.001


def _edge_neighbours(count, faces):
    """-> a set of directly connected vertices per vertex.

    The discriminator between a seam's duplicate and the mesh's own sampling is
    whether an EDGE joins the two. A duplicate has none: the seam cut the
    surface and both halves were emitted separately, at one coordinate. The
    next vertex along an authored gradient is joined by an edge by definition.

    Connected components were tried first and are useless here: a UV seam cuts
    a LINE across a surface, so the sheet stays one component and the pass
    welded nothing at all (measured: four teams re-imported, zero vertices
    welded, every shard back).
    """
    adjacent = [set() for _ in range(count)]
    for tri in faces:
        if len(tri) < 3:
            continue
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            if a == b or a >= count or b >= count or a < 0 or b < 0:
                continue
            adjacent[a].add(b)
            adjacent[b].add(a)
    return adjacent


def weld(parts, radius=WELD_RADIUS, faces=None):
    """Influence lists with duplicated vertices in the same part made identical.

    Same shape in and out, like reconcile(). A vertex within `radius` of one in
    another triangle run of its own part gets the sum of their influences, so a
    UV seam can no longer skin its two halves to two different joints.

    `faces` is one triangle list per part, indices into that part. Without it
    only exact duplicates (COINCIDENT_RADIUS) are welded, because distance
    alone cannot tell a seam duplicate from the mesh's own spacing: these
    characters are dense enough that 5 mm reaches the next authored vertex, and
    a transitive union over that band swallowed whole limbs - measured on
    lcg_2709, a 2,544-vertex hand carrying 1,900 authored blends came out with
    one, which is a rigid hand under HandRig instead of a torn one.

    Grouped by proximity and not by a rounded coordinate: a grid cell has
    boundaries, and the first version of this bucketed on round(x / radius),
    which left lcg_2709's 0.52 mm pair in two different cells and welded
    nothing at all. Union over the neighbouring cells, which is what the
    reconcile side already does.
    """
    out = []
    for index, part in enumerate(parts):
        part_faces = faces[index] if faces is not None and index < len(faces) else None
        adjacent = _edge_neighbours(len(part), part_faces) if part_faces is not None else None
        grid = {}
        for v, (position, _) in enumerate(part):
            grid.setdefault(_cells(position, radius), []).append(v)
        # Union-find over vertices that touch, so a coordinate covered by three
        # runs ends up in one group rather than each pair agreeing separately.
        parent = list(range(len(part)))
        # ...but bounded: every member of a group is within `radius` of the
        # vertex that started it, so a run of near neighbours cannot chain one
        # blend across a whole hand.
        seed = [position for position, _ in part]

        def find(a):
            while parent[a] != a:
                parent[a] = parent[parent[a]]
                a = parent[a]
            return a

        for v, (position, _) in enumerate(part):
            base = _cells(position, radius)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for other in grid.get((base[0] + dx, base[1] + dy, base[2] + dz), ()):
                            if other <= v:
                                continue
                            distance = math.dist(position, part[other][0])
                            if distance > radius:
                                continue
                            if distance > COINCIDENT_RADIUS:
                                # Past a coordinate's worth of rounding, two
                                # surfaces that genuinely cover one place always
                                # have a bone in common; two that share nothing
                                # are different limbs passing close.
                                if not _shares_a_joint(part[v][1], part[other][1]):
                                    continue
                                # An edge between them means they are two
                                # samples of one surface, not a seam's two
                                # halves: that is the artist's gradient, and
                                # agreeing it is what flattened the hands.
                                # Without the faces to say so, leave it.
                                if adjacent is None or other in adjacent[v]:
                                    continue
                            ra, rb = find(v), find(other)
                            if ra == rb:
                                continue
                            if math.dist(seed[ra], seed[rb]) > radius:
                                continue
                            parent[ra] = rb
        totals = {}
        for v, (_, joints) in enumerate(part):
            total = totals.setdefault(find(v), {})
            for joint, weight in joints:
                total[joint] = total.get(joint, 0.0) + weight
        agreed = {root: _top(total) for root, total in totals.items()}
        out.append([(position, agreed[find(v)] or joints)
                    for v, (position, joints) in enumerate(part)])
    return out


def disagreement(parts, radius=DEFAULT_RADIUS):
    """-> how many vertices name a different set of joints from a neighbour.

    What the seam is measured in, before and after.
    """
    grid = _index(parts, radius)
    count = 0
    for p, part in enumerate(parts):
        for position, joints in part:
            mine = {joint for joint, _ in joints}
            for _, other in _neighbours(parts, grid, radius, p, position, joints):
                if {joint for joint, _ in other} != mine:
                    count += 1
                    break
    return count


# How many times to run the blend. One pass leaves the two surfaces closer but not
# together - each vertex still counts its own say first - and on the stock body the
# mean disagreement across overlapping surfaces goes 0.146, 0.097, 0.073, 0.065,
# 0.061. Three is where it stops being worth the smoothing: every pass also spreads
# weight one radius further along a surface, and enough of them would soften the
# elbow along with the seam.
DEFAULT_PASSES = 3


def reconcile(parts, radius=DEFAULT_RADIUS, passes=DEFAULT_PASSES):
    """Influence lists, with overlapping vertices agreed.

    `parts` is [[(position, [(jointID, weight)])]] - one list per body part, the
    weights themselves, before anything has been squeezed into a vertex colour.
    Returned in the same shape, so a caller hands over what it was going to write
    and writes the result instead. A vertex with no neighbour in another part comes
    back untouched, which is all of them away from a seam.

    The weights and not the colours, now the fingers are rigged: a hand vertex can
    be on joint 44 and the colour a glove vertex beside it carries cannot say so,
    so blending what the colours hold would reconcile the two surfaces onto the
    fallback wrist instead of onto the finger.

    Kept here rather than in either writer because both of them have the problem:
    pes_base_body assembles PES's own shells into the stock body, and
    fmdl_to_fullbody composites an imported character over it.
    """
    agreed = parts
    for _ in range(max(1, passes)):
        agreed = agree(agreed, radius=radius)
    return agreed


def reconciled_count(before, after):
    """-> (vertices changed, vertices that changed which bone drives them).

    The second number is the one to watch. Softening a seam is the point; moving a
    vertex onto a different bone is not, and enough passes of any smoothing will do
    it.
    """
    moved = migrated = 0
    for a, b in zip(before, after):
        for (_, was), (_, now) in zip(a, b):
            if was == now:
                continue
            moved += 1
            if was and now and was[0][0] != now[0][0]:
                migrated += 1
    return moved, migrated

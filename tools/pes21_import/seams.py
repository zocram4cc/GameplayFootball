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


def reconcile_skins(parts, radius=DEFAULT_RADIUS, passes=DEFAULT_PASSES):
    """Influence lists, with overlapping vertices agreed.

    `parts` is [[(position, [(jointID, weight)])]] - the weights themselves,
    before anything has been squeezed into a vertex colour. That matters now the
    fingers are rigged: a hand vertex can be on joint 44 and the colour a glove
    vertex beside it carries cannot say so, so blending what the colours hold
    would reconcile the two surfaces onto the fallback instead of onto the finger.
    """
    agreed = parts
    for _ in range(max(1, passes)):
        agreed = agree(agreed, radius=radius)
    return agreed


def reconcile(parts, radius=DEFAULT_RADIUS, passes=DEFAULT_PASSES):
    """Body parts as the ASE writers hold them, with their seam weights agreed.

    `parts` is [(name, vertices, faces)] and a vertex is the writer's own tuple,
    (position, uv, colour) or (position, uv, colour, normal) - only the colour
    changes, and only for vertices that overlap another part, so whatever else the
    tuple carries comes back untouched.

    Kept here rather than in either writer because both of them have the problem:
    pes_base_body assembles PES's own shells into the stock body, and
    fmdl_to_fullbody composites an imported character over it.
    """
    from fmdl_to_fullbody import decode_color, encode_color

    fields = [[(vertex[0], decode_color(vertex[2])) for vertex in vertices]
              for _, vertices, _ in parts]
    agreed = fields
    for _ in range(max(1, passes)):
        agreed = agree(agreed, radius=radius)
    out = []
    for (name, vertices, faces), blended in zip(parts, agreed):
        rebuilt = [vertex[:2] + (encode_color(joints),) + tuple(vertex[3:])
                   for vertex, (_, joints) in zip(vertices, blended)]
        out.append((name, rebuilt, faces))
    return out


def reconciled_count(before, after):
    """-> (vertices changed, vertices that changed which bone drives them).

    The second number is the one to watch. Softening a seam is the point; moving a
    vertex onto a different bone is not, and enough passes of any smoothing will do
    it.
    """
    from fmdl_to_fullbody import decode_color

    moved = migrated = 0
    for (_, a, _), (_, b, _) in zip(before, after):
        for va, vb in zip(a, b):
            if va[2] == vb[2]:
                continue
            moved += 1
            was = decode_color(va[2])
            now = decode_color(vb[2])
            if was and now and was[0][0] != now[0][0]:
                migrated += 1
    return moved, migrated

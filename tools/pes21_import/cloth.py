"""The cloth in a pack: nets, flags, banners, pennants, and what each needs.

Every piece of cloth here was handled ad hoc and each broke differently.

  Goal netting     comes out solid and right. The one that already works.
  Corner flag      its cloth was authored at z 1.227..1.570 off a 1.554 m pole, and
                   grounding each mesh of a prop separately dropped it to the floor.
                   Fixed by measuring a prop's placement once, over all its meshes
                   (stadium_props.prop_placement).
  banner, pennant  PES authors them flat, at z 0.016 and 0.017, and positions them
                   from demo choreography this import does not apply - so they lie on
                   the grass instead of being carried.

This module holds the part they share: a two-sided sheet's texture mapping.

A corner flag is one quad built as two sheets so it can be seen from either side, and
on props_09 those sheets are mapped to different halves of the texture - 16 faces at
v < 0.63 on the red/yellow checker and 16 at v >= 0.63 on the pole strip, with areas
of 0.0697 and 0.0689 square metres and the same largest face. Identical areas is what
says they are front and back of one cloth rather than a cloth and a bracket, and the
back showing the pole strip is the grey band and dark disc that appear from the wrong
side.

A real corner flag is printed on both sides, so the back sheet takes the front's art.
Each back corner is matched to the front vertex at the same position and copies its
UV, which needs no guess about the offset PES intended.
"""

# How close two sheets' areas have to be to count as the two sides of one cloth. The
# corner flag's are 0.0697 and 0.0689, within 2%; a cloth and a fitting are not close.
TWO_SIDED_AREA_TOLERANCE = 0.10


def looks_two_sided(area_a, area_b):
    """Do these two sheets look like the front and back of one cloth?"""
    if area_a <= 0.0 or area_b <= 0.0:
        return False
    larger = max(area_a, area_b)
    return abs(area_a - area_b) / larger <= TWO_SIDED_AREA_TOLERANCE


# How unequal two regions may be and still be the two sides of one cloth. The flag's
# are 16 faces each at 0.0697 and 0.0689 square metres; the pole's are 114 and 160
# faces at 0.0421 and 0.2637, and it must not qualify.
COUNT_TOLERANCE = 0.25
AREA_TOLERANCE = 0.25


def regions_are_two_sided(count_a, area_a, count_b, area_b):
    """Do these two texture regions look like the front and back of one cloth?

    The gate that makes match_mesh_uvs safe to use on a whole mesh. Ungated it repaints
    the corner flag's pole, which legitimately uses the same strip the flag's back sheet
    wrongly samples.
    """
    if count_a <= 0 or count_b <= 0 or area_a <= 0.0 or area_b <= 0.0:
        return False
    if abs(count_a - count_b) / max(count_a, count_b) > COUNT_TOLERANCE:
        return False
    return abs(area_a - area_b) / max(area_a, area_b) <= AREA_TOLERANCE


def _region_key(sheet):
    """Which part of the texture a sheet sits in: its mean V, rounded."""
    if not sheet:
        return None
    return round(sum(uv[1] for _, uv in sheet) / len(sheet), 2)


def match_two_sided_uvs(sheets):
    """-> the sheets with every one of them carrying the majority region's art.

    `sheets` is a list of sheets, each a list of (position, uv) corners. A corner
    whose position the majority region does not have keeps the UV it had: never
    invent one for a vertex the other side does not share.
    """
    if len(sheets) < 2:
        return list(sheets)

    # The majority region wins. With a straight tie the first sheet's does, which
    # keeps the result settled rather than depending on dict order.
    counts = {}
    for sheet in sheets:
        key = _region_key(sheet)
        counts[key] = counts.get(key, 0) + 1
    best = max(counts.items(), key=lambda kv: (kv[1], kv[0] == _region_key(sheets[0])))[0]

    by_position = {}
    for sheet in sheets:
        if _region_key(sheet) != best:
            continue
        for position, uv in sheet:
            by_position.setdefault(position, uv)

    out = []
    for sheet in sheets:
        if _region_key(sheet) == best:
            out.append(list(sheet))
            continue
        out.append([(position, by_position.get(position, uv)) for position, uv in sheet])
    return out


# How far apart two faces' mean V has to be before they count as different regions of
# the texture. The corner flag's sheets sit at about 0.45 and 0.86.
REGION_SPLIT = 0.2


# How wide the gap between two clusters of V has to be to count as two regions of the
# texture rather than one spread-out region. The flag's clusters sit at about 0.45 and
# 0.86, a gap of near 0.3.
REGION_GAP = 0.15


def split_regions(means):
    """-> (indices of the lower cluster, indices of the upper), by the largest gap.

    Bucketing on a fixed grid was wrong: the flag's 32 faces fall into five buckets at
    0.2 because their V spreads across 0.266..0.991. The boundary is learned instead -
    sort, cut at the widest gap, and only call it two regions if that gap is wide
    enough to be one.
    """
    if not means:
        return [], []
    order = sorted(range(len(means)), key=lambda i: means[i])
    best_gap = 0.0
    best_at = -1
    for k in range(len(order) - 1):
        gap = means[order[k + 1]] - means[order[k]]
        if gap > best_gap:
            best_gap = gap
            best_at = k
    if best_gap < REGION_GAP or best_at < 0:
        return list(order), []
    return order[:best_at + 1], order[best_at + 1:]


def _face_area(face):
    if len(face) < 3 or any(len(corner) < 2 for corner in face[:3]):
        return 0.0
    a, b, c = (corner[0] for corner in face[:3])
    u = [b[i] - a[i] for i in range(3)]
    v = [c[i] - a[i] for i in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    return 0.5 * sum(x * x for x in n) ** 0.5


def match_mesh_uvs(faces, gated=False):
    """-> (faces with every one on the majority texture region, how many moved).

    Gate it with gated=True. Ungated it repaints the corner flag's pole, and why is The
    pole legitimately uses both halves of cf_common_bsm - its grey band lives in the
    same bottom strip the flag's back sheet wrongly samples - so this rule, run over
    the pole's 274 faces, moves 176 of them and repaints it. It is only correct for a
    sheet pair that looks_two_sided() agrees on: equal areas, opposite windings.
    worth keeping: the pole legitimately uses both halves of cf_common_bsm.

    Gated it is correct - on the real flag mesh it splits 16/16 at areas 0.0697 and
    0.0689 and moves 16 faces - but it CANNOT YET BE WIRED INTO stadium_staff. That
    writer keeps one UV per vertex, and the flag's two sheets share 9 of their 18
    positions, so writing a back-face corner's UV overwrites the front's. The writer has
    to emit TVERTs per face corner first, which is exactly what adboard_uvs.py does for
    the advertising ring; until then this runs correctly and changes nothing.

    `faces` is a list of faces, each a list of (position, uv) corners - which is what
    a mesh with a shared UV pool comes out as. Faces are grouped by their mean V, the
    largest group's art wins, and a corner whose position that group does not have
    keeps the UV it had.
    """
    if not faces:
        return [], 0

    def mean_v(face):
        return sum(uv[1] for _, uv in face) / len(face) if face else 0.0

    groups = {}
    for face in faces:
        key = round(mean_v(face) / REGION_SPLIT)
        groups.setdefault(key, []).append(face)
    if len(groups) < 2:
        return [list(f) for f in faces], 0

    if gated:
        # Only a mesh whose two regions look like one cloth seen from both sides.
        means = [sum(uv[1] for _, uv in f) / len(f) if f else 0.0 for f in faces]
        low, high = split_regions(means)
        if not high:
            return [list(f) for f in faces], 0
        if not regions_are_two_sided(
                len(low), sum(_face_area(faces[i]) for i in low),
                len(high), sum(_face_area(faces[i]) for i in high)):
            return [list(f) for f in faces], 0
        # The larger region's art wins, matched by position.
        keep, move = (low, high) if len(low) >= len(high) else (high, low)
        by_position = {}
        for i in keep:
            for position, uv in faces[i]:
                by_position.setdefault(position, uv)
        out = [list(f) for f in faces]
        moved = 0
        for i in move:
            out[i] = [(p, by_position.get(p, uv)) for p, uv in faces[i]]
            moved += 1
        return out, moved

    best = max(groups.items(), key=lambda kv: len(kv[1]))[0]
    by_position = {}
    for face in groups[best]:
        for position, uv in face:
            by_position.setdefault(position, uv)

    out = []
    moved = 0
    for face in faces:
        if round(mean_v(face) / REGION_SPLIT) == best:
            out.append(list(face))
            continue
        out.append([(position, by_position.get(position, uv)) for position, uv in face])
        moved += 1
    return out, moved

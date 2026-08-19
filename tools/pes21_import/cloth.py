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


def match_mesh_uvs(faces):
    """-> (faces with every one on the majority texture region, how many moved).

    NOT SAFE TO APPLY TO A WHOLE MESH UNGATED, and the corner flag pole is why. The
    pole legitimately uses both halves of cf_common_bsm - its grey band lives in the
    same bottom strip the flag's back sheet wrongly samples - so this rule, run over
    the pole's 274 faces, moves 176 of them and repaints it. It is only correct for a
    sheet pair that looks_two_sided() agrees on: equal areas, opposite windings.
    Gate it before wiring it into an importer.

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

"""Assembles PES 2021's own in-game player body as GameplayFootball's
default fullbody model.

PES has no single player mesh: the match player is composed from the base
character package plus per-slot parts. The pieces used here, and where they
come from (proprietary sources stay under the PES install):

  dt00_x64.cpk(.bak!)  Asset/model/character/#Win/common_package.fpk
      common/pants_out_sub.fmdl   kit shorts       (diffuse = runtime kit)
      common/socks_middle.fmdl    kit socks        (diffuse = runtime kit)
      common/arm.fmdl             bare arms        ("arm_mat" meshes)
      common/thigh_short.fmdl     bare thighs      ("thigh_mat")
      common/hand_l/r.fmdl        hands            ("arm_mat")
      common/neck.fmdl            neck skin        ("skin_head")
      common/eye.fmdl             eyeballs         ("eye_mat" - the face
                                  mesh has an open socket, so without these
                                  you see straight through the head)
  dt32_g4.cpk  Asset/model/character/parts/undershirt/scenes/#Win/
      undershirt.fmdl             kit shirt        ("torso_mat" meshes -
                                  PES fills their empty diffuse slot with
                                  the team kit texture at runtime)
  boots slot (4cc stockkit k0001 or any real boots fpk)
      boots.fmdl                  shoes
  dt36_g4.cpk  face/real/<id>/#Win/face.fpk
      face_high.fmdl              head (own painted texture; also the
                                  source of faceweights.txt for FaceRig)

NOTE the .bak: the live dt00_x64.cpk was edited by 4CC for kit-body
transparency; extract the pristine dt00_x64.cpk.bak.

Texturing on the GF side:
  * kit pieces are RE-UV'd onto GF's kit template layout so every existing
    team kit PNG keeps working: the stock (migrated) fullbody.ase is the
    Rosetta stone - its kit-material vertices pair positions with template
    UVs, and each PES vertex takes an inverse-distance-weighted UV from its
    panel's nearest stock vertices (panels: shirt front/back, shorts
    front/back, sock left/right, classified by facing and position).
  * skin pieces reference skin.jpg, which the engine swaps for the
    player's flat skin-tone texture (UVs are irrelevant on a flat tone).
  * the face keeps its own painted texture; boots theirs.

Skin weights ride vertex colours exactly as fmdl_to_fullbody: PES bones
resolve through retarget.resolve_bone (1:1 for animated bones, lossless
collapse for helpers), top-3 influences renormalized.

  python3 pes_base_body.py --common <dir> --undershirt <fmdl> --boots <fmdl> \
      --face <fmdl> --face-texture <png> --boots-texture <png> \
      --stock <migrated fullbody.ase> --fmdl-lib <pes-fmdl> <out_dir>

Writes <out>/fullbody_pes.ase + <out>/fullbody_pes.object, plus
faceweights.txt for the engine FaceRig (expressions ship separately).
"""

import argparse
import math
import os
import re
import sys

import ase_util
import retarget
import face_weights
import seams
import pes_skl
from fmdl_to_fullbody import (count_finger_lines, vertex_joints, encode_color,
                              build_bone_map, weights_path, write_sidecar)


def source_uv(vertex_uv):
    """The UV a PES vertex was authored with, in ASE convention.

    PES counts v downward and the ASE upward, so the flip is the whole
    conversion. Skin pieces have always used this; kit pieces do too now that
    PES's uniform layout is the one the engine maps to.
    """
    return (vertex_uv.u, 1.0 - vertex_uv.v) if vertex_uv is not None else (0.0, 0.0)


def kit_vertex_uv(mode, vertex_uv, synthesise):
    """UV for a kit vertex: the authored one, or one built for GF's template.

    "native" is the standard - a team's u0<team>p<n> sheet is laid out for
    PES's own mapping, so keeping the authored UVs is what puts an imported
    kit's front panel on the front of the shirt. Under the template mapping
    the shirt sampled the sleeve regions instead, and the synthesised sleeve
    flaps distorted the shoulders into the bargain.

    `synthesise` is only called in template mode; building a template UV is
    the expensive part of the build and there is no reason to pay for it.
    """
    if mode == "native":
        return source_uv(vertex_uv)
    return synthesise()


# --- stock kit UV harvest -----------------------------------------------------

_V_RE = re.compile(r"\*MESH_VERTEX\s+(\d+)\t([-\d.e]+)\t([-\d.e]+)\t([-\d.e]+)")
_T_RE = re.compile(r"\*MESH_TVERT\s+(\d+)\t([-\d.e]+)\t([-\d.e]+)")
_F_RE = re.compile(r"\*MESH_FACE\s+(\d+):\s+A:\s+(\d+)\s+B:\s+(\d+)\s+C:\s+(\d+)")
_TF_RE = re.compile(r"\*MESH_TFACE\s+(\d+)\t(\d+)\t(\d+)\t(\d+)")

# GF kit template layout (ASE v runs bottom-up), MEASURED off the shipped
# kit art rather than eyeballed: one continuous body suit per column - the
# shirt with the shorts painted directly below it - front column u < 0.5,
# back column u > 0.5, sleeves on T-flaps at the top corners, socks on the
# two mid-left rectangles.
#
#   front column   body u 0.105..0.394, full width (with sleeves) 0.004..0.495
#   back  column   body u 0.605..0.894, full width            0.504..0.995
#   waist seam v 0.590   shorts hem v 0.432   collar v 0.988
#   the sleeve flap starts where the column widens, v ~0.86
#
# Both columns are 0.1445 wide either side of their centre, so the body
# panel and the sleeve flap MEET exactly at the column's body edge - which
# is what keeps the shoulder continuous instead of jumping across the sheet.
COLUMN_CENTRE = {"front": 0.2495, "back": 0.7495}
# Every island is held UV_MARGIN clear of the printed edge. Sitting exactly
# on it is not enough: bilinear filtering and mipmaps reach past the border
# and pull in the black gap between the two columns, which drew a dark line
# down each flank and a stripe down every sock.
UV_MARGIN = 0.004
BODY_HALF_U = 0.1445 - UV_MARGIN
# which way u runs with the player's +x: the back column is a mirror
COLUMN_X_SIGN = {"front": 1.0, "back": -1.0}
# each garment's own band of the column
GARMENT_V = {"shirt": (0.594, 0.988 - UV_MARGIN),
             "shorts": (0.440 + UV_MARGIN, 0.588)}
# the template's shirt-hem / shorts-waist seam, in ASE v
KIT_WAIST_V = 0.590
# Sleeve flaps, (u at the top of the arm -> u at the bottom). The first
# value is the column's body edge, so the sleeve continues the body panel.
# The old table had the right-hand flaps at 0.313..0.410 / 0.586..0.684,
# INSIDE the body panel, so each sleeve was painted with a strip of chest.
SLEEVE_FLAP_U = {
    ("front", True): (0.2495 + BODY_HALF_U, 0.495 - UV_MARGIN),
    ("front", False): (0.2495 - BODY_HALF_U, 0.004 + UV_MARGIN),
    ("back", True): (0.7495 - BODY_HALF_U, 0.504 + UV_MARGIN),
    ("back", False): (0.7495 + BODY_HALF_U, 0.995 - UV_MARGIN),
}
# hem .. shoulder end of the flap. The flap is a T, not a rectangle: the
# column only reaches full width above v ~0.86, so a lower hem ran the
# sleeve off the printed fabric and rendered black wedges along the arms.
SLEEVE_V = (0.86 + UV_MARGIN, 0.988 - UV_MARGIN)
SLEEVE_LENGTH_M = 0.30   # arm length the flap covers; longer sleeves clamp
# the two sock rectangles, (u0, u1, v0, v1); left is the right-hand pair
SOCK_RECT = {
    "left": (0.277 + UV_MARGIN, 0.537 - UV_MARGIN, 0.251 + UV_MARGIN,
             0.428 - UV_MARGIN),
    "right": (0.006 + UV_MARGIN, 0.266 - UV_MARGIN, 0.251 + UV_MARGIN,
              0.428 - UV_MARGIN),
}
# shirt vertices these joints drive belong on the template's sleeve flaps
SLEEVE_JOINTS = frozenset((
    "left_shoulder", "left_elbow", "left_hand",
    "right_shoulder", "right_elbow", "right_hand"))
# the arm frame (GF coords, bind pose)
_SHOULDER = {1: (0.195, -0.0335, 1.4671), -1: (-0.195, -0.0335, 1.4671)}
_ELBOW = {1: (0.3991, -0.0138, 1.262), -1: (-0.3991, -0.0138, 1.262)}


# subdivision of each stock triangle when harvesting UV samples
STOCK_SUBDIV = 4


def barycentric_samples(corners, n=STOCK_SUBDIV):
    """Corner (pos, uv) triples -> the triangle's lattice of (pos, uv)."""
    (p0, t0), (p1, t1), (p2, t2) = corners
    out = []
    for i in range(n + 1):
        for j in range(n + 1 - i):
            k = n - i - j
            a, b, c = i / n, j / n, k / n
            out.append((
                tuple(a * p0[d] + b * p1[d] + c * p2[d] for d in range(3)),
                tuple(a * t0[d] + b * t1[d] + c * t2[d] for d in range(2)),
            ))
    return out


def harvest_stock_kit(stock_ase):
    """-> {panel: [(pos, uv)]} from the stock body's kit-material geoms."""
    text = open(stock_ase).read()
    head = text.split("*GEOMOBJECT")[0]
    kit_materials = set()
    for block in re.split(r"(?=\*MATERIAL \d+ \{)", head):
        m = re.match(r"\*MATERIAL (\d+) \{", block)
        if m and "kit_template" in block:
            kit_materials.add(m.group(1))
    panels = {}
    for g in text.split("*GEOMOBJECT")[1:]:
        name = re.search(r'\*NODE_NAME "([^"]+)"', g).group(1)
        ref = re.search(r"\*MATERIAL_REF (\d+)", g)
        if not ref or ref.group(1) not in kit_materials:
            continue
        verts = {int(m.group(1)): tuple(float(m.group(i)) for i in (2, 3, 4))
                 for m in _V_RE.finditer(g)}
        tverts = {int(m.group(1)): (float(m.group(2)), float(m.group(3)))
                  for m in _T_RE.finditer(g)}
        faces = [tuple(int(m.group(i)) for i in (2, 3, 4)) for m in _F_RE.finditer(g)]
        tfaces = [tuple(int(m.group(i)) for i in (2, 3, 4)) for m in _TF_RE.finditer(g)]
        for f, tf in zip(faces, tfaces):
            # The stock body is low-poly - under fifty vertices per shirt
            # panel - and interpolating a 25k-vertex PES body off corners
            # alone left the kit blocky and its seams ragged. Sample each
            # stock triangle's interior too: position and UV are both linear
            # across it, so the extra samples are exact, not invented.
            corners = [(verts[vi], tverts[ti]) for vi, ti in zip(f, tf)]
            for pos, (u, v) in barycentric_samples(corners):
                if name.startswith("sock"):
                    panel = ("sock_left" if name.endswith("left")
                             else "sock_right")
                else:
                    # The stock suit is ONE surface, but the PES body wears a
                    # separate shirt and shorts that overlap at the waist, so
                    # each garment has to learn from its own half of the
                    # template - otherwise a shorts vertex sitting where the
                    # stock body still wore shirt takes shirt UVs.
                    garment = "shirt" if v >= KIT_WAIST_V else "shorts"
                    side = "front" if u < 0.5 else "back"
                    panel = "body_%s_%s" % (garment, side)
                panels.setdefault(panel, []).append((pos, (u, v)))

    # body panels also carry their own (around, up) parametrization, so a
    # PES vertex can be matched by where it sits ON the garment rather than
    # by where it sits in space
    for garment in ("shirt", "shorts"):
        names = ["body_%s_%s" % (garment, side) for side in ("front", "back")]
        if not all(n in panels for n in names):
            continue
        # ONE frame per garment: front and back are two halves of the same
        # wrap, so they have to share the axis they are measured around
        frame = panel_frame(panels[names[0]] + panels[names[1]])
        for name, side in zip(names, ("front", "back")):
            panels[name] = [(pos, uv, surface_param(pos, frame, side))
                            for pos, uv in panels[name]]
        panels["body_%s_frame" % garment] = frame
    return panels


def panel_z_range(samples):
    zs = [pos[2] for pos, _ in samples]
    return min(zs), max(zs)


def panel_frame(samples):
    """-> (z_lo, z_hi, y_centre) describing a garment panel's own extent."""
    z_lo, z_hi = panel_z_range(samples)
    ys = [pos[1] for pos, _ in samples]
    return z_lo, z_hi, sum(ys) / len(ys)


def surface_param(pos, frame, side):
    """Body-relative (around, up) coordinates of a garment vertex.

    The kit template is a cylindrical unwrap of the torso: `up` is the
    fraction of the garment's own height, `around` the angle about the body
    axis measured from that side's centre line. Matching in these two
    numbers instead of raw 3D makes the transfer independent of how tall or
    how deep the two bodies are - the stock body is a different build from
    PES's, and 3D-nearest matching smeared the kit across the seams.
    """
    z_lo, z_hi, y_c = frame
    up = (pos[2] - z_lo) / (z_hi - z_lo) if z_hi > z_lo else 0.0
    depth = pos[1] - y_c
    around = math.atan2(pos[0], -depth if side == "front" else depth)
    return around, up


# an angle this far around the body counts as far as the full garment height
_AROUND_SCALE = 1.0 / (math.pi * 0.5)


_UP_BINS = 24
_panel_index = {}


def panel_bins(samples):
    """Bucket a panel's samples by `up` so the IDW search stays local."""
    index = _panel_index.get(id(samples))
    if index is None:
        index = {}
        for sample in samples:
            index.setdefault(int(sample[2][1] * _UP_BINS), []).append(sample)
        _panel_index[id(samples)] = index
    return index


def panel_uv(pos, samples, frame, side, k=4, power=2.0):
    """UV for a torso vertex, IDW over the stock panel in (around, up)."""
    a, t = surface_param(pos, frame, side)

    def key(sample):
        sa, st = sample[2]
        return ((a - sa) * _AROUND_SCALE) ** 2 + (t - st) ** 2

    index = panel_bins(samples)
    home = int(t * _UP_BINS)
    near = []
    span = 1
    while not near and span < _UP_BINS * 2:
        near = [s for b in range(home - span, home + span + 1)
                for s in index.get(b, ())]
        span += 1
    scored = sorted(near or samples, key=key)[:k]
    num_u = num_v = den = 0.0
    for sample in scored:
        d2 = key(sample)
        if d2 < 1e-12:
            return sample[1]
        w = 1.0 / d2 ** (power * 0.5)
        num_u += sample[1][0] * w
        num_v += sample[1][1] * w
        den += w
    return (num_u / den, num_v / den)


def idw_uv(pos, samples, k=4, power=2.0):
    """Inverse-distance-weighted UV from the panel's stock samples."""
    scored = sorted(samples, key=lambda s: math.dist(pos, s[0]))[:k]
    num_u = num_v = den = 0.0
    for spos, (u, v) in scored:
        d = math.dist(pos, spos)
        if d < 1e-6:
            return (u, v)
        w = 1.0 / d ** power
        num_u += u * w
        num_v += v * w
        den += w
    return (num_u / den, num_v / den)


def _arm_frame(sign):
    """(shoulder, unit axis) of one arm in the bind pose."""
    sh, el = _SHOULDER[sign], _ELBOW[sign]
    axis = tuple(e - s for e, s in zip(el, sh))
    length = math.sqrt(sum(c * c for c in axis))
    return sh, tuple(c / length for c in axis)


def arm_param(pos):
    """Sleeve vertex -> (x sign, along-arm 0..1, around-arm angle).

    The angle is measured in the plane across the arm from the arm's own
    UP direction, so it stays well defined all the way to the shoulder;
    the old `perp[2] / |perp|` collapsed there, which is what fanned the
    sleeve into those long spikes across the template.
    """
    sign = 1 if pos[0] > 0 else -1
    sh, axis = _arm_frame(sign)
    rel = tuple(p - s for p, s in zip(pos, sh))
    along = sum(r * a for r, a in zip(rel, axis))
    perp = tuple(r - along * a for r, a in zip(rel, axis))
    plen = math.sqrt(sum(c * c for c in perp))
    if plen < 1e-5:
        return sign, max(0.0, along) / SLEEVE_LENGTH_M, 0.0
    # arm-local up (world +z with the axis projected out) and the direction
    # across it; with the arm along +/-x this puts +angle on the player's front
    up = (-axis[2] * axis[0], -axis[2] * axis[1], 1.0 - axis[2] * axis[2])
    ulen = math.sqrt(sum(c * c for c in up)) or 1.0
    up = tuple(c / ulen for c in up)
    across = (axis[1] * up[2] - axis[2] * up[1],
              axis[2] * up[0] - axis[0] * up[2],
              axis[0] * up[1] - axis[1] * up[0])
    angle = math.atan2(sum(p * c for p, c in zip(perp, across)),
                       sum(p * u for p, u in zip(perp, up)))
    return sign, max(0.0, along) / SLEEVE_LENGTH_M, angle


def sleeve_uv(pos, side, frame, garment):
    """UV on the template's sleeve flap: along the arm -> v, around it -> u.

    Blended into the body chart over the length of the sleeve. The flap and
    the body panel meet at the column's body edge, but they run in different
    directions along that edge - one around the arm, one around the chest -
    so butting them together left a torn line across each shoulder. At the
    shoulder itself (along = 0) this returns the body chart exactly, and it
    reaches the pure flap only at the sleeve's end.
    """
    sign, along, angle = arm_param(pos)
    v_hem, v_shoulder = SLEEVE_V
    v = v_shoulder - min(along, 1.0) * (v_shoulder - v_hem)
    u0, u1 = SLEEVE_FLAP_U[(side, sign > 0)]
    frac = min(abs(angle) / math.pi, 1.0)      # 0 on top of the arm, 1 under
    u = u0 + frac * (u1 - u0)
    w = min(max(along, 0.0), 1.0)
    u_body, v_body = body_uv(pos, frame, side, garment)
    return ((1.0 - w) * u_body + w * u, (1.0 - w) * v_body + w * v)


def body_uv(pos, frame, side, garment):
    """UV on the template's body panel: height -> v, angle around -> u.

    A garment is a tube around the body and the template is that tube
    unrolled, so both coordinates are read straight off the geometry. The
    earlier version interpolated UVs from the stock body's own vertices,
    which is only as smooth as that (very low poly, differently
    proportioned) mesh - the resulting chart was a tangle.
    """
    z_lo, z_hi, y_centre = frame
    t = (pos[2] - z_lo) / (z_hi - z_lo) if z_hi > z_lo else 0.0
    v_lo, v_hi = GARMENT_V[garment]
    v = v_lo + min(max(t, 0.0), 1.0) * (v_hi - v_lo)

    phi = math.atan2(pos[0], -(pos[1] - y_centre))
    if side == "front":
        around = phi
    else:                                   # signed angle from the back centre
        around = (math.pi - phi) if phi > 0 else (-math.pi - phi)
    around = min(max(around / (math.pi * 0.5), -1.0), 1.0)
    u = COLUMN_CENTRE[side] + COLUMN_X_SIGN[side] * around * BODY_HALF_U
    return (u, v)


def boundary_loops(mesh):
    """-> [[(x, y, z)]] the mesh's open borders, as connected vertex groups."""
    import collections
    edges = collections.Counter()
    position = {}
    adjacency = collections.defaultdict(set)
    for face in mesh.faces:
        ids = [id(v) for v in face.vertices]
        for v in face.vertices:
            position[id(v)] = (v.position.x, v.position.y, v.position.z)
        for a, b in ((0, 1), (1, 2), (2, 0)):
            edges[tuple(sorted((ids[a], ids[b])))] += 1
    border = [e for e, count in edges.items() if count == 1]
    for a, b in border:
        adjacency[a].add(b)
        adjacency[b].add(a)
    seen, loops = set(), []
    for start in adjacency:
        if start in seen:
            continue
        stack, group = [start], []
        while stack:
            node = stack.pop()
            if node in seen:
                continue
            seen.add(node)
            group.append(position[node])
            stack.extend(adjacency[node] - seen)
        loops.append(group)
    return loops


def eye_socket_shift(face_fmdl, eye_fmdl):
    """-> (dx, dy, dz) in GF coords seating the eyeballs in THIS face.

    `eye.fmdl` is shared by every player while the head is not, so the stock
    eyeball sits wherever the generic head's socket was - about 16 mm below
    this face's, which pushed a bare eyeball out through the cheek. The face
    mesh leaves the socket open, so its border loop is the socket: match the
    eyeball's centre to it (mirroring both sides onto x > 0, the two are
    symmetric and only one side is always a separate loop).
    """
    faces = piece_meshes(face_fmdl, {"fox_skin_mat"}, True)
    if not faces:
        return (0.0, 0.0, 0.0)
    sockets = []
    for loop in boundary_loops(faces[0]):
        if len(loop) < 8:
            continue
        cx = sum(p[0] for p in loop) / len(loop)
        cy = sum(p[1] for p in loop) / len(loop)
        cz = sum(p[2] for p in loop) / len(loop)
        span = max(max(p[k] for p in loop) - min(p[k] for p in loop)
                   for k in range(3))
        # an eye socket: small, at eye height, off the midline
        if span < 0.06 and 1.60 < cy < 1.80 and 0.01 < abs(cx) < 0.07:
            sockets.append((abs(cx), cy, cz))
    if not sockets:
        return (0.0, 0.0, 0.0)
    socket = [sum(s[k] for s in sockets) / len(sockets) for k in range(3)]
    balls = [(v.position.x, v.position.y, v.position.z)
             for mesh in eye_fmdl.meshes for v in mesh.vertices
             if v.position.x > 0]
    if not balls:
        return (0.0, 0.0, 0.0)
    ball = [sum(b[k] for b in balls) / len(balls) for k in range(3)]
    d = [socket[k] - ball[k] for k in range(3)]
    return (0.0, -d[2], d[1])          # Fox -> GF, x kept symmetric


def sock_frames(meshes):
    """-> {side: (z_lo, z_hi, x_centre, y_centre)} for the sock tubes."""
    out = {}
    for side, keep in (("left", lambda x: x > 0), ("right", lambda x: x <= 0)):
        pts = [(v.position.x, -v.position.z, v.position.y)
               for mesh in meshes for v in mesh.vertices if keep(v.position.x)]
        if not pts:
            continue
        zs = [p[2] for p in pts]
        out[side] = (min(zs), max(zs),
                     sum(p[0] for p in pts) / len(pts),
                     sum(p[1] for p in pts) / len(pts))
    return out


def sock_uv(pos, side, frames):
    """A sock is a tube up the shin; its rectangle is that tube unrolled."""
    frame = frames.get(side)
    u0, u1, v0, v1 = SOCK_RECT[side]
    if not frame:
        return ((u0 + u1) * 0.5, (v0 + v1) * 0.5)
    z_lo, z_hi, x_c, y_c = frame
    t = (pos[2] - z_lo) / (z_hi - z_lo) if z_hi > z_lo else 0.0
    v = v0 + min(max(t, 0.0), 1.0) * (v1 - v0)
    phi = math.atan2(pos[0] - x_c, -(pos[1] - y_c))     # -pi..pi around the leg
    u = u0 + (phi / (2.0 * math.pi) + 0.5) * (u1 - u0)
    return (u, v)


def garment_side(pos, frame):
    """front/back half of the body a torso vertex sits on."""
    phi = math.atan2(pos[0], -(pos[1] - frame[2]))
    return "front" if abs(phi) <= math.pi * 0.5 else "back"


def vertex_island(pos, normal_y, kind, panels, garment, z_span, joint):
    """Which UV island of the kit template a vertex belongs to.

    The template is not one continuous chart: the body panel, each sleeve
    flap and the front/back columns are separate islands, far apart in UV.
    A triangle whose corners land in two of them stretches right across the
    texture. Faces are assigned an island as a whole and their corners are
    duplicated along the seams (see gather_piece).
    """
    if kind == "sock":
        return ("sock", "left" if pos[0] > 0 else "right")
    # the sleeve is what the ARM drives - the shirt's own shoulder seam, and
    # the line the template's T-flap is drawn along
    if joint in SLEEVE_JOINTS:
        _, _, angle = arm_param(pos)
        return ("sleeve", "front" if angle >= 0.0 else "back")
    frame = z_span or panels["body_%s_frame" % (garment or "shirt")]
    return ("body", garment_side(pos, frame))


def kit_uv(pos, normal_y, kind, panels, garment=None, z_span=None, island=None):
    """UV on the GF kit template for a PES vertex (GF coords), in `island`."""
    chart, side = island
    if chart == "sock":
        return sock_uv(pos, side, z_span or {})
    if chart == "sleeve":
        frame = z_span or panels["body_%s_frame" % (garment or "shirt")]
        return sleeve_uv(pos, side, frame, garment or "shirt")
    frame = z_span or panels["body_%s_frame" % (garment or "shirt")]
    return body_uv(pos, frame, side, garment or "shirt")


# --- assembly -------------------------------------------------------------------

def load_fmdl(path, fmdl_lib):
    sys.path.insert(0, fmdl_lib)
    import FmdlFile
    f = FmdlFile.FmdlFile()
    f.readFile(path)
    return f


def piece_meshes(fmdl, keep_materials=None, biggest_only=False):
    meshes = fmdl.meshes
    if keep_materials is not None:
        meshes = [m for m in meshes
                  if m.materialInstance and m.materialInstance.name in keep_materials]
    if biggest_only and meshes:
        meshes = [max(meshes, key=lambda m: len(m.vertices))]
    return meshes


def part_rebind(skl_path):
    """-> {gf_node: (dx, dy, dz) in GF coords} moving a part authored against
    its OWN .skl onto the body rig's bind.

    Slot parts (boots, gloves) ship a skeleton of just the bones they ride,
    and PES does not author them at the body rig's bind: the stock boots'
    `sk_foot_l` sits at Fox x 0.0898 where the player's foot bone is at
    0.194, so dropping the mesh in as-authored leaves the boots ~10 cm
    inboard of the ankles - floating beside the feet. Each bone's bind
    difference is the translation that re-anchors it (Fox rigs are
    world-aligned, so a translation is the whole transform).
    """
    deltas = {}
    for bone in pes_skl.parse_file(skl_path):
        target = retarget.PES_BIND.get(bone.name)
        if not target:
            continue
        node = retarget.resolve_bone(bone.name)
        if not node:
            continue
        d = tuple(t - s for t, s in zip(target[0], bone.position))
        deltas[node] = (d[0], -d[2], d[1])          # Fox -> GF
    return deltas


# A sleeve vertex this close to the arm surface is the SAME surface authored
# twice. Measured on the stock parts, the two populations are cleanly
# separated: 1,759 of the shirt sleeve's vertices sit within 6 mm of the arm
# (1,396 of them exactly on it) and the remaining 1,116 - the shoulder cap
# that bridges torso to arm - are all more than 20 mm away.
DUPLICATE_SURFACE_M = 0.006
_GRID = 0.01


def surface_grid(fmdl, keep_materials):
    """Spatial hash of a part's vertices, for de-duplicating another part."""
    import collections
    grid = collections.defaultdict(list)
    for mesh in piece_meshes(fmdl, keep_materials):
        for v in mesh.vertices:
            p = (v.position.x, v.position.y, v.position.z)
            grid[tuple(int(c / _GRID) for c in p)].append(p)
    return grid


def on_surface(grid, position, eps=DUPLICATE_SURFACE_M):
    """Is this vertex sitting on the hashed surface?

    Exact vertex matching was not enough: the two sleeves are only partly
    coincident, and the millimetre-apart remainder z-fought its way up the
    arm as alternating stripes of kit and skin.
    """
    p = (position.x, position.y, position.z)
    cell = tuple(int(c / _GRID) for c in p)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for q in grid.get((cell[0] + dx, cell[1] + dy, cell[2] + dz), ()):
                    if math.dist(p, q) <= eps:
                        return True
    return False


def torso_frame(meshes):
    """(z_lo, z_hi, y_centre) of a garment's TORSO vertices in GF coords -
    sleeves excluded, they go on the template's flaps, not up the body."""
    torso = [(v.position.x, -v.position.z, v.position.y)
             for mesh in meshes for v in mesh.vertices
             if not (abs(v.position.x) > 0.24 and v.position.y > 1.0)]
    if not torso:
        return None
    zs = [p[2] for p in torso]
    return min(zs), max(zs), sum(p[1] for p in torso) / len(torso)


def covered_ranges(garment_meshes, margin=0.015):
    """Height bands (PES y) in which a garment hides the skin underneath.

    A few millimetres of inset is not enough clearance for skin that a
    garment wraps: 460 of the bare leg's vertices sit within 8 mm of the
    sock, so the leg pokes through it in patches. Skin the garment covers
    is not visible at all, so it is simply not shipped - the band stops
    `margin` short of the garment's hem so the seam still closes.
    """
    ys = [v.position.y for mesh in garment_meshes for v in mesh.vertices]
    if not ys:
        return []
    return (min(ys) + margin, max(ys) - margin)


def gather_piece(fmdl, meshes, kit_kind, panels, inset=0.0, garment=None,
                 rebind=None, dedupe=None, shift=None, hidden=None,
                 kit_uv_mode="native"):
    """-> (vertices [(pos, uv, color, normal, skin)], faces) in GF coords.

    `color` is the three-channel fallback, `skin` the influence list the sidecar
    weight file carries - the fingers live past what a colour can name.

    inset pushes vertices along their inverse normal (metres): skin pieces
    that live under garments (arms in sleeves, thighs in shorts) get a few
    millimetres of clearance so they cannot poke through in animation.

    rebind: {gf_node: delta} from part_rebind(), applied per vertex through
    the joint it rides, for parts authored against their own .skl.
    dedupe: vertex keys owned by another part - faces entirely inside it are
    dropped as duplicate surface.
    shift: a fixed translation for the whole piece, in GF coords.
    hidden: [(y_lo, y_hi)] bands another garment covers - faces wholly
    inside one are dropped."""
    bone_to_joint = build_bone_map(fmdl)
    joint_positions = retarget.gf_world_render_bind()
    z_span = None
    if kit_kind == "sock":
        z_span = sock_frames(meshes)
    elif garment:
        z_span = torso_frame(meshes)
    vertices = []
    faces = []
    index = {}
    for mesh in meshes:
        for face in mesh.faces:
            # PES ships the sleeve twice - the shirt's long sleeve and the
            # arm part are the SAME surface, and shipping both z-fights.
            # Faces whose every vertex is on the other part are the duplicate;
            # faces straddling the boundary stay, so the seam still closes.
            if dedupe and all(on_surface(dedupe, v.position)
                              for v in face.vertices):
                continue
            if hidden and any(all(lo <= v.position.y <= hi
                                  for v in face.vertices)
                              for lo, hi in hidden):
                continue
            # ONE island per face. A face is a single patch of fabric, so all
            # three corners read the same chart; corners on a seam are emitted
            # once per island they take part in (the key below), which is what
            # keeps a triangle from stretching across the whole template.
            island = None
            if kit_kind:
                votes = {}
                for vertex in face.vertices:
                    skin = vertex_joints(vertex, bone_to_joint, joint_positions)
                    p = vertex.position
                    cand = vertex_island((p.x, -p.z, p.y), 0.0, kit_kind, panels,
                                         garment, z_span,
                                         retarget.GF_JOINT_ORDER[skin[0][0]])
                    votes[cand] = votes.get(cand, 0) + 1
                island = max(votes.items(), key=lambda kv: kv[1])[0]

            tri = []
            for vertex in face.vertices:
                key = (id(vertex), island)
                if key not in index:
                    index[key] = len(vertices)
                    p = vertex.position
                    pos = (p.x, -p.z, p.y)
                    n = vertex.normal
                    normal = (n.x, -n.z, n.y) if n is not None else None
                    if inset and normal is not None:
                        pos = tuple(c - inset * nc for c, nc in zip(pos, normal))
                    skin = vertex_joints(vertex, bone_to_joint, joint_positions)
                    uv0 = vertex.uv[0] if vertex.uv else None
                    if kit_kind:
                        ny = -vertex.normal.z if vertex.normal else -1.0
                        uv = kit_vertex_uv(
                            kit_uv_mode, uv0,
                            lambda: kit_uv(pos, ny, kit_kind, panels, garment,
                                           z_span, island))
                    else:
                        uv = source_uv(uv0)
                    if rebind:
                        node = retarget.GF_JOINT_ORDER[skin[0][0]]
                        delta = rebind.get(node)
                        if delta:
                            pos = tuple(c + d for c, d in zip(pos, delta))
                    if shift:
                        pos = tuple(c + d for c, d in zip(pos, shift))
                    vertices.append((pos, uv, encode_color(skin), normal, skin))
                tri.append(index[key])
            # Fox winds clockwise-front (D3D); GF culls GL-style, so reverse
            faces.append((tri[0], tri[2], tri[1]))
    return vertices, faces


def write_geomobject(out, name, vertices, faces, material_ref):
    out.write("*GEOMOBJECT {\n")
    out.write('\t*NODE_NAME "%s"\n' % name)
    out.write('\t*NODE_TM {\n\t\t*NODE_NAME "%s"\n' % name)
    out.write("\t\t*INHERIT_POS 0 0 0\n\t\t*INHERIT_ROT 0 0 0\n\t\t*INHERIT_SCL 0 0 0\n")
    out.write("\t\t*TM_ROW0 1.0\t0.0\t0.0\n\t\t*TM_ROW1 0.0\t1.0\t0.0\n")
    out.write("\t\t*TM_ROW2 0.0\t0.0\t1.0\n\t\t*TM_ROW3 0.0\t0.0\t0.0\n\t}\n")
    out.write("\t*MESH {\n")
    out.write("\t\t*MESH_NUMVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_NUMFACES %d\n" % len(faces))
    out.write("\t\t*MESH_VERTEX_LIST {\n")
    for i, (pos, _, _, _, _) in enumerate(vertices):
        out.write("\t\t\t*MESH_VERTEX %d\t%.6f\t%.6f\t%.6f\n" % (i, *pos))
    out.write("\t\t}\n\t\t*MESH_FACE_LIST {\n")
    for i, (a, b, c) in enumerate(faces):
        out.write("\t\t\t*MESH_FACE %d: A: %d B: %d C: %d AB: 1 BC: 1 CA: 1 "
                  "*MESH_SMOOTHING 1 *MESH_MTLID 0\n" % (i, a, b, c))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMTVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_TVERTLIST {\n")
    for i, (_, uv, _, _, _) in enumerate(vertices):
        out.write("\t\t\t*MESH_TVERT %d\t%.6f\t%.6f\t0.0\n" % (i, uv[0], uv[1]))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMTVFACES %d\n" % len(faces))
    out.write("\t\t*MESH_TFACELIST {\n")
    for i, (a, b, c) in enumerate(faces):
        out.write("\t\t\t*MESH_TFACE %d\t%d\t%d\t%d\n" % (i, a, b, c))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMCVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_CVERTLIST {\n")
    for i, (_, _, color, _, _) in enumerate(vertices):
        out.write("\t\t\t*MESH_VERTCOL %d\t%.3f\t%.3f\t%.3f\n" % (i, *color))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMCVFACES %d\n" % len(faces))
    out.write("\t\t*MESH_CFACELIST {\n")
    for i, (a, b, c) in enumerate(faces):
        out.write("\t\t\t*MESH_CFACE %d\t%d\t%d\t%d\n" % (i, a, b, c))
    out.write("\t\t}\n")
    if all(v[3] is not None for v in vertices):
        _write_authored_normals(out, vertices, faces)
    else:
        ase_util.write_mesh_normals(out, [v[0] for v in vertices], faces, smooth=True)
    out.write("\t}\n")
    out.write("\t*PROP_MOTIONBLUR 0\n\t*PROP_CASTSHADOW 1\n")
    out.write("\t*PROP_RECVSHADOW 1\n\t*MATERIAL_REF %d\n}\n" % material_ref)


def _write_authored_normals(out, vertices, faces):
    out.write("\t\t*MESH_NORMALS {\n")
    for i, (a, b, c) in enumerate(faces):
        pa, pb, pc = vertices[a][0], vertices[b][0], vertices[c][0]
        ux, uy, uz = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
        vx, vy, vz = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        l = math.sqrt(sum(x * x for x in n)) or 1.0
        out.write("\t\t\t*MESH_FACENORMAL %d\t%.4f\t%.4f\t%.4f\n"
                  % (i, n[0] / l, n[1] / l, n[2] / l))
        for idx in (a, b, c):
            nx, ny, nz = vertices[idx][3]
            out.write("\t\t\t\t*MESH_VERTEXNORMAL %d\t%.4f\t%.4f\t%.4f\n"
                      % (idx, nx, ny, nz))
    out.write("\t\t}\n")


MATERIAL_TMPL = (
    '\t*MATERIAL %(idx)d {\n'
    '\t\t*MATERIAL_NAME "%(name)s"\n\t\t*MATERIAL_CLASS "Standard"\n'
    "\t\t*MATERIAL_AMBIENT 0.588\t0.588\t0.588\n"
    "\t\t*MATERIAL_DIFFUSE 0.588\t0.588\t0.588\n"
    "\t\t*MATERIAL_SPECULAR 0.900\t0.900\t0.900\n"
    "\t\t*MATERIAL_SHINE 0.100\n\t\t*MATERIAL_SHADING Blinn\n"
    "\t\t*MATERIAL_SHINESTRENGTH 0.0\n"
    "\t\t*MATERIAL_SELFILLUM 0.0\n"
    '\t\t*MAP_DIFFUSE {\n\t\t\t*MAP_NAME "%(name)s"\n'
    '\t\t\t*MAP_CLASS "Bitmap"\n'
    '\t\t\t*BITMAP "%(texture)s"\n'
    "\t\t\t*MAP_TYPE Screen\n\t\t}\n\t}\n")


def assemble(args):
    panels = harvest_stock_kit(args.stock)

    def C(name):
        return os.path.join(args.common, name)

    # (geom name, fmdl path, keep_materials, biggest, kit kind, material,
    #  inset, garment, skl)
    pieces = []
    # The shirt is a torso and a pair of sleeves, and they come from different
    # parts. "torso_mat" in undershirt.fmdl reaches out to |x| 0.60 - it is
    # the SLEEVES. The body next to it, on the "undershirt" material, is the
    # undergarment: its chest maps to u 0.24 and its back to u 0.73, a
    # left/right two-column chart, where a PES uniform sheet is a centred
    # cross. Dressed in a team kit it put the sponsor print on the ribs.
    #
    # The torso that IS uniform-mapped rides "mod_latest_uni_shirts": its
    # chest lands on (0.51, 0.66) and its back on (0.49, 0.33), the front and
    # back panels of the sheet. It carries no sleeves of its own, which is why
    # both parts are needed.
    pieces.append(("shirt", args.shirt_body, {"mod_latest_uni_shirts"},
                   False, "body", "kit", 0.0, "shirt", None))
    pieces.append(("sleeves", args.undershirt, {"torso_mat"},
                   False, "body", "kit", 0.0, "shirt", None))
    pieces.append(("shorts", C("pants_out_sub.fmdl"), None, False, "body", "kit",
                   0.0, "shorts", None))
    pieces.append(("socks", C("socks_middle.fmdl"), None, False, "sock", "kit",
                   0.0, None, None))
    # skin under garments gets clearance so it cannot poke through in motion
    pieces.append(("arms", C("arm.fmdl"), {"arm_mat"}, False, None, "skin", 0.004,
                   None, None))
    pieces.append(("thighs", C("thigh_short.fmdl"), {"thigh_mat"}, False, None,
                   "skin", 0.004, None, None))
    pieces.append(("hand_l", C("hand_l.fmdl"), {"arm_mat"}, False, None, "skin", 0.0,
                   None, None))
    pieces.append(("hand_r", C("hand_r.fmdl"), {"arm_mat"}, False, None, "skin", 0.0,
                   None, None))
    pieces.append(("neck", C("neck.fmdl"), {"skin_head"}, False, None, "skin", 0.0,
                   None, None))
    pieces.append(("eyes", C("eye.fmdl"), {"eye_mat"}, False, None, "eye", 0.0,
                   None, None))
    pieces.append(("face", args.face, {"fox_skin_mat"}, True, None, "face", 0.0,
                   None, None))
    if args.hair:
        # the scalp/cranium ride the hair fmdl (fox_head_shell_mat) - a bare
        # face fmdl has no top of head
        pieces.append(("scalp", args.hair,
                       {"fox_skin_mat", "fox_head_shell_mat"}, False, None,
                       "face", 0.0, None, None))
        pieces.append(("hair", args.hair, {"fox_hair_mat"}, False, None, "hair", 0.0,
                   None, None))
    # a slot part: authored against its own boots.skl, not the body bind
    boots_skl = args.boots_skl or os.path.join(os.path.dirname(args.boots),
                                               "boots.skl")
    pieces.append(("boots", args.boots, None, False, None, "boots", 0.0, None,
                   boots_skl if os.path.isfile(boots_skl) else None))

    materials = [
        ("kit", "media/objects/players/textures/kit_template.png"),
        ("skin", "media/objects/players/textures/skin.jpg"),
        ("face", args.face_texture_ref),
        ("hair", args.hair_texture_ref),
        ("boots", args.boots_texture_ref),
        ("eye", args.eye_texture_ref),
    ]
    mat_index = {name: i for i, (name, _) in enumerate(materials)}

    os.makedirs(args.out_dir, exist_ok=True)
    ase_path = os.path.join(args.out_dir, "fullbody_pes.ase")
    with open(ase_path, "w") as out:
        out.write("*3DSMAX_ASCIIEXPORT\t200\n")
        out.write('*COMMENT "PES base player -> GF fullbody by tools/pes21_import/pes_base_body.py"\n')
        out.write("*SCENE {\n\t*SCENE_FILENAME \"fullbody\"\n")
        out.write("\t*SCENE_FIRSTFRAME 0\n\t*SCENE_LASTFRAME 100\n")
        out.write("\t*SCENE_FRAMESPEED 30\n\t*SCENE_TICKSPERFRAME 160\n")
        out.write("\t*SCENE_BACKGROUND_STATIC 0.000\t0.000\t0.000\n")
        out.write("\t*SCENE_AMBIENT_STATIC 0.000\t0.000\t0.000\n}\n")
        out.write("*MATERIAL_LIST {\n\t*MATERIAL_COUNT %d\n" % len(materials))
        for i, (name, tex) in enumerate(materials):
            out.write(MATERIAL_TMPL % {"idx": i, "name": name, "texture": tex})
        out.write("}\n")

        # the shirt's sleeve and the arm part are one surface authored twice
        arm_surface = surface_grid(load_fmdl(C("arm.fmdl"), args.fmdl_lib),
                                   {"arm_mat"})
        # bare legs are hidden inside the socks and the shorts
        sock_band = covered_ranges(
            piece_meshes(load_fmdl(C("socks_middle.fmdl"), args.fmdl_lib)))
        shorts_band = covered_ranges(
            piece_meshes(load_fmdl(C("pants_out_sub.fmdl"), args.fmdl_lib)))
        leg_hidden = [(-10.0, sock_band[1]), (shorts_band[0], 10.0)]
        # the shared eyeballs have to be seated in THIS face's sockets
        eye_shift = eye_socket_shift(load_fmdl(args.face, args.fmdl_lib),
                                     load_fmdl(C("eye.fmdl"), args.fmdl_lib))
        print("  eye socket shift: %.4f %.4f %.4f" % eye_shift)

        total_v = total_f = 0
        built = []
        for name, path, keep, biggest, kit_kind, material, inset, garment, skl \
                in pieces:
            fmdl = load_fmdl(path, args.fmdl_lib)
            meshes = piece_meshes(fmdl, keep, biggest)
            vertices, faces = gather_piece(fmdl, meshes, kit_kind, panels, inset,
                                          garment,
                                          part_rebind(skl) if skl else None,
                                          arm_surface if name == "shirt" else None,
                                          eye_shift if name == "eyes" else None,
                                          leg_hidden if name == "thighs" else None,
                                          args.kit_uv)
            built.append((name, vertices, faces, material))

        # A player is a dozen shells that overlap rather than meet, each weighted on
        # its own, and where two of them cover the same place they have to move
        # together or one comes through the other (seams.py). Done over the whole body
        # at once, because a seam is between parts by definition.
        before = [[(v[0], v[4]) for v in vertices]
                  for _, vertices, _, _ in built]
        # Welded first: a UV seam duplicates a vertex inside one shell, and
        # reconcile() only looks across shells, so those duplicates kept two
        # different guesses and tore at the bind pose (seams.weld).
        agreed = seams.weld(before)
        welded, _ = seams.reconciled_count(before, agreed)
        if welded:
            print("  seams: %d coincident vertex weight(s) welded" % welded)
        reconciled = seams.reconcile(agreed)
        changed, migrated = seams.reconciled_count(agreed, reconciled)
        agreed = reconciled
        print("  seams: %d vertex weight(s) reconciled between parts, %d changed bone"
              % (changed, migrated))
        skins = []
        for (name, vertices, faces, material), blended in zip(built, agreed):
            vertices = [v[:2] + (encode_color(joints), v[3], joints)
                        for v, (_, joints) in zip(vertices, blended)]
            skins += [(v[0], v[4]) for v in vertices]
            write_geomobject(out, name, vertices, faces, mat_index[material])
            total_v += len(vertices)
            total_f += len(faces)
            print("  %-8s %5d verts %5d faces (%s)" %
                  (name, len(vertices), len(faces), material))

    # The weights the vertex colours cannot carry. PES weights a vertex to up to
    # four bones and the finger joints are past what a colour can name, so the real
    # weights ride a sidecar and the colours stay as the fallback
    # (skinweights.hpp). This is also why the hands are no longer curled at
    # conversion: they have joints now, and the engine poses them (handrig.hpp).
    carried = write_sidecar(ase_path, skins)
    fingers = count_finger_lines(weights_path(ase_path))
    print("  skin weights: %d vertex/vertices in %s, %d of them on a finger"
          % (carried, os.path.basename(weights_path(ase_path)), fingers))

    open(os.path.join(args.out_dir, "fullbody_pes.object"), "w").write(
        "<object>\n\n\t<geometry>\n"
        "\t\t<filename>models/fullbody_pes.ase</filename>\n"
        "\t\t<name>fullbody</name>\n"
        "\t\t<position>0, 0, 0</position>\n"
        "\t\t<rotation>0, 0, 0, 0</rotation>\n"
        "\t</geometry>\n\n</object>\n")

    # FaceRig weight map for the default head
    face_weights_path = os.path.join(args.out_dir, "faceweights.txt")
    face_weights.export(args.face, face_weights_path, args.fmdl_lib)
    print("total: %d verts, %d faces -> %s" % (total_v, total_f, ase_path))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out_dir")
    parser.add_argument("--common", required=True,
                        help="extracted common_package .../character/common dir")
    parser.add_argument("--undershirt", required=True,
                        help="undershirt.fmdl - its torso_mat meshes are the "
                             "shirt's SLEEVES")
    parser.add_argument("--shirt-body", required=True,
                        help="the uniform-mapped shirt torso: the part whose "
                             "material is mod_latest_uni_shirts (bibs.fmdl)")
    parser.add_argument("--boots", required=True, help="a boots.fmdl")
    parser.add_argument("--boots-skl", default=None,
                        help="the boots' own .skl (default: next to the fmdl)")
    parser.add_argument("--face", required=True, help="a face_high.fmdl")
    parser.add_argument("--hair", default=None,
                        help="the matching hair_high.fmdl (scalp + hair)")
    parser.add_argument("--stock", required=True,
                        help="the migrated stock fullbody.ase (kit UV source)")
    parser.add_argument("--fmdl-lib", required=True)
    parser.add_argument("--kit-uv", choices=("native", "template"), default="native",
                        help="native (default): kit pieces keep the UVs PES "
                             "authored them with, so a team's own u0<team>p<n> "
                             "sheet maps correctly. template: re-UV onto GF's "
                             "kit_template layout, for kits drawn against it.")
    parser.add_argument("--face-texture-ref",
                        default="media/objects/players/textures/pes_base_face.png")
    parser.add_argument("--hair-texture-ref",
                        default="media/objects/players/textures/pes_base_hair.png")
    parser.add_argument("--boots-texture-ref",
                        default="media/objects/players/textures/pes_base_boots.png")
    parser.add_argument("--eye-texture-ref",
                        default="media/objects/players/textures/pes_base_eye.png")
    args = parser.parse_args()
    assemble(args)


if __name__ == "__main__":
    main()

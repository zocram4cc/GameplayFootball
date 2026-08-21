#!/usr/bin/env python3
"""Curls a bound hand mesh out of its splayed bind pose.

PES's hands are modelled flat with the fingers spread, and PES poses them at runtime
from its own finger animation channels (skh_* bones). This engine's rig has twenty body
joints and no fingers - retarget.py collapses skh_* onto the wrist, which is lossless
for skinning but means the fingers never move. So the hands sit in bind forever, and a
splayed bind reads as a flat paddle: it is what shows on every player in the stock body.

Nothing here invents finger joints. It bends the hand geometry once, about the knuckle
line, so the resting pose is a relaxed cup rather than a spread paddle. The bend grows
with distance past the knuckles, which is what a real hand does when it relaxes.

  python3 hand_pose.py <fullbody.ase> [--degrees 35] [--dry-run]
"""

import argparse
import math
import re


# How far past the wrist the knuckles sit, as a fraction of the hand's own length.
# Measured on PES's hand mesh: the metacarpals are a little under half of it.
KNUCKLE_FRACTION = 0.45

# The resting curl. A relaxed hand closes its fingers by rather more than this at the
# tip, but the mesh has no joints to fold at, so an over-bend shows as a crease.
DEFAULT_DEGREES = 35.0


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _scale(a, k):
    return (a[0] * k, a[1] * k, a[2] * k)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _length(a):
    return math.sqrt(_dot(a, a))


def normalise(a):
    length = _length(a)
    if length < 1e-9:
        return (0.0, 0.0, 0.0)
    return _scale(a, 1.0 / length)


def rotate_about(point, origin, axis, radians):
    """Rodrigues rotation of `point` about the line (origin, axis)."""
    axis = normalise(axis)
    if axis == (0.0, 0.0, 0.0) or radians == 0.0:
        return point
    v = _sub(point, origin)
    c, s = math.cos(radians), math.sin(radians)
    rotated = _add(_add(_scale(v, c), _scale(_cross(axis, v), s)),
                   _scale(axis, _dot(axis, v) * (1.0 - c)))
    return _add(rotated, origin)


def hand_frame(points, wrist):
    """-> (forward, span) for a hand mesh: the direction the fingers point, and how
    far they reach from the wrist."""
    if not points:
        return (0.0, 0.0, 0.0), 0.0
    centre = (sum(p[0] for p in points) / len(points),
              sum(p[1] for p in points) / len(points),
              sum(p[2] for p in points) / len(points))
    # The fingers are the far half of the mesh, so the mean sits between wrist and
    # tips: the direction to it is the direction they point.
    forward = normalise(_sub(centre, wrist))
    span = max((_dot(_sub(p, wrist), forward) for p in points), default=0.0)
    return forward, span


def curl_axis(points, wrist, forward):
    """The knuckle line: the axis the fingers fold about.

    Taken as the mesh's widest direction across `forward`, which for a hand is the
    line through the knuckles - so folding about it closes the fingers rather than
    splaying them sideways.
    """
    widest = (0.0, 0.0, 0.0)
    best = -1.0
    for p in points:
        across = _sub(_sub(p, wrist), _scale(forward, _dot(_sub(p, wrist), forward)))
        length = _length(across)
        if length > best:
            best, widest = length, across
    return normalise(widest)


def curl(points, wrist, degrees=DEFAULT_DEGREES, knuckle_fraction=KNUCKLE_FRACTION):
    """-> the hand's points, curled about the knuckle line.

    A point before the knuckles does not move; past them the bend grows with distance,
    so the palm keeps its shape and the fingers close.
    """
    if not points or degrees == 0.0:
        return list(points)
    forward, span = hand_frame(points, wrist)
    if span <= 1e-6:
        return list(points)
    axis = curl_axis(points, wrist, forward)
    knuckle = span * knuckle_fraction
    origin = _add(wrist, _scale(forward, knuckle))
    radians = math.radians(degrees)
    posed = []
    for p in points:
        along = _dot(_sub(p, wrist), forward)
        if along <= knuckle:
            posed.append(p)
            continue
        t = (along - knuckle) / max(1e-6, span - knuckle)
        posed.append(rotate_about(p, origin, axis, radians * t))
    return posed


VERTEX_RE = re.compile(r'(\*MESH_VERTEX\s+(\d+)\s+)([-\d.eE]+)\s+([-\d.eE]+)\s+([-\d.eE]+)')
NODE_RE = re.compile(r'\*NODE_NAME "([^"]*)"')


def geom_blocks(text):
    """-> [(name, start, end)] for every GEOMOBJECT in an ASE."""
    blocks = []
    for match in re.finditer(r"\*GEOMOBJECT \{", text):
        start = match.start()
        depth = 0
        i = match.end() - 1
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        block = text[start:i + 1]
        name_match = NODE_RE.search(block)
        blocks.append((name_match.group(1) if name_match else "", start, i + 1))
    return blocks


def pose_ase(text, hand_names=("hand_l", "hand_r"), degrees=DEFAULT_DEGREES):
    """-> (posed ASE text, {mesh: vertices moved})."""
    moved = {}
    out = text
    # Rewrite from the end so earlier offsets stay valid.
    for name, start, end in sorted(geom_blocks(text), key=lambda b: -b[1]):
        if name not in hand_names:
            continue
        block = out[start:end]
        entries = list(VERTEX_RE.finditer(block))
        if not entries:
            continue
        points = [(float(m.group(3)), float(m.group(4)), float(m.group(5))) for m in entries]
        # The wrist end of the mesh: its own nearest point to the body, which is where
        # the arm meets it. Taken from the mesh rather than the rig, so this works on
        # any hand mesh whatever its joint is called.
        forward_guess, _ = hand_frame(points, points[0])
        wrist = min(points, key=lambda p: _dot(p, forward_guess))
        posed = curl(points, wrist, degrees)
        pieces = []
        cursor = 0
        changed = 0
        for m, new in zip(entries, posed):
            pieces.append(block[cursor:m.start()])
            pieces.append("%s%.6f\t%.6f\t%.6f" % (m.group(1), new[0], new[1], new[2]))
            cursor = m.end()
            if new != (float(m.group(3)), float(m.group(4)), float(m.group(5))):
                changed += 1
        pieces.append(block[cursor:])
        out = out[:start] + "".join(pieces) + out[end:]
        moved[name] = changed
    return out, moved


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ase")
    parser.add_argument("--degrees", type=float, default=DEFAULT_DEGREES)
    parser.add_argument("--hands", default="hand_l,hand_r")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    text = open(args.ase, errors="replace").read()
    posed, moved = pose_ase(text, tuple(h for h in args.hands.split(",") if h), args.degrees)
    for name in sorted(moved):
        print("%-10s %d vertices curled" % (name, moved[name]))
    if not moved:
        print("no hand meshes found in %s" % args.ase)
        return
    if args.dry_run:
        print("dry run; nothing written")
        return
    open(args.ase, "w").write(posed)
    print("wrote %s" % args.ase)


if __name__ == "__main__":
    main()

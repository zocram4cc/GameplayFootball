#!/usr/bin/env python3
"""Hand-authored camerawork for the moments PES ships no camera for.

Writes .camtrack files (src/utils/camtrack.hpp: one 30 fps frame per line,
``frame,px,py,pz,qx,qy,qz,qw,fov,near,far``; metres, Z up, pitch centre at the
origin, vertical FOV in degrees) into data/media/presentation/cutscenes/, the
tracked twin of the PES export in data/media/cutscenes/ that Match scans
alongside it.

  python3 tools/presentation/author_camtracks.py

Every shot is a dolly with a look-at: the position path is a few keyed points,
smoothly interpolated, and the aim is what a broadcast operator would hold.
Incident-local shots (foul/*) are authored about the origin, where Match puts
the incident; stadium shots are in pitch coordinates.
"""
import math
import os

ROOT = os.path.join(os.path.dirname(__file__), "..", "..", "data", "media", "presentation",
                    "cutscenes")
FPS = 30


def sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def norm(v):
    n = math.sqrt(sum(c * c for c in v))
    return [c / n for c in v]


def quat_from_basis(x, y, z):
    """Right-handed camera basis (right, up, -forward) -> (qx, qy, qz, qw).

    Same arithmetic as QuatFromBasis in src/utils/camtrack.cpp, so the engine
    reads the frame back exactly as authored."""
    trace = x[0] + y[1] + z[2]
    if trace > 0:
        s = math.sqrt(trace + 1) * 2
        return [(y[2] - z[1]) / s, (z[0] - x[2]) / s, (x[1] - y[0]) / s, 0.25 * s]
    if x[0] > y[1] and x[0] > z[2]:
        s = math.sqrt(1 + x[0] - y[1] - z[2]) * 2
        return [0.25 * s, (y[0] + x[1]) / s, (z[0] + x[2]) / s, (y[2] - z[1]) / s]
    if y[1] > z[2]:
        s = math.sqrt(1 + y[1] - x[0] - z[2]) * 2
        return [(y[0] + x[1]) / s, 0.25 * s, (z[1] + y[2]) / s, (z[0] - x[2]) / s]
    s = math.sqrt(1 + z[2] - x[0] - y[1]) * 2
    return [(z[0] + x[2]) / s, (z[1] + y[2]) / s, 0.25 * s, (x[1] - y[0]) / s]


def look_at(eye, target):
    forward = norm(sub(target, eye))
    right = norm(cross(forward, [0, 0, 1]))
    up = cross(right, forward)
    return quat_from_basis(right, up, [-c for c in forward])


def forward_of(q):
    x, y, z, w = q
    return [-2 * (x * z + w * y), -2 * (y * z - w * x), -(1 - 2 * (x * x + y * y))]


def smooth(t):
    """Ease in and out: a dolly starts and stops without a kick."""
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return [a[i] + (b[i] - a[i]) * t for i in range(len(a))]


def dolly(seconds, eye_from, eye_to, aim_from, aim_to, fov_from, fov_to, near=0.3, far=400.0,
          ease=True):
    frames = int(round(seconds * FPS))
    rows = []
    for i in range(frames):
        t = i / max(1, frames - 1)
        s = smooth(t) if ease else t
        eye = lerp(eye_from, eye_to, s)
        aim = lerp(aim_from, aim_to, s)
        q = look_at(eye, aim)
        fov = fov_from + (fov_to - fov_from) * s
        rows.append([i] + eye + q + [fov, near, far])
    return rows


def write(path, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        for r in rows:
            f.write("%d,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%.3f,%.2f,%.1f\n" % tuple(r))
    print(f"{os.path.relpath(path)}: {len(rows)} frames, {len(rows) / FPS:.1f} s")


def injury():
    # PES's injury staging (foul/injury/*.chor): the fouled man is down at about
    # (-0.6, 1.6) in the scene, the offender at (1.6, -0.6) calling for help, the
    # referee arriving from (1.1, 1.7). The broadcast camera side is -y. A low,
    # slow lateral dolly from the near touchline side, knee height, tracking
    # across the downed man and the referee bending over him; the lens tightens
    # a little as it goes. Match re-aims incident-local shots at the incident,
    # so the aim here is where the engine will point it anyway.
    return dolly(8.0, [-5.0, -5.0, 0.7], [-1.0, -5.6, 0.7], [0.0, 0.6, 0.9], [0.0, 0.6, 0.9],
                 24.0, 20.0, near=0.3)


def half_time():
    # The whistle for the interval at a ground PES exported no walk-off for: the
    # players are wherever the whistle caught them, so this is the broadcast's
    # own beat - the main camera position on the -y stand, a slow push in over
    # the pitch and a gentle drop, then the wipe covers the reset for the second
    # half (SetPieceLaws::kHalfTimeCutscene_ms).
    return dolly(SECONDS_HALF, [0.0, -46.0, 15.0], [0.0, -40.0, 11.0], [0.0, 0.0, 0.0],
                 [0.0, -4.0, 0.0], 30.0, 24.0, near=1.0)


SECONDS_HALF = 10.0

TRACKS = {
    "foul/injury/gf_injury_low_dolly.camtrack": injury,
    "timeup/tu_half_gf_wide_push.camtrack": half_time,
}
# Where each shot's first frame looks, for the self-check.
AIMS = {
    "foul/injury/gf_injury_low_dolly.camtrack": [0.0, 0.6, 0.9],
    "timeup/tu_half_gf_wide_push.camtrack": [0.0, 0.0, 0.0],
}


def main():
    for rel, make in TRACKS.items():
        rows = make()
        # Self-check: the quaternion reads back as the direction it was authored
        # to look in (the engine's CamTrackForward, ported above).
        q = rows[0][4:8]
        assert abs(math.sqrt(sum(c * c for c in q)) - 1) < 1e-5, rel
        fwd = forward_of(q)
        eye = rows[0][1:4]
        want = AIMS[rel]
        got = norm(sub(want, eye))
        assert all(abs(fwd[i] - got[i]) < 1e-4 for i in range(3)), (rel, fwd, got)
        write(os.path.join(ROOT, rel), rows)


if __name__ == "__main__":
    main()

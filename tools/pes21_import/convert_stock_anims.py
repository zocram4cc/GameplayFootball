"""Rebases legacy GF .anim files onto the native PES rig's bind directions.

The old skeleton's limb segments pointed straight down (shoulder->elbow
(0,0,-0.33)); the PES bind is a slight A-pose (upperarm 45 degrees out).
An animation's local quaternions rotate the BIND segment, so the same keys
mean different world directions on the two rigs: unconverted stock clips
hold their arms out in front.

The exact fix is a per-node constant conjugation. With C_b the minimal
rotation taking the NEW bind direction of bone b's segment onto the OLD one
(both rigs' bind world rotations are identity, so world == local at bind):

    q'(b) = C_parent(b)^-1 * q(b) * C_b

which preserves every keyed pose's world segment directions exactly. C is
identity for the torso chain (whose offsets barely moved) and for leaf
nodes; only thigh/knee/shoulder/elbow carry a real C, and ankle/hand lines
absorb their parent's C. A hand line missing from a clip (stock clips key
14 nodes) becomes a constant C_elbow^-1 line, otherwise the hand would
inherit the elbow's rebasing untwisted.

Ball keyframes, the player root line and the metadata tail are untouched --
the selector's contract (docs/PES21_ANIMS.md) reads those, so selection
behaviour is unchanged.

  python3 convert_stock_anims.py <dir-or-file>...   (in place, idempotent
  only by re-running against pristine sources -- keep it to one migration)
"""

import math
import os
import sys

import retarget
from migrate_to_native_rig import OLD_BIND, q_mul, q_conj


def _norm(v):
    l = math.sqrt(sum(c * c for c in v))
    return tuple(c / l for c in v)


def _rot_between(a, b):
    """Minimal quaternion rotating unit vector a onto unit vector b."""
    a, b = _norm(a), _norm(b)
    d = sum(x * y for x, y in zip(a, b))
    if d > 1.0 - 1e-9:
        return (0.0, 0.0, 0.0, 1.0)
    if d < -1.0 + 1e-9:
        # opposite: rotate pi about any perpendicular axis
        ax = (1.0, 0.0, 0.0) if abs(a[0]) < 0.9 else (0.0, 1.0, 0.0)
        c = _norm((a[1] * ax[2] - a[2] * ax[1],
                   a[2] * ax[0] - a[0] * ax[2],
                   a[0] * ax[1] - a[1] * ax[0]))
        return (c[0], c[1], c[2], 0.0)
    c = (a[1] * b[2] - a[2] * b[1],
         a[2] * b[0] - a[0] * b[2],
         a[0] * b[1] - a[1] * b[0])
    s = math.sqrt((1.0 + d) * 2.0)
    return _norm((c[0] / s, c[1] / s, c[2] / s, s * 0.5))


def _segment_c():
    """C per limb node: minimal rotation new-bind-segment -> old-bind-segment."""
    new_world = retarget.gf_world_bind()
    old_world = {}
    for name, (offset, parent) in OLD_BIND.items():
        pass
    # old world positions
    order = list(OLD_BIND.keys())
    for name in order:
        offset, parent = OLD_BIND[name]
        old_world[name] = offset if parent is None else tuple(
            a + b for a, b in zip(old_world[parent], offset))

    segments = {
        "left_thigh": "left_knee", "left_knee": "left_ankle",
        "right_thigh": "right_knee", "right_knee": "right_ankle",
        "left_shoulder": "left_elbow", "left_elbow": "left_hand",
        "right_shoulder": "right_elbow", "right_elbow": "right_hand",
    }
    C = {}
    for bone, child in segments.items():
        d_old = tuple(a - b for a, b in zip(old_world[child], old_world[bone]))
        d_new = tuple(a - b for a, b in zip(new_world[child], new_world[bone]))
        C[bone] = _rot_between(d_new, d_old)
    return C


def conversions():
    """node -> (pre, post) quats: q' = pre * q * post. None = identity."""
    C = _segment_c()
    conv = {}
    for side in ("left", "right"):
        thigh, knee, ankle = side + "_thigh", side + "_knee", side + "_ankle"
        shoulder, elbow, hand = (side + "_shoulder", side + "_elbow",
                                 side + "_hand")
        conv[thigh] = (None, C[thigh])
        conv[knee] = (q_conj(C[thigh]), C[knee])
        conv[ankle] = (q_conj(C[knee]), None)
        conv[shoulder] = (None, C[shoulder])
        conv[elbow] = (q_conj(C[shoulder]), C[elbow])
        conv[hand] = (q_conj(C[elbow]), None)
    return conv


def _convert_quat(q, pre, post):
    if pre is not None:
        q = q_mul(pre, q)
    if post is not None:
        q = q_mul(q, post)
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q)


def convert_text(text, conv=None):
    conv = conv or conversions()
    lines = text.split("\n")
    out = []
    present = set()
    last_frame = 0
    insert_at = None
    for i, line in enumerate(lines):
        parts = line.split(",")
        name = parts[0]
        if name == "player" and len(parts) > 4:
            # frame,x,y,z groups: remember the clip length for inserted lines
            frames = [int(parts[j]) for j in range(1, len(parts) - 3, 4)]
            last_frame = frames[-1] if frames else 0
            out.append(line)
            present.add(name)
            insert_at = len(out)
            continue
        if name in conv and len(parts) >= 6:
            pre, post = conv[name]
            pieces = [name]
            for j in range(1, len(parts) - 4, 5):
                frame = parts[j]
                q = tuple(float(parts[j + k]) for k in range(1, 5))
                q = _convert_quat(q, pre, post)
                pieces.append("%s,%f,%f,%f,%f" % ((frame,) + q))
            out.append(",".join(pieces))
            present.add(name)
            insert_at = len(out)
            continue
        if len(parts) >= 6 and not name.startswith("<") and name != "extension":
            present.add(name)
            insert_at = len(out) + 1
        out.append(line)

    # nodes the clip does not key were implicitly identity; if their
    # conversion is not identity, they need an explicit constant line
    inserts = []
    for name, (pre, post) in conv.items():
        if name in present:
            continue
        q = _convert_quat((0.0, 0.0, 0.0, 1.0), pre, post)
        if abs(q[3]) > 1.0 - 1e-7:
            continue
        keys = "%d,%f,%f,%f,%f" % ((0,) + q)
        if last_frame > 0:
            keys += ",%d,%f,%f,%f,%f" % ((last_frame,) + q)
        inserts.append(name + "," + keys)
    if inserts and insert_at is not None:
        out[insert_at:insert_at] = inserts
    return "\n".join(out)


def main():
    conv = conversions()
    count = 0
    for root_arg in sys.argv[1:]:
        if os.path.isfile(root_arg):
            files = [root_arg]
        else:
            files = []
            for dirpath, _, names in os.walk(root_arg):
                for n in names:
                    if n.endswith(".anim"):
                        files.append(os.path.join(dirpath, n))
        for path in sorted(files):
            text = open(path).read()
            open(path, "w").write(convert_text(text, conv))
            count += 1
    print("converted %d anims" % count)


if __name__ == "__main__":
    main()

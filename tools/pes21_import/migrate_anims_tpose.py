"""Rebases stock GF .anim files from the render-pose rig onto the anim-pose rig.

The skeleton's bind moved from PES's render bind (arms ~45 degrees down, legs
splayed - body.skl, what meshes are authored in) to PES's anim skeleton
(strict T-pose - body_anim_skel.ask, what every gani is authored on). A local
quaternion rotates the BIND segment, so the same keys mean different world
directions on the two rigs; every stock clip must be re-expressed once.

The exact fix is the alignment sandwich. With W(b) = retarget.ALIGN_GF[b]
(render pose -> anim pose, per bone, GF coords):

    q'(b) = W(parent(b)) * q(b) * W(b)^-1

which preserves every keyed pose's world segment directions exactly. W is
identity for the trunk chain and clavicles; thighs/knees/ankles carry ~7/4
degrees and shoulders/elbows/hands ~45. A hand line missing from a clip
(stock clips key 13 nodes) becomes a constant W(elbow) * W(hand)^-1 line,
otherwise the hand would inherit the elbow's rebasing untwisted - and the
fingers hang off the hand with W(finger) == W(hand), so they stay unkeyed.

The body node also moved: it used to sit at the pelvis (dsk_hip, 0.9921 m)
and now sits at PES's mover sk_root_hip (1.0961 m). Same world skeleton, but
the player line must re-aim the node: with delta the bind vector from the old
node to the new one (GF coords, (0, -0.01835, 0.104019)),

    player'(f) = player(f) + old_offset + R_body(f) . delta - new_offset

Ball keyframes and the metadata tail are untouched - the selector's contract
reads those, so selection behaviour is unchanged. Files are stamped with a
<tpose_rig> tag and skipped when already stamped, so the migration is
idempotent.

  python3 migrate_anims_tpose.py <dir-or-file>...   (in place)
"""

import math
import os
import sys

import retarget

OLD_BODY_OFFSET = retarget.fox_to_gf((0.0, 0.9921, -0.0184))
NEW_BODY_OFFSET = retarget.GF_BIND["body"][0]
BODY_DELTA = tuple(o - n for o, n in zip(OLD_BODY_OFFSET, NEW_BODY_OFFSET))
STAMP = "tpose_rig"


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def q_conj(q):
    return (-q[0], -q[1], -q[2], q[3])


def q_rot(q, v):
    r = q_mul(q_mul(q, (v[0], v[1], v[2], 0.0)), q_conj(q))
    return (r[0], r[1], r[2])


def q_norm(q):
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q) if n > 0 else (0.0, 0.0, 0.0, 1.0)


def q_nlerp(a, b, t):
    if sum(x * y for x, y in zip(a, b)) < 0.0:
        b = tuple(-c for c in b)
    return q_norm(tuple(x + (y - x) * t for x, y in zip(a, b)))


def _is_identity(q, tol=1e-6):
    return abs(q[0]) < tol and abs(q[1]) < tol and abs(q[2]) < tol


def conversions():
    """node -> (pre, post): q' = pre * q * post; None where identity."""
    conv = {}
    for node in retarget.GF_JOINT_ORDER:
        parent = retarget.GF_PARENT[node]
        pre = retarget.ALIGN_GF[parent] if parent else (0.0, 0.0, 0.0, 1.0)
        post = q_conj(retarget.ALIGN_GF[node])
        if _is_identity(pre) and _is_identity(post):
            conv[node] = None
        else:
            conv[node] = (pre, post)
    return conv


def _parse_keys(parts, stride):
    keys = []
    at = 1
    while at + stride <= len(parts):
        keys.append((int(parts[at]),
                     tuple(float(c) for c in parts[at + 1:at + stride])))
        at += stride
    return keys


def _sample(keys, frame):
    """nlerp between bracketing quat keys."""
    if not keys:
        return (0.0, 0.0, 0.0, 1.0)
    if frame <= keys[0][0]:
        return keys[0][1]
    for i in range(1, len(keys)):
        f0, q0 = keys[i - 1]
        f1, q1 = keys[i]
        if f1 >= frame:
            t = (frame - f0) / (f1 - f0) if f1 != f0 else 0.0
            return q_nlerp(q0, q1, t)
    return keys[-1][1]


def convert_text(text, conv=None):
    conv = conv or conversions()
    lines = text.split("\n")

    body_keys = None
    curve_names = set()
    for line in lines:
        parts = line.split(",")
        if parts and parts[0] == "body":
            body_keys = _parse_keys(parts, 5)
        if parts and parts[0] in conv or (parts and parts[0] == "player"):
            curve_names.add(parts[0])
    if "<%s>" % STAMP in text:
        return None                      # already migrated
    if body_keys is None or "player" not in curve_names:
        return None                      # not a skeleton clip (extensions etc.)

    out = []
    tail_at = None
    for index, line in enumerate(lines):
        parts = line.split(",")
        name = parts[0] if parts else ""
        if name.startswith("<"):
            tail_at = index
            break
        if name == "player":
            keys = _parse_keys(parts, 4)
            rebuilt = ["player"]
            for frame, pos in keys:
                turned = q_rot(_sample(body_keys, frame), BODY_DELTA)
                rebuilt.append("%d,%f,%f,%f" % (frame,
                                                pos[0] + turned[0],
                                                pos[1] + turned[1],
                                                pos[2] + turned[2]))
            out.append(",".join(rebuilt))
        elif name in conv and conv[name] is not None:
            pre, post = conv[name]
            keys = _parse_keys(parts, 5)
            rebuilt = [name]
            for frame, q in keys:
                q = q_norm(q_mul(q_mul(pre, q), post))
                rebuilt.append("%d,%f,%f,%f,%f" % ((frame,) + q))
            out.append(",".join(rebuilt))
        else:
            out.append(line)

    # A keyed elbow with an unkeyed hand: the hand must not inherit the
    # elbow's rebasing untwisted. Same for any keyed parent whose child's W
    # differs and is absent - add the constant corrective line.
    frames = sorted({f for f, _ in body_keys})
    first, last = frames[0], frames[-1]
    for node in retarget.GF_JOINT_ORDER:
        if node in curve_names:
            continue
        parent = retarget.GF_PARENT[node]
        if parent is None or parent not in curve_names:
            continue
        wp, wb = retarget.ALIGN_GF[parent], retarget.ALIGN_GF[node]
        constant = q_norm(q_mul(wp, q_conj(wb)))
        if _is_identity(constant):
            continue
        key = "%f,%f,%f,%f" % constant
        out.append("%s,%d,%s,%d,%s" % (node, first, key, last, key))

    # The stamp goes at the very end: Animation::Load reads node lines until
    # the first "<" or "extension" token, then extension lines, then XML -
    # anything XML-shaped inserted BEFORE the extension block would end it.
    out.extend(lines[tail_at:] if tail_at is not None else [])
    out.append("<%s>" % STAMP)
    out.append("\t1")
    out.append("</%s>" % STAMP)
    return "\n".join(out)


def main():
    conv = conversions()
    count = skipped = 0
    for target in sys.argv[1:]:
        paths = []
        if os.path.isdir(target):
            for root, _, files in os.walk(target):
                paths += [os.path.join(root, f) for f in sorted(files)
                          if f.endswith(".anim") and not f.startswith("pes_")]
        else:
            paths.append(target)
        for path in paths:
            text = open(path).read()
            converted = convert_text(text, conv)
            if converted is None:
                skipped += 1
                continue
            open(path, "w").write(converted)
            count += 1
    print("migrated %d anims, skipped %d (already stamped or no skeleton)"
          % (count, skipped))


if __name__ == "__main__":
    main()

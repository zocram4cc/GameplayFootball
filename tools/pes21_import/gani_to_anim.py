"""Converts decoded PES .gani body animations into GameplayFootball .anim files.

Pipeline per output frame (GF frame = 10 ms):
  1. sample every mapped PES bone quaternion (nlerp) and the root/motion
     position tracks (lerp) at the equivalent PES time (PES frame = 1/59.94 s)
  2. run FK over the PES bind skeleton (world-aligned bind frames, so world
     rotation = product of local anim quats down the chain)
  3. map world transforms from Fox coords (Y up, +Z forward) into GF coords
     (Z up, -Y forward): (x, y, z) -> (x, -z, y), quats likewise
  4. solve GF's 13 nodes:
       body / middle / neck / ankles: direct orientation match
       thighs & shoulders: aim the bind -Z bone axis at the FK'd limb
         direction, rolled so the hinge axis lands on local X
       knees (+X) & elbows (-X): pure hinge angles, GF's conventions
  5. write the .anim text: a player root line + one line per node + metadata

Usage:
  python3 gani_to_anim.py in.gani out.anim [--type movement]
  python3 gani_to_anim.py --batch <dir-of-ganis> <out-dir> [--type movement]
"""

import argparse
import math
import os

import gani
import retarget

PES_FRAME_MS = 1000.0 / 59.94
GF_FRAME_MS = 10.0


# --- minimal quaternion/vector toolkit (x, y, z, w) -------------------------

def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def q_conj(q):
    return (-q[0], -q[1], -q[2], q[3])


def q_norm(q):
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q) if n > 0 else (0.0, 0.0, 0.0, 1.0)


def q_rot(q, v):
    """Rotate vector v by quaternion q."""
    qv = (v[0], v[1], v[2], 0.0)
    r = q_mul(q_mul(q, qv), q_conj(q))
    return (r[0], r[1], r[2])


def q_nlerp(a, b, t):
    if sum(x * y for x, y in zip(a, b)) < 0.0:
        b = tuple(-c for c in b)
    return q_norm(tuple(x + (y - x) * t for x, y in zip(a, b)))


def q_axis_angle(axis, angle):
    s = math.sin(angle * 0.5)
    return q_norm((axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle * 0.5)))


def q_from_matrix(m):
    """Columns m = (X, Y, Z) basis vectors -> quaternion."""
    xx, yx, zx = m[0]
    xy, yy, zy = m[1]
    xz, yz, zz = m[2]
    tr = xx + yy + zz
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return q_norm(((yz - zy) / s, (zx - xz) / s, (xy - yx) / s, 0.25 * s))
    if xx > yy and xx > zz:
        s = math.sqrt(1.0 + xx - yy - zz) * 2
        return q_norm((0.25 * s, (yx + xy) / s, (zx + xz) / s, (yz - zy) / s))
    if yy > zz:
        s = math.sqrt(1.0 + yy - xx - zz) * 2
        return q_norm(((yx + xy) / s, 0.25 * s, (zy + yz) / s, (zx - xz) / s))
    s = math.sqrt(1.0 + zz - xx - yy) * 2
    return q_norm(((zx + xz) / s, (zy + yz) / s, 0.25 * s, (xy - yx) / s))


def v_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def v_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def v_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def v_len(a):
    return math.sqrt(v_dot(a, a))


def v_normalize(a):
    n = v_len(a)
    return (a[0] / n, a[1] / n, a[2] / n) if n > 1e-9 else (0.0, 0.0, 0.0)


# --- Fox -> GF coordinate mapping -------------------------------------------

def map_vec(v):
    return (v[0], -v[2], v[1])


def map_quat(q):
    return (q[0], -q[2], q[1], q[3])


# --- track sampling ----------------------------------------------------------

class Sampler:
    """Samples one decoded segment at fractional PES frame time."""

    def __init__(self, segment):
        self.frames = segment.frames
        self.quats = segment.quats
        self.vecs = segment.vecs

    def _bracket(self, t):
        frames = self.frames
        if t <= frames[0]:
            return 0, 0, 0.0
        if t >= frames[-1]:
            return len(frames) - 1, len(frames) - 1, 0.0
        lo = 0
        for i in range(1, len(frames)):
            if frames[i] >= t:
                lo = i - 1
                span = frames[i] - frames[lo]
                return lo, i, (t - frames[lo]) / span if span else 0.0
        return len(frames) - 1, len(frames) - 1, 0.0

    def quat(self, t):
        a, b, f = self._bracket(t)
        return q_nlerp(self.quats[a], self.quats[b], f) if self.quats else (0, 0, 0, 1)

    def vec(self, t):
        a, b, f = self._bracket(t)
        if not self.vecs:
            return (0.0, 0.0, 0.0)
        va, vb = self.vecs[a], self.vecs[b]
        return tuple(x + (y - x) * f for x, y in zip(va, vb))


def build_samplers(g):
    """(bone -> quat Sampler, root quat/pos, motion quat/pos)."""
    bones = {}
    for (u, s), bone in retarget.PES_TRACK_MAP.items():
        if u < len(g.units) and s < len(g.units[u].segments):
            seg = g.units[u].segments[s]
            if seg.quats:
                bones[bone] = Sampler(seg)
    root = g.units[retarget.PES_ROOT_UNIT].segments
    motion = g.units[retarget.PES_MOTION_UNIT].segments
    root_q = Sampler(root[0]) if root and root[0].quats else None
    root_p = Sampler(root[1]) if len(root) > 1 and root[1].vecs else None
    mot_q = Sampler(motion[0]) if motion and motion[0].quats else None
    mot_p = Sampler(motion[1]) if len(motion) > 1 and motion[1].vecs else None
    return bones, root_q, root_p, mot_q, mot_p


# --- FK over the PES skeleton -------------------------------------------------

def fk_pose(bones, root_q, root_p, mot_q, mot_p, t):
    """World rotation+position per PES bone at PES frame t (Fox coords)."""
    rot = {}
    pos = {}
    bind = retarget.PES_BIND
    scale = retarget.PES_POS_TO_M

    rq = q_norm(root_q.quat(t)) if root_q else (0, 0, 0, 1)
    rp = tuple(c * scale for c in root_p.vec(t)) if root_p else (0.0, 0.0, 0.0)

    mq = q_norm(mot_q.quat(t)) if mot_q else (0, 0, 0, 1)
    mp = tuple(c * scale for c in mot_p.vec(t)) if mot_p else (0.0, 0.0, 0.0)

    rot["motion"] = q_mul(rq, mq)
    pos["motion"] = v_add(rp, q_rot(rq, v_add(bind["motion"][0], mp)))

    order = ["dsk_hip", "sk_thigh_l", "sk_leg_l", "sk_foot_l",
             "sk_thigh_r", "sk_leg_r", "sk_foot_r",
             "sk_belly", "sk_chest", "sk_neck", "sk_head",
             "sk_shoulder_l", "sk_upperarm_l", "sk_forearm_l", "sk_hand_l",
             "sk_shoulder_r", "sk_upperarm_r", "sk_forearm_r", "sk_hand_r"]
    for bone in order:
        bpos, parent = bind[bone]
        local = bones[bone].quat(t) if bone in bones else (0, 0, 0, 1)
        offset = v_sub(bpos, bind[parent][0])
        rot[bone] = q_norm(q_mul(rot[parent], local))
        pos[bone] = v_add(pos[parent], q_rot(rot[parent], offset))
    return rot, pos


# --- GF solve -----------------------------------------------------------------

def _aim_frame(parent_world, bone_dir, hinge_world, hinge_local_sign):
    """World rotation for a GF limb node: -Z along bone_dir, X on the hinge."""
    z_axis = v_normalize(tuple(-c for c in bone_dir))
    x_axis = tuple(hinge_local_sign * c for c in hinge_world)
    x_axis = v_normalize(v_sub(x_axis, tuple(v_dot(x_axis, z_axis) * c for c in z_axis)))
    if v_len(x_axis) < 1e-6:
        fallback = q_rot(parent_world, (1.0, 0.0, 0.0))
        x_axis = v_normalize(v_sub(fallback, tuple(v_dot(fallback, z_axis) * c
                                                   for c in z_axis)))
    y_axis = v_cross(z_axis, x_axis)
    return q_from_matrix((x_axis, y_axis, z_axis))


def _solve_chain(parent_world_q, p_a, p_b, p_c, hinge_local_sign):
    """A 2-bone chain (thigh/knee or shoulder/elbow) from world positions.

    Returns (upper local quat, hinge local quat, upper world quat, hinge world quat).
    The hinge rotates about local X * hinge_local_sign by the bend angle.
    """
    d_upper = v_normalize(v_sub(p_b, p_a))
    d_lower = v_normalize(v_sub(p_c, p_b))
    hinge = v_cross(d_upper, d_lower)
    if v_len(hinge) < 1e-6:
        hinge = q_rot(parent_world_q, (1.0, 0.0, 0.0))
    hinge = v_normalize(hinge)

    upper_world = _aim_frame(parent_world_q, d_upper, hinge, hinge_local_sign)
    upper_local = q_norm(q_mul(q_conj(parent_world_q), upper_world))

    # the aim frame put the world hinge on local X*sign, so the +bend rotation
    # from upper to lower reads as sign*bend about local +X: +bend for knees,
    # -bend for elbows -- exactly GF's conventions.
    bend = math.acos(max(-1.0, min(1.0, v_dot(d_upper, d_lower))))
    hinge_local = q_axis_angle((1.0, 0.0, 0.0), hinge_local_sign * bend)
    hinge_world = q_norm(q_mul(upper_world, hinge_local))
    return upper_local, hinge_local, upper_world, hinge_world


def solve_gf(rot, pos):
    """PES world pose -> GF node local quaternions + player root."""
    out = {}

    # world orientations in GF coords
    w = {bone: map_quat(q) for bone, q in rot.items()}
    p = {bone: map_vec(v) for bone, v in pos.items()}

    body = q_norm(w["dsk_hip"])
    out["body"] = body
    out["middle"] = q_norm(q_mul(q_conj(body), w["sk_chest"]))
    out["neck"] = q_norm(q_mul(q_conj(w["sk_chest"]), w["sk_neck"]))
    out["head"] = q_norm(q_mul(q_conj(w["sk_neck"]), w["sk_head"]))

    for side, sign in (("left", "l"), ("right", "r")):
        # legs: thigh aims at the knee, knee is a +X hinge, ankle matches the
        # foot's world orientation
        thigh_l, knee_l, _, knee_w = _solve_chain(
            body, p["sk_thigh_" + sign], p["sk_leg_" + sign],
            p["sk_foot_" + sign], +1.0)
        out[side + "_thigh"] = thigh_l
        out[side + "_knee"] = knee_l
        out[side + "_ankle"] = q_norm(q_mul(q_conj(knee_w), w["sk_foot_" + sign]))

        # arms: GF's shoulder node is the upper-arm pivot (parent: middle);
        # elbows hinge on -X; hands match the PES wrist orientation
        parent = q_norm(q_mul(body, out["middle"]))
        shoulder_l, elbow_l, _, elbow_w = _solve_chain(
            parent, p["sk_upperarm_" + sign], p["sk_forearm_" + sign],
            p["sk_hand_" + sign], -1.0)
        out[side + "_shoulder"] = shoulder_l
        out[side + "_elbow"] = elbow_l
        out[side + "_hand"] = q_norm(q_mul(q_conj(elbow_w),
                                           w["sk_hand_" + sign]))

    hip = p["motion"]
    return out, hip


# --- .anim writing ------------------------------------------------------------

GF_NODES = ["body", "middle", "neck", "head",
            "left_shoulder", "left_elbow", "left_hand",
            "right_shoulder", "right_elbow", "right_hand",
            "left_thigh", "left_knee", "left_ankle",
            "right_thigh", "right_knee", "right_ankle"]


def convert(blob, anim_type="movement", key_step=2):
    """gani bytes -> .anim text."""
    g = gani.parse(blob)
    bones, root_q, root_p, mot_q, mot_p = build_samplers(g)

    duration_ms = g.frame_count * PES_FRAME_MS
    gf_frames = max(2, int(round(duration_ms / GF_FRAME_MS)))

    keys = {node: [] for node in GF_NODES}
    player = []
    origin = None
    for f in range(0, gf_frames + 1, key_step):
        t = min(f * GF_FRAME_MS / PES_FRAME_MS, float(g.frame_count))
        rot, pos = fk_pose(bones, root_q, root_p, mot_q, mot_p, t)
        locals_, hip = solve_gf(rot, pos)
        if origin is None:
            origin = (hip[0], hip[1])
        player.append((f, hip[0] - origin[0], hip[1] - origin[1],
                       hip[2] - retarget.GF_BODY_HEIGHT))
        for node in GF_NODES:
            keys[node].append((f,) + locals_[node])

    lines = []
    lines.append("player," + ",".join(
        "%d,%f,%f,%f" % k for k in player))
    for node in GF_NODES:
        lines.append(node + "," + ",".join(
            "%d,%f,%f,%f,%f" % k for k in keys[node]))
    lines.append("<type>")
    lines.append("\t" + anim_type)
    lines.append("</type>")
    return "\n".join(lines) + "\n", g


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("src", help=".gani file, or a directory with --batch")
    parser.add_argument("dest", help=".anim file, or a directory with --batch")
    parser.add_argument("--batch", action="store_true")
    parser.add_argument("--type", default="movement")
    parser.add_argument("--key-step", type=int, default=2,
                        help="GF frames between keys (2 = every 20ms)")
    args = parser.parse_args()

    if not args.batch:
        text, g = convert(open(args.src, "rb").read(), args.type, args.key_step)
        open(args.dest, "w").write(text)
        print("wrote %s (%d PES frames -> %d bytes)"
              % (args.dest, g.frame_count, len(text)))
        return

    os.makedirs(args.dest, exist_ok=True)
    done = failed = 0
    for name in sorted(os.listdir(args.src)):
        if not name.endswith(".gani"):
            continue
        try:
            text, _ = convert(open(os.path.join(args.src, name), "rb").read(),
                              args.type, args.key_step)
            out = os.path.join(args.dest, os.path.splitext(name)[0] + ".anim")
            open(out, "w").write(text)
            done += 1
        except Exception as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc))
    print("converted %d, failed %d -> %s" % (done, failed, args.dest))


if __name__ == "__main__":
    main()

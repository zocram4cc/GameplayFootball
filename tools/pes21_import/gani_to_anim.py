"""Converts decoded PES .gani body animations into GameplayFootball .anim files.

The engine's skeleton IS the Fox anim skeleton (retarget.GF_NODES, bind =
body_anim_skel.ask, 1:1), so conversion is a change of basis, not a retarget:

  1. sample every PES bone's LOCAL quaternion (nlerp) and the root/motion
     position tracks (lerp) at the equivalent PES time (PES frame = 1/59.94 s,
     GF frame = 10 ms)
  2. map each local quaternion from Fox coords (Y up, +Z forward) into GF
     coords (Z up, faces -Y): both rigs' binds are world-aligned, so the same
     conjugation (x, y, z) -> (x, -z, y) applies to every local, VERBATIM
  3. body = RIG_ROOT o motion (GF's body node is the PES mover sk_root_hip;
     the player line carries the world translation); every other node's line
     is its PES bone's local, verbatim

Nothing is solved and nothing is lost: clavicles (sk_shoulder_*), the
belly/chest spine chain and the wrists all keep their own tracks, and every
GF frame gets a key (key_step 1), so no posing data is condensed away.

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


def v_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


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


# --- root handling ------------------------------------------------------------

def root_yaw(q):
    """Yaw of a Fox root quaternion about +Y (0 faces +Z, pi/2 faces +X)."""
    f = q_rot(q, (0.0, 0.0, 1.0))
    return math.atan2(f[0], f[2])


def sample_root(bones, root_q, root_p, mot_q, mot_p, t, strip_root=False,
                scale=None, mover_scale=None):
    """(body world quat, motion world position) at PES frame t, Fox coords.

    With strip_root the RIG_ROOT translation and yaw are removed (the pose
    stays in place, facing +Z, keeping any root tilt) - for clips whose world
    placement is driven externally, like the fixdemo entrance choreography.

    Two scales, because the two position channels are not in the same unit and
    each was measured on its own evidence. `scale` is for RIG_ROOT, the world
    path: the gameplay set reads right at 1/20480 (`run_3_3_000_holdmiss`
    covers 5.3 m in 1.47 s, where 1/128000 gives 0.84 m - a run at 0.6 m/s)
    and the fixdemo set at 1/128000 (goal_2018_run_30's choreography path is
    6.0 m over 2.5 s; at 1/20480 it would be 37 m).

    `mover_scale` is for sk_root_hip, which carries the vertical and nothing
    else (RIG_ROOT's own y is flat in every clip measured). It defaults to
    `scale`, and 1/20480 is the number calibrate_pos_scale.py measured against
    ankle and pelvis heights. A fixdemo clip read at the path's scale instead
    flattens every jump: goal_celebrate_0057 is a somersault whose pelvis
    should swing 0.79 m to 1.75 m, and at 1/128000 it stays between 1.05 and
    1.20 - the body turns upside down at standing height and the actor appears
    to lie down in mid-air.
    """
    bind = retarget.PES_BIND
    if scale is None:
        scale = retarget.PES_POS_TO_M
    if mover_scale is None:
        mover_scale = scale

    rq = q_norm(root_q.quat(t)) if root_q else (0, 0, 0, 1)
    rp = tuple(c * scale for c in root_p.vec(t)) if root_p else (0.0, 0.0, 0.0)
    if strip_root:
        rq = q_mul(q_axis_angle((0.0, 1.0, 0.0), -root_yaw(rq)), rq)
        rp = (0.0, 0.0, 0.0)

    mq = q_norm(mot_q.quat(t)) if mot_q else (0, 0, 0, 1)
    mp = tuple(c * mover_scale for c in mot_p.vec(t)) if mot_p else (0.0, 0.0, 0.0)

    body_q = q_mul(rq, mq)
    body_p = v_add(rp, q_rot(rq, v_add(bind["motion"][0], mp)))
    return body_q, body_p


# legacy full-FK helper, still used by calibrate_pos_scale.py and by tools
# that need world transforms of every bone (metrics research, previews)
def fk_pose(bones, root_q, root_p, mot_q, mot_p, t, strip_root=False,
            scale=None):
    """World rotation+position per PES bone at PES frame t (Fox coords)."""
    rot = {}
    pos = {}
    bind = retarget.PES_BIND

    body_q, body_p = sample_root(bones, root_q, root_p, mot_q, mot_p, t,
                                 strip_root, scale)
    rot["motion"] = body_q
    pos["motion"] = body_p

    order = ["dsk_hip", "sk_thigh_l", "sk_leg_l", "sk_foot_l",
             "sk_thigh_r", "sk_leg_r", "sk_foot_r",
             "sk_belly", "sk_chest", "sk_neck", "sk_head",
             "sk_shoulder_l", "sk_upperarm_l", "sk_forearm_l", "sk_hand_l",
             "sk_shoulder_r", "sk_upperarm_r", "sk_forearm_r", "sk_hand_r"]
    for bone in order:
        bpos, parent = bind[bone]
        local = bones[bone].quat(t) if bone in bones else (0, 0, 0, 1)
        offset = tuple(b - p for b, p in zip(bpos, bind[parent][0]))
        rot[bone] = q_norm(q_mul(rot[parent], local))
        pos[bone] = v_add(pos[parent], q_rot(rot[parent], offset))
    return rot, pos


# --- .anim writing ------------------------------------------------------------

GF_NODES = list(retarget.GF_JOINT_ORDER)


def convert(blob, anim_type="movement", key_step=1, strip_root=False,
            pos_scale=None, mover_scale=None):
    """gani bytes -> .anim text, 1:1 onto the native rig.

    `pos_scale` is metres per raw unit of the world path and `mover_scale` of
    the vertical (see sample_root). Match animation wants
    retarget.PES_POS_TO_M_GAMEPLAY for both; the fixdemo cutscene set wants the
    legacy scale for the path and the gameplay one for the vertical.
    """
    g = gani.parse(blob)
    bones, root_q, root_p, mot_q, mot_p = build_samplers(g)

    duration_ms = g.frame_count * PES_FRAME_MS
    gf_frames = max(2, int(round(duration_ms / GF_FRAME_MS)))

    body_offset = retarget.GF_BIND["body"][0]

    keys = {node: [] for node in GF_NODES}
    player = []
    origin = None
    for f in range(0, gf_frames + 1, key_step):
        t = min(f * GF_FRAME_MS / PES_FRAME_MS, float(g.frame_count))
        body_q, body_p = sample_root(bones, root_q, root_p, mot_q, mot_p, t,
                                     strip_root, pos_scale, mover_scale)
        p = map_vec(body_p)
        root = tuple(a - b for a, b in zip(p, body_offset))
        if origin is None:
            origin = (root[0], root[1])
        player.append((f, root[0] - origin[0], root[1] - origin[1], root[2]))
        keys["body"].append((f,) + map_quat(q_norm(body_q)))
        for node in GF_NODES:
            if node == "body":
                continue
            bone = retarget.PES_OF_GF[node]
            local = bones[bone].quat(t) if bone in bones else (0.0, 0.0, 0.0, 1.0)
            keys[node].append((f,) + map_quat(q_norm(local)))

    lines = []
    lines.append("player," + ",".join(
        "%d,%f,%f,%f" % k for k in player))
    for node in GF_NODES:
        lines.append(node + "," + ",".join(
            "%d,%f,%f,%f,%f" % k for k in keys[node]))
    lines.append("<type>")
    lines.append("\t" + anim_type)
    lines.append("</type>")
    # Which scale the vertical was read at, so a migration can tell a clip that
    # already has it from one that predates it. migrate_cutscene_mover.py skips
    # any clip carrying this tag; without it, reconverting and then migrating
    # multiplied the vertical by 6.25 a second time.
    if mover_scale is not None and mover_scale != (pos_scale or retarget.PES_POS_TO_M):
        lines.append("<mover_scale>")
        lines.append("\tgameplay")
        lines.append("</mover_scale>")
    return "\n".join(lines) + "\n", g


def root_sampler(g):
    """RIG_ROOT world motion of a parsed gani, in Fox space.

    Returns a function of PES frame time -> (x_m, z_m, yaw_rad): the root
    translation in metres and its yaw about +Y. This is the part convert()
    removes under strip_root, so an external mover can re-apply it.

    Read at the gameplay scale. It was read at PES_POS_TO_M (1/128000) on the
    strength of one celebration path, and every entrance actor then glided at a
    sixth of the speed his legs were walking - "pulled by a force" (owner,
    03-09). Measured on the raw ganis: dml_ent_entering04_walk15 moves its
    RIG_ROOT 2.25 m/s at 1/20480 while its stance foot covers ground at 2.46 m/s
    and its 22 steps of 0.72 m over 6.0 s make 2.63 m/s; at 1/128000 it is
    0.36 m/s. The stair climb (1.82 vs 1.9-2.0) and the somersault
    (3.23 vs a flight-phase-lowered 2.16) agree. Legs do not have a unit
    problem, so they decide.
    """
    _, root_q, root_p, _, _ = build_samplers(g)
    scale = retarget.PES_POS_TO_M_GAMEPLAY

    def sample(t):
        yaw = root_yaw(q_norm(root_q.quat(t))) if root_q else 0.0
        p = root_p.vec(t) if root_p else (0.0, 0.0, 0.0)
        return (p[0] * scale, p[2] * scale, yaw)

    return sample


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("src", help=".gani file, or a directory with --batch")
    parser.add_argument("dest", help=".anim file, or a directory with --batch")
    parser.add_argument("--batch", action="store_true")
    parser.add_argument("--type", default="movement")
    parser.add_argument("--key-step", type=int, default=1,
                        help="GF frames between keys (1 = every 10ms)")
    parser.add_argument("--gameplay-scale", action="store_true",
                        help="use the calibrated match-animation position "
                             "scale (retarget.PES_POS_TO_M_GAMEPLAY)")
    parser.add_argument("--strip-root", action="store_true",
                        help="remove RIG_ROOT yaw+translation (in-place clip "
                             "for externally driven playback, e.g. .chor)")
    args = parser.parse_args()

    pos_scale = retarget.PES_POS_TO_M_GAMEPLAY if args.gameplay_scale else None

    if not args.batch:
        text, g = convert(open(args.src, "rb").read(), args.type, args.key_step,
                          args.strip_root, pos_scale)
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
                              args.type, args.key_step, args.strip_root,
                              pos_scale)
            out = os.path.join(args.dest, os.path.splitext(name)[0] + ".anim")
            open(out, "w").write(text)
            done += 1
        except Exception as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc))
    print("converted %d, failed %d -> %s" % (done, failed, args.dest))


if __name__ == "__main__":
    main()

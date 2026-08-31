#!/usr/bin/env python3
"""Imports PES's hand-pose library as named finger poses for the engine.

Where PES keeps its finger animation, measured rather than assumed:

  * Body ganis carry none of it. All 4,389 clips in dt13's
    Body/body_anime_file*.mtar have exactly fifteen units and twenty-seven
    segments - the twenty bones of body_skel.frig, of which none is skh_* -
    and body.skl has no skh_* bone at all.
  * PES ships a second rig for the hand:
    common/anime/FoxAnim/Hand/CharacterAssets/pes_human_hand_141203.frig,
    named "HumanHand", 21 units / 23 tracks, and a bone table of twenty: a
    root, then the nineteen skh_*_l bones of ONE hand.
  * Beside it, common/anime/FoxAnim/Hand/Animations holds 162 ganis, and every
    single one is one frame long. They are poses, not clips - and their names
    say what they are for: normal, relax, nigiri (a grip), open_full,
    open_full_ball, move_nigiri, kp_catch_before / kp_catch_after / kp_hold
    (the keeper), clap, pointing, taore (falling), trophy_*.
  * All 162 names appear in a contiguous string pool in PES2021.exe (from
    0x2b91020), so the game picks a pose by name from code; no shipped table
    binds poses to body animations.

So: one hand, authored once, mirrored to the other, chosen from context. This
converts that library into media/objects/players/handposes.txt - one line per
joint per pose - and the engine picks between them (handrig.hpp).

Usage:
  python3 hand_poses.py --frig <pes_human_hand_141203.frig> \
      --ganis <Hand/Animations dir> --out <handposes.txt> [--poses a,b,c]
"""

import argparse
import math
import os

import frig
import gani
import retarget
import strcode

# Every skh_* bone the hand rig can name, as retarget knows them.
_LEFT_BONES = [bone for _, bone, _ in retarget.GF_NODES
               if bone.startswith("skh_") and bone.endswith("_l")]

# The two leading units of a hand gani are the rig root (a quaternion and a
# vector, twice over); the bone tracks follow, one quaternion each.
_ROOT_UNITS = 2


def rig_bones(frig_blob):
    """-> the hand rig's skh_* bones, in its own unit order.

    PES's unit order is thumb, index, middle, PINKY, ring; hand_l.skl lists
    ring before pinky. Reading the skl instead would put the ring finger's
    curl on the pinky, so the order comes from the rig.
    """
    name, hashes = frig.bone_table(frig_blob)
    names = {strcode.strcode32(bone): bone for bone in _LEFT_BONES}
    bones = [names[h] for h in hashes if h in names]
    if len(bones) != len(_LEFT_BONES):
        raise ValueError("not a hand rig: %d of %d skh_* bones in %r"
                         % (len(bones), len(_LEFT_BONES), name))
    return bones


def pose_quats(gani_blob, bones):
    """-> {bone: Fox-space local quaternion} for a one-frame hand gani."""
    g = gani.parse(gani_blob)
    tracks = g.units[_ROOT_UNITS:]
    if len(tracks) < len(bones):
        raise ValueError("hand gani has %d bone units, rig wants %d"
                         % (len(tracks), len(bones)))
    out = {}
    for bone, unit in zip(bones, tracks):
        if not unit.segments or not unit.segments[0].quats:
            raise ValueError("%s has no rotation" % bone)
        out[bone] = unit.segments[0].quats[0]
    return out


# --- coordinate mapping ------------------------------------------------------

def map_quat(q):
    """Fox coords (Y up, +Z forward) -> GF coords (Z up, faces -Y).

    The same conjugation gani_to_anim applies to every body local: the change
    of basis is a quarter turn about X, and conjugating by it permutes the
    components exactly like the vector map (x, y, z) -> (x, -z, y).
    """
    return (q[0], -q[2], q[1], q[3])


def map_quat_inverse(q):
    """GF -> Fox, so a pose can be stated in GF terms and pushed back."""
    return (q[0], q[2], -q[1], q[3])


def mirror_quat(q):
    """A GF rotation reflected across the sagittal plane (x -> -x).

    The mirror of a rotation R about that plane is S R S with S = diag(-1,1,1),
    which negates the y and z parts of the quaternion and leaves x and w.
    """
    return (q[0], -q[1], -q[2], q[3])


def to_gf_pose(fox_pose):
    """{skh_*_l bone: Fox quat} -> {GF joint: GF quat}, both hands.

    PES authors one hand and mirrors it, which is what the right hand's bind
    is too (hand_r.skl is hand_l.skl with x negated).

    The hand rig is authored in the RENDER bind (hand_[lr].skl); on the anim
    skeleton the whole hand subtree is turned by retarget.ALIGN_GF[hand], so
    every finger local conjugates by it: q' = W . q . W^-1 - the same
    rotation, re-expressed in the rotated frame. Without this a curl authored
    about the render hand's axes bends the anim-pose fingers sideways.
    """
    def _conj(w, q):
        return _q_mul(_q_mul(w, q), (-w[0], -w[1], -w[2], w[3]))

    align = {"_l": retarget.ALIGN_GF["left_hand"],
             "_r": retarget.ALIGN_GF["right_hand"]}
    out = {}
    for node, bone, _ in retarget.GF_NODES:
        if not bone.startswith("skh_"):
            continue
        left_bone = bone[:-2] + "_l"
        q = map_quat(fox_pose.get(left_bone, (0.0, 0.0, 0.0, 1.0)))
        if not bone.endswith("_l"):
            q = mirror_quat(q)
        out[node] = _conj(align[bone[-2:]], q)
    return out


# --- forward kinematics, for checking a pose does what it says ---------------

def _q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def _q_rot(q, v):
    x, y, z, w = q
    tx = 2.0 * (y * v[2] - z * v[1])
    ty = 2.0 * (z * v[0] - x * v[2])
    tz = 2.0 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + (y * tz - z * ty),
            v[1] + w * ty + (z * tx - x * tz),
            v[2] + w * tz + (x * ty - y * tx))


def fingertip(side, finger, pose):
    """World position (GF coords) of a finger's last joint under `pose`."""
    world = retarget.gf_world_bind()
    chain = [name for name in retarget.GF_JOINT_ORDER
             if name.startswith("%s_%s_" % (side, finger))]
    parent = "%s_hand" % side
    position = world[parent]
    rotation = (0.0, 0.0, 0.0, 1.0)
    for node in chain:
        offset, _ = retarget.GF_BIND[node]
        turned = _q_rot(rotation, offset)
        position = tuple(p + t for p, t in zip(position, turned))
        rotation = _q_mul(rotation, pose.get(node, (0.0, 0.0, 0.0, 1.0)))
    return position


# --- the file ---------------------------------------------------------------

HEADER = "# gfhandposes 1"


def render(poses):
    """{pose name: {GF joint: quat}} -> the engine's text form."""
    lines = [HEADER]
    for name in sorted(poses):
        lines.append("pose %s" % name)
        pose = poses[name]
        for node in retarget.GF_JOINT_ORDER[20:]:
            q = pose.get(node, (0.0, 0.0, 0.0, 1.0))
            lines.append("%s %.6f %.6f %.6f %.6f" % (node, q[0], q[1], q[2], q[3]))
    return "\n".join(lines) + "\n"


def parse(text):
    """The inverse of render(), for tests and tools."""
    poses = {}
    current = None
    lines = text.splitlines()
    if not lines or lines[0].strip() != HEADER:
        raise ValueError("not a hand pose file")
    for line in lines[1:]:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if fields[0] == "pose":
            current = {}
            poses[fields[1]] = current
        elif current is not None and len(fields) == 5:
            current[fields[0]] = tuple(float(c) for c in fields[1:])
    return poses


def convert(frig_path, gani_dir, wanted=None):
    """-> {pose name: {GF joint: quat}} for the ganis in `gani_dir`."""
    bones = rig_bones(open(frig_path, "rb").read())
    poses = {}
    for name in sorted(os.listdir(gani_dir)):
        if not name.endswith(".gani"):
            continue
        stem = name[:-len(".gani")]
        if wanted is not None and stem not in wanted:
            continue
        blob = open(os.path.join(gani_dir, name), "rb").read()
        poses[stem] = to_gf_pose(pose_quats(blob, bones))
    return poses


# Every pose ChooseHandPose (handrig.cpp) can return. An export that omits one is
# legal - the rig degrades that state to the bind pose - but nobody chooses that by
# accident, so it is said out loud at export time rather than discovered on a pitch.
ENGINE_POSES = ("normal", "relax", "move_nigiri", "clap", "taore",
                "open_full_ball", "kp_hold")


def missing_engine_poses(poses):
    """-> the engine-selected pose names this export will not carry."""
    return [name for name in ENGINE_POSES if name not in poses]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--frig", required=True,
                        help="pes_human_hand_141203.frig")
    parser.add_argument("--ganis", required=True,
                        help="the Hand/Animations directory")
    parser.add_argument("--out", required=True, help="handposes.txt")
    parser.add_argument("--poses", default=None,
                        help="comma-separated pose names (default: all)")
    args = parser.parse_args()

    wanted = set(args.poses.split(",")) if args.poses else None
    poses = convert(args.frig, args.ganis, wanted)
    if not poses:
        raise SystemExit("no hand ganis found in %s" % args.ganis)
    for name in sorted(missing_engine_poses(poses)):
        print("WARNING: the engine selects pose '%s' (handrig.cpp) and this file "
              "will not carry it - that state degrades to the bind pose" % name)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    open(args.out, "w").write(render(poses))

    world = retarget.gf_world_bind()
    reach = math.dist(world["left_index_dip"], world["left_hand"])
    print("wrote %s: %d pose(s), %d joints each"
          % (args.out, len(poses), len(retarget.GF_JOINT_ORDER) - 20))
    for name in sorted(poses):
        tip = fingertip("left", "index", poses[name])
        print("  %-24s index reach %.4f m (bind %.4f)"
              % (name, math.dist(tip, world["left_hand"]), reach))


if __name__ == "__main__":
    main()

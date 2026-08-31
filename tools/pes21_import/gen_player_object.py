"""Writes the engine's skeleton data files from retarget.py:

  data/media/objects/players/player.object     bind offsets (anim skeleton)
  data/media/animations/base.anim.util         the mesh AUTHORING pose
  data/media/animations/straight.anim.util     the bind pose (identity)

The engine's skeleton is defined once, in retarget.py (the Fox anim skeleton
plus PES's hand rig, 1:1); this generator keeps the files in sync so bind
offsets, joint numbering and the authoring pose can never drift apart.
Joint IDs are NOT the file's DFS order - the twenty body joints come first
and the thirty-eight finger joints follow, so a model converted before the
fingers existed still names the same bones (the engine builds that order in
jointorder.cpp; retarget.GF_JOINT_ORDER is the same list).

Bind rotations are identity: the anim skeleton is world-aligned. Meshes are
authored in the RENDER bind (retarget.PES_RENDER_BIND, arms ~45 deg down),
so base.anim.util carries retarget.BASE_POSE - the pose that puts this rig
back into the render bind - and HumanoidBase::PrepareFullbodyModel's
authoring->bind bake (R_straight * R_base^-1 * (v - p_base) + p_straight)
re-poses every vertex onto the T-pose rig at load. straight.anim.util stays
identity: the bind IS the anim pose.

The <geometry> attachments (pelvis.ase, trunk.ase, ...) stay on the legacy
nodes: they are the utility skeleton's body parts, used by
AnimCollection::AddExtraTouches to find which limb a ball keyframe touches.

  python3 gen_player_object.py [out.object]
"""

import os
import sys

import retarget

# node -> (ase file, geomobject name, optional local offset)
GEOMETRY = {
    "body": ("models/pelvis.ase", "pelvis", None),
    "middle": ("models/trunk.ase", "trunk", None),
    "head": ("models/head.ase", "head", (0.0, 0.01, -0.09)),
    "left_shoulder": ("models/upperarm.ase", "left_upperarm", None),
    "left_elbow": ("models/lowerarm.ase", "left_lowerarm", None),
    "right_shoulder": ("models/upperarm.ase", "right_upperarm", None),
    "right_elbow": ("models/lowerarm.ase", "right_lowerarm", None),
    "left_thigh": ("models/upperleg.ase", "left_upperleg", None),
    "left_knee": ("models/lowerleg.ase", "left_lowerleg", None),
    "left_ankle": ("models/foot.ase", "left_foot", None),
    "right_thigh": ("models/upperleg.ase", "right_upperleg", None),
    "right_knee": ("models/lowerleg.ase", "right_lowerleg", None),
    "right_ankle": ("models/foot.ase", "right_foot", None),
}


def fmt(v):
    return "%g, %g, %g" % tuple(round(c, 6) for c in v)


def emit(node, children, indent):
    pad = "\t" * indent
    offset, _ = retarget.GF_BIND[node]
    lines = [pad + "<node>",
             pad + "\t<name>%s</name>" % node,
             pad + "\t<position>%s</position>" % fmt(offset),
             pad + "\t<rotation>0, 0, 0, 0</rotation>"]
    if node in GEOMETRY:
        ase, name, geom_offset = GEOMETRY[node]
        lines += [pad + "\t<geometry>",
                  pad + "\t\t<filename>%s</filename>" % ase,
                  pad + "\t\t<name>%s</name>" % name]
        if geom_offset:
            lines.append(pad + "\t\t<position>%s</position>" % fmt(geom_offset))
        lines.append(pad + "\t</geometry>")
    for child in children.get(node, []):
        lines.append("")
        lines += emit(child, children, indent + 1)
    lines.append(pad + "</node>")
    return lines


def generate():
    children = {}
    roots = []
    for name, _, parent in retarget.GF_NODES:
        if parent is None:
            roots.append(name)
        else:
            children.setdefault(parent, []).append(name)
    # NB: the engine's XMLLoader does not understand <!-- comments -->, so the
    # provenance note lives here rather than in the generated file.
    lines = ["<object>", ""]
    for root in roots:
        lines += emit(root, children, 1)
    lines += ["", "</object>", ""]
    return "\n".join(lines)


# The nodes the pose utils key: the player line plus every body node. The
# fingers inherit the hand's authoring pose (their BASE_POSE is identity by
# construction - ALIGN is constant across the hand subtree), so they stay
# unkeyed and follow their parents, exactly as unkeyed nodes always have.
_POSE_UTIL_FRAMES = (0, 14)


def _pose_util(pose):
    """{node: local quat} -> .anim.util text (nodes at frames 0 and 14)."""
    lines = ["player," + ",".join("%d,0.000000,0.000000,0.000000" % f
                                  for f in _POSE_UTIL_FRAMES)]
    for node, _, _ in retarget.GF_BODY_NODES:
        q = pose.get(node, (0.0, 0.0, 0.0, 1.0))
        key = "%f,%f,%f,%f" % q
        lines.append(node + "," + ",".join("%d,%s" % (f, key)
                                           for f in _POSE_UTIL_FRAMES))
    lines += ["<type>", "\tmovement", "</type>"]
    return "\n".join(lines) + "\n"


def generate_base_anim():
    return _pose_util(retarget.BASE_POSE)


def generate_straight_anim():
    return _pose_util({})


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    data = os.path.join(here, "..", "..", "data", "media")
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        data, "objects", "players", "player.object")
    open(out, "w").write(generate())
    print("wrote %s (%d nodes)" % (out, len(retarget.GF_NODES)))
    if len(sys.argv) <= 1:
        for name, text in (("base.anim.util", generate_base_anim()),
                           ("straight.anim.util", generate_straight_anim())):
            path = os.path.join(data, "animations", name)
            open(path, "w").write(text)
            print("wrote %s" % path)

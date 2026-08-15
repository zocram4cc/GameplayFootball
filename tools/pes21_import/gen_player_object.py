"""Writes data/media/objects/players/player.object from retarget.GF_NODES.

The engine's skeleton is defined once, in retarget.py (the PES animated rig,
1:1); this generator keeps the XML in sync so joint IDs (DFS order), bind
offsets and the vertex-colour weight encoding can never drift apart.

Bind rotations are identity: the PES rig is world-aligned, and every stock
GF animation keys all legacy nodes anyway (Animation::Apply REPLACES node
rotations, so bind rotations only ever showed through on unkeyed nodes).

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


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        here, "..", "..", "data", "media", "objects", "players", "player.object")
    text = generate()
    open(out, "w").write(text)
    print("wrote %s (%d nodes)" % (out, len(retarget.GF_NODES)))

"""Migrates a legacy fullbody ASE onto the native PES rig.

The legacy convention: 16 joints (old player.object DFS), geometry stored as
base-pose world coordinates (base.anim.util: trunk pitched ~25 deg, elbows
~62, knees bent). The native-rig convention: 20 joints (PES animated rig,
retarget.GF_NODES), geometry stored at the PES bind (identity pose).

Per vertex, with its OLD skin weights w_b:

    v_new = sum_b w_b * [ R_oldbase_b^-1 * (v - p_oldbase_b) + p_newbind_b ]

i.e. proper blended linear blend skinning from the old base pose onto the
new bind. The previous migration attempt rotated each vertex about its
DOMINANT joint only, which shredded anything blended across two joints
(forearms, hands); this one blends every influence.

Vertex colours are re-encoded with the new joint IDs. Normals are rotated by
the blended inverse base rotation.

  python3 migrate_to_native_rig.py <fullbody.ase> [-o out.ase]
"""

import argparse
import math
import re

import retarget

# the OLD skeleton: node -> (local offset, parent), from the 16-node
# player.object this tool retires (bind rotations were always overridden by
# base.anim.util's keys, so offsets are all that matters here)
OLD_BIND = {
    "body": ((0.0, 0.0, 0.96), None),
    "middle": ((0.0, 0.0, 0.15), "body"),
    "neck": ((0.0, -0.03, 0.5), "middle"),
    "head": ((0.0, -0.01, 0.09), "neck"),
    "left_shoulder": ((0.16, -0.01, 0.48), "middle"),
    "left_elbow": ((-0.01, 0.0, -0.33), "left_shoulder"),
    "left_hand": ((0.0, 0.0, -0.28), "left_elbow"),
    "right_shoulder": ((-0.16, -0.01, 0.48), "middle"),
    "right_elbow": ((0.01, 0.0, -0.33), "right_shoulder"),
    "right_hand": ((0.0, 0.0, -0.28), "right_elbow"),
    "left_thigh": ((0.087, 0.0, -0.01), "body"),
    "left_knee": ((0.0, 0.0, -0.42), "left_thigh"),
    "left_ankle": ((0.0, -0.04, -0.44), "left_knee"),
    "right_thigh": ((-0.087, 0.0, -0.01), "body"),
    "right_knee": ((0.0, 0.0, -0.42), "right_thigh"),
    "right_ankle": ((0.0, -0.04, -0.44), "right_knee"),
}
OLD_JOINT_ORDER = ["body", "middle", "neck", "head",
                   "left_shoulder", "left_elbow", "left_hand",
                   "right_shoulder", "right_elbow", "right_hand",
                   "left_thigh", "left_knee", "left_ankle",
                   "right_thigh", "right_knee", "right_ankle"]


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def q_rot(q, v):
    x, y, z, w = q
    tx, ty, tz = (2.0 * (y * v[2] - z * v[1]),
                  2.0 * (z * v[0] - x * v[2]),
                  2.0 * (x * v[1] - y * v[0]))
    return (v[0] + w * tx + y * tz - z * ty,
            v[1] + w * ty + z * tx - x * tz,
            v[2] + w * tz + x * ty - y * tx)


def q_conj(q):
    return (-q[0], -q[1], -q[2], q[3])


def load_anim_pose(path):
    """First-key local quats per node from a .anim/.anim.util file."""
    pose = {}
    for line in open(path):
        parts = line.strip().split(",")
        if len(parts) >= 6 and parts[0] != "player" and not parts[0].startswith("<"):
            pose[parts[0]] = tuple(float(x) for x in parts[2:6])
    return pose


def old_base_pose(base_anim_path):
    """OLD skeleton FK under base.anim.util -> (world pos, world rot) per node."""
    local = load_anim_pose(base_anim_path)
    pos = {}
    rot = {}
    for name in OLD_JOINT_ORDER:
        offset, parent = OLD_BIND[name]
        q = local.get(name, (0.0, 0.0, 0.0, 1.0))
        if parent is None:
            pos[name] = offset
            rot[name] = q
        else:
            pos[name] = tuple(p + o for p, o in
                              zip(pos[parent], q_rot(rot[parent], offset)))
            rot[name] = q_mul(rot[parent], q)
    return pos, rot


def decode_weights(color):
    """ASE colour (3 floats 0..1) -> [(old jointID, weight)], normalized."""
    out = []
    for ch in color:
        v = ch * 255.0
        joint = int(v // 10)
        weight = (v - joint * 10.0) / 9.0
        if weight > 0.01 and 0 <= joint < len(OLD_JOINT_ORDER):
            out.append((joint, weight))
    total = sum(w for _, w in out)
    if total <= 0:
        return [(0, 1.0)]
    return [(j, w / total) for j, w in out]


def encode_color(joints):
    channels = []
    for j, w in joints[:3]:
        w = max(0.12, min(1.0, w))
        channels.append((j * 10 + w * 9.0) / 255.0)
    while len(channels) < 3:
        channels.append(0.0)
    return channels


VERTEX_RE = re.compile(r"^(\s*\*MESH_VERTEX\s+)(\d+)\t([-\d.e]+)\t([-\d.e]+)\t([-\d.e]+)\s*$")
VERTCOL_RE = re.compile(r"^(\s*\*MESH_VERTCOL\s+)(\d+)\t([-\d.e]+)\t([-\d.e]+)\t([-\d.e]+)\s*$")
FACE_RE = re.compile(r"^\s*\*MESH_FACE\s+(\d+):\s+A:\s+(\d+)\s+B:\s+(\d+)\s+C:\s+(\d+)")
CFACE_RE = re.compile(r"^\s*\*MESH_CFACE\s+(\d+)\t(\d+)\t(\d+)\t(\d+)")
VNORMAL_RE = re.compile(r"^(\s*\*MESH_VERTEXNORMAL\s+)(\d+)\t([-\d.e]+)\t([-\d.e]+)\t([-\d.e]+)\s*$")
FNORMAL_RE = re.compile(r"^(\s*\*MESH_FACENORMAL\s+)(\d+)\t([-\d.e]+)\t([-\d.e]+)\t([-\d.e]+)\s*$")


def migrate(text, base_anim_path):
    old_pos, old_rot = old_base_pose(base_anim_path)
    new_world = retarget.gf_world_bind()
    id_map = {i: retarget.JOINT_ID[name] for i, name in enumerate(OLD_JOINT_ORDER)}

    # per old joint: inverse base rotation, base position, new bind position
    inv_rot = [q_conj(old_rot[n]) for n in OLD_JOINT_ORDER]
    base_pos = [old_pos[n] for n in OLD_JOINT_ORDER]
    bind_pos = [new_world[n] for n in OLD_JOINT_ORDER]

    out = []
    # process per GEOMOBJECT chunk so vertex/colour indices stay scoped
    chunks = text.split("*GEOMOBJECT")
    out.append(chunks[0])
    for chunk in chunks[1:]:
        lines = chunk.split("\n")
        verts = {}
        cols = {}
        faces = {}
        cfaces = {}
        for line in lines:
            m = VERTEX_RE.match(line)
            if m:
                verts[int(m.group(2))] = tuple(float(m.group(i)) for i in (3, 4, 5))
                continue
            m = VERTCOL_RE.match(line)
            if m:
                cols[int(m.group(2))] = tuple(float(m.group(i)) for i in (3, 4, 5))
                continue
            m = FACE_RE.match(line)
            if m:
                faces[int(m.group(1))] = tuple(int(m.group(i)) for i in (2, 3, 4))
                continue
            m = CFACE_RE.match(line)
            if m:
                cfaces[int(m.group(1))] = tuple(int(m.group(i)) for i in (2, 3, 4))

        # vertex -> weights, via the FACE/CFACE pairing (exactly how
        # gamedefines.cpp GetVertexColors relates the two index spaces)
        vertex_weights = {}
        for fi, tri in faces.items():
            if fi not in cfaces:
                continue
            for vi, ci in zip(tri, cfaces[fi]):
                if vi not in vertex_weights and ci in cols:
                    vertex_weights[vi] = decode_weights(cols[ci])

        def new_vertex(vi):
            v = verts[vi]
            weights = vertex_weights.get(vi, [(0, 1.0)])
            acc = [0.0, 0.0, 0.0]
            for j, w in weights:
                local = q_rot(inv_rot[j], tuple(a - b for a, b in zip(v, base_pos[j])))
                for c in range(3):
                    acc[c] += w * (local[c] + bind_pos[j][c])
            return tuple(acc)

        def blended_rotate(vi, n):
            weights = vertex_weights.get(vi, [(0, 1.0)])
            acc = [0.0, 0.0, 0.0]
            for j, w in weights:
                r = q_rot(inv_rot[j], n)
                for c in range(3):
                    acc[c] += w * r[c]
            l = math.sqrt(sum(c * c for c in acc))
            return tuple(c / l for c in acc) if l > 1e-9 else n

        face_of_normals = [-1]  # MESH_FACENORMAL precedes its VERTEXNORMALs

        new_lines = []
        for line in lines:
            m = VERTEX_RE.match(line)
            if m:
                vi = int(m.group(2))
                x, y, z = new_vertex(vi)
                new_lines.append("%s%d\t%.6f\t%.6f\t%.6f" % (m.group(1), vi, x, y, z))
                continue
            m = VERTCOL_RE.match(line)
            if m:
                ci = int(m.group(2))
                weights = decode_weights(cols[ci])
                remapped = [(id_map[j], w) for j, w in weights]
                r, g, b = encode_color(remapped)
                new_lines.append("%s%d\t%.3f\t%.3f\t%.3f" % (m.group(1), ci, r, g, b))
                continue
            m = FNORMAL_RE.match(line)
            if m:
                fi = int(m.group(2))
                face_of_normals[0] = fi
                vi = faces.get(fi, (0,))[0]
                n = tuple(float(m.group(i)) for i in (3, 4, 5))
                x, y, z = blended_rotate(vi, n)
                new_lines.append("%s%d\t%.4f\t%.4f\t%.4f" % (m.group(1), fi, x, y, z))
                continue
            m = VNORMAL_RE.match(line)
            if m:
                vi = int(m.group(2))
                n = tuple(float(m.group(i)) for i in (3, 4, 5))
                if vi in verts:
                    x, y, z = blended_rotate(vi, n)
                else:
                    x, y, z = n
                new_lines.append("%s%d\t%.4f\t%.4f\t%.4f" % (m.group(1), vi, x, y, z))
                continue
            new_lines.append(line)
        out.append("\n".join(new_lines))
    return "*GEOMOBJECT".join(out)


if __name__ == "__main__":
    import os
    parser = argparse.ArgumentParser()
    parser.add_argument("ase")
    parser.add_argument("-o", "--out", default=None)
    parser.add_argument("--base-anim", default=None,
                        help="the LEGACY base.anim.util (authoring pose)")
    args = parser.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    base = args.base_anim or os.path.join(
        here, "..", "..", "data", "media", "animations", "base.anim.util")
    text = migrate(open(args.ase).read(), base)
    open(args.out or args.ase, "w").write(text)
    print("migrated %s -> %s" % (args.ase, args.out or args.ase))

"""Converts a skinned PES player .fmdl into GameplayFootball's fullbody
morph format: one "fullbody" GEOMOBJECT whose VERTEX COLORS carry the skin
weights the engine's PrepareFullbodyModel expects.

Encoding (from humanoidbase.cpp): each color channel holds one bone
influence as jointID*10 + weight*9 (0..255 scale, ASE stores /255); up to
three influences per vertex. Joint IDs are the player.object DFS order:

  0 body, 1 middle, 2 neck, 3 head, 4 left_shoulder, 5 left_elbow,
  6 left_hand, 7 right_shoulder, 8 right_elbow, 9 right_hand,
  10 left_thigh, 11 left_knee, 12 left_ankle, 13 right_thigh,
  14 right_knee, 15 right_ankle

PES bones map to joints through retarget.GF_FROM_PES. The result pairs with
a kit texture converted from the model's own ftex set.

  python3 fmdl_to_fullbody.py model.fmdl out_dir --fmdl-lib <pes-fmdl dir>
                              [--texture kit.png] [--base stock_fullbody.ase]

--base composites the imported mesh OVER the stock body: many aesthetic
exports (HDG armor) are plate sets that rely on PES's invisible-kit trick,
so they need the stock skinned body underneath. The stock ase's materials,
geometry and vertex colors are carried over verbatim.
"""

import argparse
import math
import os
import sys

import ase_util
import retarget

GF_JOINT_ORDER = ["body", "middle", "neck", "head",
                  "left_shoulder", "left_elbow", "left_hand",
                  "right_shoulder", "right_elbow", "right_hand",
                  "left_thigh", "left_knee", "left_ankle",
                  "right_thigh", "right_knee", "right_ankle"]
JOINT_ID = {name: i for i, name in enumerate(GF_JOINT_ORDER)}

# how far a vertex may sit from the joint that drives it before the bind is
# treated as trailing geometry and re-anchored (metres, GF scale)
FAR_BIND_METRES = 0.55

# each GF joint's PES anchor bone and the bone whose bind position defines
# the limb direction (None = axial joint, translation-only)
JOINT_PES_BONES = {
    "body": ("dsk_hip", None),
    "middle": ("sk_belly", None),
    "neck": ("sk_neck", None),
    "head": ("sk_head", None),
    "left_shoulder": ("sk_upperarm_l", "sk_forearm_l"),
    "left_elbow": ("sk_forearm_l", "sk_hand_l"),
    "left_hand": ("sk_hand_l", None),
    "right_shoulder": ("sk_upperarm_r", "sk_forearm_r"),
    "right_elbow": ("sk_forearm_r", "sk_hand_r"),
    "right_hand": ("sk_hand_r", None),
    "left_thigh": ("sk_thigh_l", "sk_leg_l"),
    "left_knee": ("sk_leg_l", "sk_foot_l"),
    "left_ankle": ("sk_foot_l", None),
    "right_thigh": ("sk_thigh_r", "sk_leg_r"),
    "right_knee": ("sk_leg_r", "sk_foot_r"),
    "right_ankle": ("sk_foot_r", None),
}
# leaf joints inherit their parent limb's rotation so extremities stay attached
JOINT_ROTATION_PARENT = {
    "left_hand": "left_elbow", "right_hand": "right_elbow",
    "left_ankle": "left_knee", "right_ankle": "right_knee",
}


def _gf_world_positions():
    """GF joint name -> world bind position (accumulated player.object offsets)."""
    out = {}
    for name in GF_JOINT_ORDER:
        offset, parent = retarget.GF_BIND[name]
        if parent is None:
            out[name] = offset
        else:
            p = out[parent]
            out[name] = (p[0] + offset[0], p[1] + offset[1], p[2] + offset[2])
    return out


def _rotation_between(a, b):
    """3x3 rotation matrix taking unit vector a to unit vector b."""
    import math
    ax, ay, az = a
    bx, by, bz = b
    cx, cy, cz = (ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx)
    d = ax * bx + ay * by + az * bz
    s = math.sqrt(cx * cx + cy * cy + cz * cz)
    if s < 1e-8:
        if d > 0:
            return ((1, 0, 0), (0, 1, 0), (0, 0, 1))
        return ((-1, 0, 0), (0, -1, 0), (0, 0, 1))  # opposite: flip
    # Rodrigues
    kx, ky, kz = cx / s, cy / s, cz / s
    c = d
    v = 1 - c
    return ((c + kx * kx * v, kx * ky * v - kz * s, kx * kz * v + ky * s),
            (ky * kx * v + kz * s, c + ky * ky * v, ky * kz * v - kx * s),
            (kz * kx * v - ky * s, kz * ky * v + kx * s, c + kz * kz * v))


def _mat_vec(m, v):
    return (m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2])


def build_bind_alignment(fmdl, normalize_proportions=True):
    """Per GF joint: (pes pivot GF-coords, rotation matrix, scale, gf pivot).

    Rotates each limb from the MODEL'S OWN bind pose onto GF's bind
    directions and re-anchors it at the GF joint, so PrepareFullbodyModel's
    joint-relative offsets are computed in matching poses. Falls back to
    the standard PES bind for bones the model does not carry.
    """
    import math
    model_bones = {}
    for bone in fmdl.bones:
        g = bone.globalPosition
        model_bones[bone.name] = (g.x, -g.z, g.y)          # fox -> GF

    def pes_pos(bone_name):
        if bone_name in model_bones:
            return model_bones[bone_name]
        std = retarget.PES_BIND.get(bone_name)
        if std:
            p = std[0]
            return (p[0], -p[2], p[1])
        return (0.0, 0.0, 1.0)

    gf_pos = _gf_world_positions()
    transforms = {}
    for joint, (bone, direction_child) in JOINT_PES_BONES.items():
        pivot_pes = pes_pos(bone)
        pivot_gf = gf_pos[joint]
        rotation = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
        scale = 1.0
        if direction_child:
            child_pes = pes_pos(direction_child)
            d_pes = tuple(c - p for c, p in zip(child_pes, pivot_pes))
            len_pes = math.sqrt(sum(c * c for c in d_pes))
            # GF direction: the child joint's offset in player.object
            gf_children = {"left_shoulder": "left_elbow",
                           "right_shoulder": "right_elbow",
                           "left_elbow": "left_hand",
                           "right_elbow": "right_hand",
                           "left_thigh": "left_knee",
                           "right_thigh": "right_knee",
                           "left_knee": "left_ankle",
                           "right_knee": "right_ankle"}
            gf_child = gf_children[joint]
            d_gf = tuple(c - p for c, p in zip(gf_pos[gf_child], pivot_gf))
            len_gf = math.sqrt(sum(c * c for c in d_gf))
            if len_pes > 1e-6 and len_gf > 1e-6:
                rotation = _rotation_between(
                    tuple(c / len_pes for c in d_pes),
                    tuple(c / len_gf for c in d_gf))
                if normalize_proportions:
                    # clamped: chibi/stylized rigs have tiny limbs, and an
                    # unclamped ratio balloons them into giants
                    scale = max(0.5, min(2.0, len_gf / len_pes))
        transforms[joint] = (pivot_pes, rotation, scale, pivot_gf)

    # leaves ride their parent limb's rotation/scale, anchored at their own pivots
    for leaf, parent in JOINT_ROTATION_PARENT.items():
        pivot_pes, _, _, _ = transforms[leaf]
        _, rotation, scale, _ = transforms[parent]
        transforms[leaf] = (pivot_pes, rotation, scale, gf_pos[leaf])
    return transforms


def align_vertex(pos_gf, joints, transforms):
    """Blend the per-joint bind alignments for one vertex ((x,y,z) GF coords)."""
    out = [0.0, 0.0, 0.0]
    total = 0.0
    for joint_id, weight in joints:
        joint = GF_JOINT_ORDER[joint_id]
        pivot_pes, rotation, scale, pivot_gf = transforms[joint]
        local = tuple((p - q) * scale for p, q in zip(pos_gf, pivot_pes))
        rotated = _mat_vec(rotation, local)
        for c in range(3):
            out[c] += weight * (rotated[c] + pivot_gf[c])
        total += weight
    if total <= 0:
        return pos_gf
    return (out[0] / total, out[1] / total, out[2] / total)


def vertex_skin_joints(vertex, joints, joint_positions):
    """Runtime skin weights for a vertex, given its bind-pose joints.

    PES bends long trailing geometry (hair down the back, capes, skirts) with
    extra bones GF does not have, so those vertices arrive bound a metre from
    the joint that drives them and flail when it turns. Their skin weight is
    moved to the joint they actually sit on; the bind alignment is left alone,
    so the mesh keeps its shape.
    """
    if not joint_positions:
        return joints
    p = vertex.position
    position = (p.x, -p.z, p.y)          # fox -> GF
    dominant = GF_JOINT_ORDER[joints[0][0]]
    if math.dist(position, joint_positions[dominant]) <= FAR_BIND_METRES:
        return joints
    return nearest_joints(position, joint_positions, count=1)


def nearest_joints(position, joint_positions, count=3, falloff=0.35):
    """-> [(jointID, weight)] for geometry that carries no skin weights.

    4cc exports ship hats, hair, capes and skirts as rigid props with an empty
    bone mapping. Welding those to one fixed joint drags them across the body
    (the "floating head" look); binding them to the joints they actually sit
    near follows the skeleton instead. Weights fall off with distance, so a
    hat is head-bound while hair down the back blends head into neck.
    """
    ordered = sorted(
        ((name, math.dist(position, pos)) for name, pos in joint_positions.items()),
        key=lambda pair: pair[1])[:count]
    if not ordered:
        return [(JOINT_ID["middle"], 1.0)]
    # inverse-distance blending, softened so the nearest joint dominates
    weights = []
    for name, distance in ordered:
        weights.append((JOINT_ID[name], 1.0 / (falloff + distance) ** 2))
    total = sum(w for _, w in weights)
    return [(j, w / total) for j, w in weights]


def vertex_joints(vertex, pes_to_gf_map, joint_positions=None):
    """-> [(jointID, weight)] top-3, normalized, engine-encodable.

    `joint_positions` (GF joint name -> bind position) lets unweighted
    geometry bind to the joints it sits near instead of a fixed fallback.
    """
    position = None
    if joint_positions:
        p = vertex.position
        position = (p.x, -p.z, p.y)   # fox -> GF, to match the joint bind

    if not vertex.boneMapping:
        if position is not None:
            return nearest_joints(position, joint_positions)
        return [(JOINT_ID["middle"], 1.0)]

    weights = {}
    unmapped = 0.0
    for bone, weight in vertex.boneMapping.items():
        gf_node = pes_to_gf_map.get(bone.name)
        if gf_node is None:
            gf_node = retarget.gf_node_for_bone(bone.name)
        if gf_node is None:
            # a bone outside the body rig (prop bones on hats and tails)
            unmapped += weight
            continue
        joint = JOINT_ID[gf_node]
        weights[joint] = weights.get(joint, 0.0) + weight

    if unmapped > 0.0 and position is not None:
        for joint, weight in nearest_joints(position, joint_positions):
            weights[joint] = weights.get(joint, 0.0) + weight * unmapped
    elif unmapped > 0.0:
        joint = JOINT_ID["middle"]
        weights[joint] = weights.get(joint, 0.0) + unmapped

    top = sorted(weights.items(), key=lambda kv: -kv[1])[:3]
    total = sum(w for _, w in top)
    if total <= 0:
        if position is not None:
            return nearest_joints(position, joint_positions)
        return [(JOINT_ID["middle"], 1.0)]
    top = [(j, w / total) for j, w in top]

    return top


def encode_color(joints):
    """[(jointID, weight)] -> three 0..1 floats (ASE color channels)."""
    channels = []
    for j, w in joints[:3]:
        w = max(0.12, min(1.0, w))          # engine skips <=0.01, asserts >0
        channels.append((j * 10 + w * 9.0) / 255.0)
    while len(channels) < 3:
        channels.append(0.0)
    return channels


def mesh_rebind_joint(mesh, pes_to_gf_map, joint_positions):
    """-> jointID to bind a whole mesh to, or None to keep its own weights.

    PES bends long trailing geometry with extra bones GF does not have, so a
    cape rigged to the head arrives as vertices a metre from their joint. Bound
    per vertex they tear apart; bound as one piece to the joint they sit on,
    they simply follow it.
    """
    positions = []
    far = 0
    for vertex in mesh.vertices:
        p = vertex.position
        pos = (p.x, -p.z, p.y)          # fox -> GF
        positions.append(pos)
        joints = vertex_joints(vertex, pes_to_gf_map)
        dominant = GF_JOINT_ORDER[joints[0][0]]
        if math.dist(pos, joint_positions[dominant]) > FAR_BIND_METRES:
            far += 1
    if not positions or far < len(positions) * 0.5:
        return None                      # normal skinned geometry: leave it
    centre = tuple(sum(c[i] for c in positions) / len(positions) for i in range(3))
    return nearest_joints(centre, joint_positions, count=1)[0][0]


def _mesh_signature(mesh):
    """Duplicate-detection key: 4cc fmdls carry every mesh twice."""
    sig = [len(mesh.faces), len(mesh.vertices)]
    for vertex in mesh.vertices[:64]:
        p = vertex.position
        sig.append((round(p.x, 4), round(p.y, 4), round(p.z, 4)))
    return tuple(sig)


def _mesh_joints(mesh, pes_to_gf_map, joint_positions=None):
    """Set of GF joint IDs a mesh's skin weights reference."""
    joints = set()
    for vertex in mesh.vertices:
        for joint_id, _ in vertex_joints(vertex, pes_to_gf_map, joint_positions):
            joints.add(joint_id)
    return joints


def select_meshes(meshes, max_tris, pes_to_gf_map, joint_positions=None):
    """Dedupe identical meshes, then pick within the triangle budget.

    The old biggest-first fill let one huge decorative mesh exhaust the
    budget and drop the body ("floating hat" players). Selection is now
    coverage-first: greedy set-cover over the GF joints the skin references
    (so every limb keeps geometry), then remaining budget fills biggest-first.
    """
    seen = set()
    unique = []
    for mesh in meshes:
        sig = _mesh_signature(mesh)
        if sig in seen:
            continue
        seen.add(sig)
        unique.append(mesh)
    if not max_tris:
        return unique

    joints_of = {id(m): _mesh_joints(m, pes_to_gf_map, joint_positions) for m in unique}
    kept, used = [], 0
    covered = set()
    remaining = sorted(unique, key=lambda m: -len(m.faces))
    # coverage pass: repeatedly take the mesh adding most uncovered joints
    # (ties: biggest) while it fits the budget
    while True:
        best, best_new = None, 0
        for m in remaining:
            new = len(joints_of[id(m)] - covered)
            if new > best_new and used + len(m.faces) <= max_tris:
                best, best_new = m, new
        if best is None:
            break
        kept.append(best)
        used += len(best.faces)
        covered |= joints_of[id(best)]
        remaining.remove(best)
    # fill pass: biggest remaining meshes that still fit
    for m in remaining:
        if used + len(m.faces) <= max_tris:
            kept.append(m)
            used += len(m.faces)
    # keep the original draw order for deterministic output
    order = {id(m): i for i, m in enumerate(unique)}
    kept.sort(key=lambda m: order[id(m)])
    return kept


MATERIAL_BLOCK = (
    '\t\t*MATERIAL_NAME "fullbody"\n\t\t*MATERIAL_CLASS "Standard"\n'
    "\t\t*MATERIAL_AMBIENT 0.588\t0.588\t0.588\n"
    "\t\t*MATERIAL_DIFFUSE 0.588\t0.588\t0.588\n"
    "\t\t*MATERIAL_SPECULAR 0.900\t0.900\t0.900\n"
    "\t\t*MATERIAL_SHINE 0.100\n\t\t*MATERIAL_SHADING Blinn\n"
    "\t\t*MATERIAL_SHINESTRENGTH 0.0\n"
    "\t\t*MATERIAL_SELFILLUM 0.0\n"
    '\t\t*MAP_DIFFUSE {\n\t\t\t*MAP_NAME "fullbody"\n'
    '\t\t\t*MAP_CLASS "Bitmap"\n'
    '\t\t\t*BITMAP "%(texture)s"\n'
    "\t\t\t*MAP_TYPE Screen\n\t\t}\n")


def convert(fmdl_path, out_dir, fmdl_lib, texture, base_ase=None,
            max_tris=None, align_bind=True, normalize_proportions=True):
    sys.path.insert(0, fmdl_lib)
    import FmdlFile
    fmdl = FmdlFile.FmdlFile()
    fmdl.readFile(fmdl_path)

    pes_to_gf_map = retarget.pes_to_gf()

    vertices = []       # (pos, uv, color)
    faces = []
    index = {}
    # duplicate meshes are dropped, then the triangle budget keeps joint
    # coverage first (a body for every limb) and biggest meshes second
    # bind positions of the GF joints: unweighted costume geometry binds to
    # whichever joints it sits near, rather than to one fixed fallback
    joint_positions = _gf_world_positions()
    meshes = select_meshes(fmdl.meshes, max_tris, pes_to_gf_map, joint_positions)
    transforms = None
    if align_bind:
        transforms = build_bind_alignment(fmdl, normalize_proportions)
    for mesh in meshes:
        for face in mesh.faces:
            tri = []
            for vertex in face.vertices:
                key = id(vertex)
                if key not in index:
                    index[key] = len(vertices)
                    uv = vertex.uv[0] if vertex.uv else None
                    # the bind alignment must use the joint the SOURCE rig
                    # placed the vertex under, or neighbouring vertices bake
                    # through different transforms and the mesh tears. The
                    # runtime skin weight is free to differ: trailing
                    # geometry follows the joint it sits on instead.
                    joints = vertex_joints(vertex, pes_to_gf_map)
                    skin = vertex_skin_joints(vertex, joints, joint_positions)
                    color = encode_color(skin)
                    pos = (vertex.position.x, -vertex.position.z,
                           vertex.position.y)
                    if transforms:
                        pos = align_vertex(pos, joints, transforms)
                    vertices.append((pos, uv, color))
                tri.append(index[key])
            faces.append(tri)

    os.makedirs(out_dir, exist_ok=True)
    # the engine's resource cache keys geometry by BASENAME, so every model
    # needs a unique ase filename or it collides with the stock fullbody.ase
    unique = "fullbody_%s.ase" % os.path.basename(os.path.normpath(out_dir))
    ase_path = os.path.join(out_dir, unique)

    # --base: carry the stock body (materials, geometry, skin colors) over
    # verbatim; the import becomes an extra subgeom with its own material
    material_ref = 0
    base_head = base_geoms = None
    if base_ase:
        import re
        base_text = open(base_ase).read()
        geom_at = base_text.find("*GEOMOBJECT {")
        base_head = base_text[:geom_at]
        base_geoms = base_text[geom_at:]
        count_match = re.search(r"\*MATERIAL_COUNT\s+(\d+)", base_head)
        material_ref = int(count_match.group(1))
        base_head = base_head.replace(
            count_match.group(0), "*MATERIAL_COUNT %d" % (material_ref + 1))
        close_at = base_head.rstrip().rfind("}")
        plate_material = ("\t*MATERIAL %d {\n%s\t}\n"
                          % (material_ref, MATERIAL_BLOCK % {"texture": texture}))
        base_head = base_head[:close_at] + plate_material + base_head[close_at:]

    with open(ase_path, "w") as out:
        if base_ase:
            out.write(base_head)
            out.write(base_geoms)
            if not base_geoms.endswith("\n"):
                out.write("\n")
        else:
            out.write("*3DSMAX_ASCIIEXPORT\t200\n")
            out.write('*COMMENT "PES player -> GF fullbody by tools/pes21_import"\n')
            out.write("*SCENE {\n\t*SCENE_FILENAME \"fullbody\"\n")
            out.write("\t*SCENE_FIRSTFRAME 0\n\t*SCENE_LASTFRAME 100\n")
            out.write("\t*SCENE_FRAMESPEED 30\n\t*SCENE_TICKSPERFRAME 160\n")
            out.write("\t*SCENE_BACKGROUND_STATIC 0.000\t0.000\t0.000\n")
            out.write("\t*SCENE_AMBIENT_STATIC 0.000\t0.000\t0.000\n}\n")
            out.write("*MATERIAL_LIST {\n\t*MATERIAL_COUNT 1\n\t*MATERIAL 0 {\n")
            out.write(MATERIAL_BLOCK % {"texture": texture})
            out.write("\t}\n}\n")

        out.write("*GEOMOBJECT {\n")
        out.write('\t*NODE_NAME "fullbody_import"\n')
        out.write("\t*NODE_TM {\n\t\t*NODE_NAME \"fullbody_import\"\n")
        out.write("\t\t*INHERIT_POS 0 0 0\n\t\t*INHERIT_ROT 0 0 0\n\t\t*INHERIT_SCL 0 0 0\n")
        out.write("\t\t*TM_ROW0 1.0\t0.0\t0.0\n\t\t*TM_ROW1 0.0\t1.0\t0.0\n")
        out.write("\t\t*TM_ROW2 0.0\t0.0\t1.0\n\t\t*TM_ROW3 0.0\t0.0\t0.0\n\t}\n")
        out.write("\t*MESH {\n")
        out.write("\t\t*MESH_NUMVERTEX %d\n" % len(vertices))
        out.write("\t\t*MESH_NUMFACES %d\n" % len(faces))
        out.write("\t\t*MESH_VERTEX_LIST {\n")
        for i, (pos, _, _) in enumerate(vertices):
            out.write("\t\t\t*MESH_VERTEX %d\t%.6f\t%.6f\t%.6f\n"
                      % (i, pos[0], pos[1], pos[2]))
        out.write("\t\t}\n\t\t*MESH_FACE_LIST {\n")
        for i, (a, b, c) in enumerate(faces):
            out.write("\t\t\t*MESH_FACE %d: A: %d B: %d C: %d AB: 1 BC: 1 CA: 1 "
                      "*MESH_SMOOTHING 1 *MESH_MTLID 0\n" % (i, a, b, c))
        out.write("\t\t}\n")
        out.write("\t\t*MESH_NUMTVERTEX %d\n" % len(vertices))
        out.write("\t\t*MESH_TVERTLIST {\n")
        for i, (_, uv, _) in enumerate(vertices):
            u, v = (uv.u, 1.0 - uv.v) if uv is not None else (0.0, 0.0)
            out.write("\t\t\t*MESH_TVERT %d\t%.6f\t%.6f\t0.0\n" % (i, u, v))
        out.write("\t\t}\n")
        out.write("\t\t*MESH_NUMTVFACES %d\n" % len(faces))
        out.write("\t\t*MESH_TFACELIST {\n")
        for i, (a, b, c) in enumerate(faces):
            out.write("\t\t\t*MESH_TFACE %d\t%d\t%d\t%d\n" % (i, a, b, c))
        out.write("\t\t}\n")
        out.write("\t\t*MESH_NUMCVERTEX %d\n" % len(vertices))
        out.write("\t\t*MESH_CVERTLIST {\n")
        for i, (_, _, color) in enumerate(vertices):
            out.write("\t\t\t*MESH_VERTCOL %d\t%.3f\t%.3f\t%.3f\n"
                      % (i, color[0], color[1], color[2]))
        out.write("\t\t}\n")
        out.write("\t\t*MESH_NUMCVFACES %d\n" % len(faces))
        out.write("\t\t*MESH_CFACELIST {\n")
        for i, (a, b, c) in enumerate(faces):
            out.write("\t\t\t*MESH_CFACE %d\t%d\t%d\t%d\n" % (i, a, b, c))
        out.write("\t\t}\n")
        gf_verts = [pos for (pos, _, _) in vertices]
        ase_util.write_mesh_normals(out, gf_verts, faces, smooth=True)
        out.write("\t}\n")
        out.write("\t*PROP_MOTIONBLUR 0\n\t*PROP_CASTSHADOW 1\n")
        out.write("\t*PROP_RECVSHADOW 1\n\t*MATERIAL_REF %d\n}\n" % material_ref)

    object_path = os.path.join(out_dir, "fullbody.object")
    open(object_path, "w").write(
        "<object>\n\n\t<geometry>\n"
        "\t\t<filename>%s</filename>\n"
        "\t\t<name>fullbody</name>\n"
        "\t\t<position>0, 0, 0</position>\n"
        "\t\t<rotation>0, 0, 0, 0</rotation>\n"
        "\t</geometry>\n\n</object>\n" % unique)
    return len(vertices), len(faces)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("fmdl")
    parser.add_argument("out_dir")
    parser.add_argument("--fmdl-lib", required=True)
    parser.add_argument("--texture",
                        default="media/objects/players/textures/kit_template.png")
    parser.add_argument("--base", default=None,
                        help="stock fullbody.ase to composite the import over")
    parser.add_argument("--max-tris", type=int, default=None,
                        help="triangle budget (biggest meshes kept first)")
    parser.add_argument("--no-align", action="store_true",
                        help="skip bind-pose alignment onto GF joints")
    parser.add_argument("--keep-proportions", action="store_true",
                        help="do not normalize limb lengths to GF's skeleton")
    args = parser.parse_args()
    verts, faces = convert(args.fmdl, args.out_dir, args.fmdl_lib, args.texture,
                           args.base, args.max_tris,
                           align_bind=not args.no_align,
                           normalize_proportions=not args.keep_proportions)
    print("wrote fullbody (%d imported vertices, %d faces%s)" %
          (verts, faces, ", composited over base" if args.base else ""))

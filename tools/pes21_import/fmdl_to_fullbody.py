"""Converts a skinned PES player .fmdl into GameplayFootball's fullbody
format: one "fullbody" GEOMOBJECT whose VERTEX COLORS carry the skin
weights the engine's PrepareFullbodyModel expects.

Since the native-rig migration this is a change of basis, not a retarget:
the engine's skeleton IS the PES animated rig (retarget.GF_NODES), player
fmdls are authored at that very bind pose, and the engine captures its bind
in the same pose (base.anim.util is identity). So vertices map Fox->GF
coordinates ((x, y, z) -> (x, -z, y)) and weights resolve bone->joint
through retarget.resolve_bone: animated bones 1:1, helper bones (dsk_*
twists, skh_* fingers, cloth) onto the animated bone they rigidly follow -
lossless under the engine's inverse-bind skinning.

Encoding (from humanoidbase.cpp): each color channel holds one bone
influence as jointID*10 + weight*9 (0..255 scale, ASE stores /255); up to
three influences per vertex. PES skins with up to four; the three
strongest are kept and renormalized (the only approximation in this
pipeline, and a standard one).

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

GF_JOINT_ORDER = list(retarget.GF_JOINT_ORDER)
JOINT_ID = dict(retarget.JOINT_ID)


def fox_to_gf(p):
    return (p.x, -p.z, p.y)


def nearest_joints(position, joint_positions, count=3, falloff=0.35):
    """-> [(jointID, weight)] for geometry that carries no skin weights.

    4cc exports ship hats, hair, capes and skirts as rigid props with an
    empty bone mapping. Binding them to the joints they sit near follows the
    skeleton; weights fall off with distance, so a hat is head-bound while
    hair down the back blends head into neck.
    """
    ordered = sorted(
        ((name, math.dist(position, pos)) for name, pos in joint_positions.items()),
        key=lambda pair: pair[1])[:count]
    if not ordered:
        return [(JOINT_ID["middle"], 1.0)]
    weights = []
    for name, distance in ordered:
        weights.append((JOINT_ID[name], 1.0 / (falloff + distance) ** 2))
    total = sum(w for _, w in weights)
    return [(j, w / total) for j, w in weights]


def vertex_joints(vertex, bone_to_joint, joint_positions=None):
    """-> [(jointID, weight)] top-3, normalized, engine-encodable."""
    position = fox_to_gf(vertex.position) if joint_positions else None

    if not vertex.boneMapping:
        if position is not None:
            return nearest_joints(position, joint_positions)
        return [(JOINT_ID["middle"], 1.0)]

    weights = {}
    unmapped = 0.0
    for bone, weight in vertex.boneMapping.items():
        joint = bone_to_joint.get(bone.name)
        if joint is None:
            unmapped += weight
            continue
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
    return [(j, w / total) for j, w in top]


def encode_color(joints):
    """[(jointID, weight)] -> three 0..1 floats (ASE color channels)."""
    channels = []
    for j, w in joints[:3]:
        w = max(0.12, min(1.0, w))          # engine skips <=0.01, asserts >0
        channels.append((j * 10 + w * 9.0) / 255.0)
    while len(channels) < 3:
        channels.append(0.0)
    return channels


def build_bone_map(fmdl):
    """fmdl bone table -> {bone name: GF joint id} via retarget.resolve_bone."""
    positions = {}
    for bone in fmdl.bones:
        g = bone.globalPosition
        positions[bone.name] = (g.x, g.y, g.z)
    out = {}
    for bone in fmdl.bones:
        node = retarget.resolve_bone(bone.name, positions)
        if node is not None:
            out[bone.name] = JOINT_ID[node]
    return out


def _mesh_signature(mesh):
    """Duplicate-detection key: 4cc fmdls carry every mesh twice."""
    sig = [len(mesh.faces), len(mesh.vertices)]
    for vertex in mesh.vertices[:64]:
        p = vertex.position
        sig.append((round(p.x, 4), round(p.y, 4), round(p.z, 4)))
    return tuple(sig)


def _mesh_joints(mesh, bone_to_joint, joint_positions=None):
    """Set of GF joint IDs a mesh's skin weights reference."""
    joints = set()
    for vertex in mesh.vertices:
        for joint_id, _ in vertex_joints(vertex, bone_to_joint, joint_positions):
            joints.add(joint_id)
    return joints


def select_meshes(meshes, max_tris, bone_to_joint, joint_positions=None):
    """Dedupe identical meshes, then pick within the triangle budget.

    Coverage-first: greedy set-cover over the GF joints the skin references
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

    joints_of = {id(m): _mesh_joints(m, bone_to_joint, joint_positions) for m in unique}
    kept, used = [], 0
    covered = set()
    remaining = sorted(unique, key=lambda m: -len(m.faces))
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
    for m in remaining:
        if used + len(m.faces) <= max_tris:
            kept.append(m)
            used += len(m.faces)
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
            max_tris=None, only_meshes=None, force_joint=None, max_edge=0.0):
    sys.path.insert(0, fmdl_lib)
    import FmdlFile
    fmdl = FmdlFile.FmdlFile()
    fmdl.readFile(fmdl_path)

    bone_to_joint = build_bone_map(fmdl)
    joint_positions = retarget.gf_world_bind()

    vertices = []       # (pos, uv, color)
    faces = []
    index = {}
    meshes = select_meshes(fmdl.meshes, max_tris, bone_to_joint, joint_positions)
    if only_meshes is not None:
        meshes = [m for i, m in enumerate(meshes) if i in only_meshes]
    for mesh in meshes:
        for face in mesh.faces:
            tri = []
            for vertex in face.vertices:
                key = id(vertex)
                if key not in index:
                    index[key] = len(vertices)
                    uv = vertex.uv[0] if vertex.uv else None
                    skin = ([(force_joint, 1.0)] if force_joint is not None
                            else vertex_joints(vertex, bone_to_joint,
                                               joint_positions))
                    pos = fox_to_gf(vertex.position)
                    color = encode_color(skin)
                    vertices.append((pos, uv, color))
                tri.append(index[key])
            # Fox winds clockwise-front (D3D); GF culls GL-style, so reverse.
            # (4cc exports double every mesh so they hid this; Konami's
            # single-sided originals do not.)
            faces.append((tri[0], tri[2], tri[1]))

    if max_edge > 0.0:
        kept = []
        for tri in faces:
            a, b, c = (vertices[i][0] for i in tri)
            if max(math.dist(a, b), math.dist(b, c), math.dist(c, a)) > max_edge:
                continue
            kept.append(tri)
        if len(kept) != len(faces):
            print("dropped %d stretched triangles (edge > %.2fm)"
                  % (len(faces) - len(kept), max_edge))
        faces = kept

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
                        help="triangle budget (joint coverage first)")
    parser.add_argument("--force-joint", type=int, default=None,
                        help="debug: bind every vertex to this joint id")
    parser.add_argument("--only-meshes", default="",
                        help="comma-separated mesh indices to keep (after dedupe)")
    parser.add_argument("--max-edge", type=float, default=0.15,
                        help="drop triangles with an edge longer than this "
                             "(metres, 0 disables). On by default: a source "
                             "mesh routinely carries a few triangles joining "
                             "far-apart vertices, and on a 1.8 m body they "
                             "render as metre-long shards. A real body "
                             "triangle is centimetres; the median is under 2 cm.")
    args = parser.parse_args()
    verts, faces = convert(args.fmdl, args.out_dir, args.fmdl_lib, args.texture,
                           args.base, args.max_tris,
                           only_meshes=({int(x) for x in args.only_meshes.split(",") if x.strip()}
                                        if args.only_meshes else None),
                           force_joint=args.force_joint,
                           max_edge=args.max_edge)
    print("wrote fullbody (%d imported vertices, %d faces%s)" %
          (verts, faces, ", composited over base" if args.base else ""))

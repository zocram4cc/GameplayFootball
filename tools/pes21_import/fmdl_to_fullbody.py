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


def vertex_joints(vertex, pes_to_gf_map):
    """-> [(jointID, weight)] top-3, normalized, engine-encodable."""
    weights = {}
    if not vertex.boneMapping:
        return [(JOINT_ID["middle"], 1.0)]
    for bone, weight in vertex.boneMapping.items():
        gf_node = pes_to_gf_map.get(bone.name)
        if gf_node is None:
            gf_node = retarget.gf_node_for_bone(bone.name) or "middle"
        joint = JOINT_ID[gf_node]
        weights[joint] = weights.get(joint, 0.0) + weight
    top = sorted(weights.items(), key=lambda kv: -kv[1])[:3]
    total = sum(w for _, w in top)
    if total <= 0:
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
            max_tris=None):
    sys.path.insert(0, fmdl_lib)
    import FmdlFile
    fmdl = FmdlFile.FmdlFile()
    fmdl.readFile(fmdl_path)

    pes_to_gf_map = retarget.pes_to_gf()

    vertices = []       # (pos, uv, color)
    faces = []
    index = {}
    # keep the biggest meshes within the triangle budget: the engine's
    # fullbody preparation is quadratic in vertex count, so huge imports
    # must be capped (decorative meshes drop first)
    meshes = sorted(fmdl.meshes, key=lambda m: -len(m.faces))
    if max_tris:
        kept, used = [], 0
        for m in meshes:
            if used + len(m.faces) <= max_tris:
                kept.append(m)
                used += len(m.faces)
        meshes = kept
    for mesh in meshes:
        for face in mesh.faces:
            tri = []
            for vertex in face.vertices:
                key = id(vertex)
                if key not in index:
                    index[key] = len(vertices)
                    uv = vertex.uv[0] if vertex.uv else None
                    color = encode_color(vertex_joints(vertex, pes_to_gf_map))
                    vertices.append((vertex.position, uv, color))
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
                      % (i, pos.x, -pos.z, pos.y))
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
        gf_verts = [(pos.x, -pos.z, pos.y) for (pos, _, _) in vertices]
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
    args = parser.parse_args()
    verts, faces = convert(args.fmdl, args.out_dir, args.fmdl_lib, args.texture,
                           args.base, args.max_tris)
    print("wrote fullbody (%d imported vertices, %d faces%s)" %
          (verts, faces, ", composited over base" if args.base else ""))

"""Converts a PES .fmdl (Fox Engine model) into GameplayFootball's ASE format.

The fmdl is parsed with the 4cc community's FmdlFile module (point --fmdl-lib at
the pes-fmdl Blender addon directory; it runs fine outside Blender). The skinned
mesh is segmented by each vertex's dominant bone through the retarget map, and
every GameplayFootball body part becomes one GEOMOBJECT in a single .ase, which
is exactly how the stock fullbody.ase is laid out.

Textures are not handled here: pair the .ase with a kit texture or convert the
model's .dds set separately (see package_assets.py).
"""

import argparse
import os
import sys

import ase_util
import retarget


ASE_HEADER = """*3DSMAX_ASCIIEXPORT\t200
*COMMENT "converted from PES .fmdl by tools/pes21_import"
*SCENE {
\t*SCENE_FILENAME "import"
\t*SCENE_FIRSTFRAME 0
\t*SCENE_LASTFRAME 100
\t*SCENE_FRAMESPEED 30
\t*SCENE_TICKSPERFRAME 160
\t*SCENE_BACKGROUND_STATIC 0.000\t0.000\t0.000
\t*SCENE_AMBIENT_STATIC 0.000\t0.000\t0.000
}
*MATERIAL_LIST {
\t*MATERIAL_COUNT 1
\t*MATERIAL 0 {
\t\t*MATERIAL_NAME "imported"
\t\t*MATERIAL_CLASS "Standard"
\t\t*MATERIAL_AMBIENT 0.588\t0.588\t0.588
\t\t*MATERIAL_DIFFUSE 0.588\t0.588\t0.588
\t\t*MATERIAL_SPECULAR 0.900\t0.900\t0.900
\t\t*MATERIAL_SHINE 0.100
\t\t*MATERIAL_SHINESTRENGTH 0.0
\t\t*MATERIAL_SELFILLUM 0.0
\t\t*MATERIAL_SHADING Blinn
\t\t*MAP_DIFFUSE {
\t\t\t*MAP_NAME "imported"
\t\t\t*MAP_CLASS "Bitmap"
\t\t\t*BITMAP "%(texture)s"
\t\t\t*MAP_TYPE Screen
\t\t}
\t}
}
"""


def dominant_bone(vertex):
    """Name of the bone with the highest skin weight on this vertex."""
    best = None
    best_weight = -1.0
    if vertex.boneMapping is None:
        return None
    for bone, weight in vertex.boneMapping.items():
        if weight > best_weight:
            best_weight = weight
            best = bone
    return best.name if best is not None else None


def segment_meshes(fmdl):
    """GF geomobject name -> list of (vertex, uv) & faces, per dominant bone."""
    segments = {}
    for mesh in fmdl.meshes:
        for face in mesh.faces:
            # a face belongs where the majority of its corners belong
            votes = {}
            for vertex in face.vertices:
                bone = dominant_bone(vertex)
                gf_node = retarget.gf_node_for_bone(bone) if bone else None
                votes[gf_node] = votes.get(gf_node, 0) + 1
            gf_node = max(votes, key=votes.get)
            if gf_node is None:
                continue
            geom = retarget.GF_GEOMOBJECT[gf_node]
            segments.setdefault(geom, []).append(face)
    return segments


def write_geomobject(out, name, faces):
    # collect unique vertices for this segment
    vertex_index = {}
    vertices = []
    uvs = []
    for face in faces:
        for vertex in face.vertices:
            key = id(vertex)
            if key not in vertex_index:
                vertex_index[key] = len(vertices)
                vertices.append(vertex.position)
                uv = vertex.uv[0] if vertex.uv else None
                uvs.append(uv)

    out.write("*GEOMOBJECT {\n")
    out.write('\t*NODE_NAME "%s"\n' % name)
    out.write("\t*NODE_TM {\n")
    out.write('\t\t*NODE_NAME "%s"\n' % name)
    out.write("\t\t*INHERIT_POS 0 0 0\n\t\t*INHERIT_ROT 0 0 0\n\t\t*INHERIT_SCL 0 0 0\n")
    out.write("\t\t*TM_ROW0 1.0\t0.0\t0.0\n")
    out.write("\t\t*TM_ROW1 0.0\t1.0\t0.0\n")
    out.write("\t\t*TM_ROW2 0.0\t0.0\t1.0\n")
    out.write("\t\t*TM_ROW3 0.0\t0.0\t0.0\n")
    out.write("\t}\n")
    out.write("\t*MESH {\n")
    out.write("\t\t*MESH_NUMVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_NUMFACES %d\n" % len(faces))
    out.write("\t\t*MESH_VERTEX_LIST {\n")
    for i, pos in enumerate(vertices):
        # Fox is Y-up right-handed; GameplayFootball's ASE convention is Z-up:
        # (x, y, z) -> (x, -z, y)
        out.write("\t\t\t*MESH_VERTEX %d\t%.6f\t%.6f\t%.6f\n" %
                  (i, pos.x, -pos.z, pos.y))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_FACE_LIST {\n")
    for i, face in enumerate(faces):
        a, b, c = [vertex_index[id(v)] for v in face.vertices]
        out.write("\t\t\t*MESH_FACE %d: A: %d B: %d C: %d "
                  "AB: 1 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID 0\n" %
                  (i, a, b, c))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMTVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_TVERTLIST {\n")
    for i, uv in enumerate(uvs):
        u, v = (uv.u, 1.0 - uv.v) if uv is not None else (0.0, 0.0)
        out.write("\t\t\t*MESH_TVERT %d\t%.6f\t%.6f\t0.0\n" % (i, u, v))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMTVFACES %d\n" % len(faces))
    out.write("\t\t*MESH_TFACELIST {\n")
    for i, face in enumerate(faces):
        a, b, c = [vertex_index[id(v)] for v in face.vertices]
        out.write("\t\t\t*MESH_TFACE %d\t%d\t%d\t%d\n" % (i, a, b, c))
    out.write("\t\t}\n")
    gf_verts = [(pos.x, -pos.z, pos.y) for pos in vertices]
    tri_faces = [tuple(vertex_index[id(v)] for v in face.vertices)
                 for face in faces]
    ase_util.write_mesh_normals(out, gf_verts, tri_faces, smooth=True)
    out.write("\t}\n")
    out.write("\t*PROP_MOTIONBLUR 0\n")
    out.write("\t*PROP_CASTSHADOW 1\n")
    out.write("\t*PROP_RECVSHADOW 1\n")
    out.write("\t*MATERIAL_REF 0\n")
    out.write("}\n")


def convert(fmdl_path, ase_path, fmdl_lib, texture):
    sys.path.insert(0, fmdl_lib)
    import FmdlFile  # noqa: E402  (community parser, runs outside Blender)

    fmdl = FmdlFile.FmdlFile()
    fmdl.readFile(fmdl_path)

    segments = segment_meshes(fmdl)
    with open(ase_path, "w") as out:
        out.write(ASE_HEADER % {"texture": texture})
        for name, faces in sorted(segments.items()):
            write_geomobject(out, name, faces)
    return {name: len(faces) for name, faces in segments.items()}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("fmdl")
    parser.add_argument("ase")
    parser.add_argument("--fmdl-lib", required=True,
                        help="path to the pes-fmdl Blender addon directory")
    parser.add_argument("--texture", default="media/objects/players/textures/kit_template.png")
    args = parser.parse_args()

    counts = convert(args.fmdl, args.ase, args.fmdl_lib, args.texture)
    total = sum(counts.values())
    print("wrote %s: %d faces across %d body parts" %
          (args.ase, total, len(counts)))
    for name, count in sorted(counts.items()):
        print("  %-16s %d" % (name, count))

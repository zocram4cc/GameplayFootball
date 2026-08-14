"""Converts a PES stadium scene (.fmdl inside the stadium fpk) into a
GameplayFootball stadium: a multi-material ASE + PNG textures + an .object.

PES stadium fmdls are static (no bones); every fmdl mesh becomes one
GEOMOBJECT with its own material, so the engine's per-geometry culling
works. Fox coords (Y up, +Z forward, metres) map to GF (Z up) as
(x, y, z) -> (x, -z, y).

The generated .object references the converted shell plus GameplayFootball's
own pitch surface (the PES pitch is a separate subsystem and GF draws its
own grass).

  python3 stadium_to_gf.py <scene.fmdl> <out_dir> \
      --fmdl-lib <pes-fmdl dir> --textures <sourceimages/#windx11 dir> \
      [--name pes_st060] [--extra plane.fmdl ...]
"""

import argparse
import os
import sys

import ase_util
import ftex


def _load_fmdl(path, fmdl_lib):
    if fmdl_lib and fmdl_lib not in sys.path:
        sys.path.insert(0, fmdl_lib)
    import FmdlFile
    f = FmdlFile.FmdlFile()
    f.readFile(path)
    return f


def _texture_png(texture, tex_dirs, out_dir, converted):
    """Converts the mesh's base ftex to png; returns the bitmap path."""
    base = os.path.splitext(os.path.basename(texture.filename))[0]
    if base in converted:
        return converted[base]
    png_rel = "textures/%s.png" % base
    out_path = os.path.join(out_dir, png_rel)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    for tex_dir in tex_dirs:
        for candidate in (base + ".ftex", os.path.basename(texture.filename)):
            src = os.path.join(tex_dir, candidate)
            if os.path.isfile(src):
                try:
                    ftex.convert(src, out_path)
                    converted[base] = png_rel
                    return png_rel
                except Exception:
                    pass
    converted[base] = None
    return None


def _mesh_base_texture(mesh):
    textures = mesh.materialInstance.textures
    items = textures.items() if hasattr(textures, "items") else list(textures)
    for role, tex in items:
        if "Base_Tex" in role:
            return tex
    for role, tex in items:
        return tex
    return None


def write_ase(fmdls, out_dir, name, tex_dirs, max_tris=None):
    converted = {}
    materials = []          # (material name, bitmap path or None)
    geoms = []              # (geom name, material index, mesh)

    budget = max_tris if max_tris else float("inf")
    used = 0
    for label, fmdl in fmdls:
        # biggest meshes first so a budget keeps the structural shell and
        # drops decorative detail last-to-first
        order = sorted(range(len(fmdl.meshes)),
                       key=lambda i: -len(fmdl.meshes[i].faces))
        for i in order:
            mesh = fmdl.meshes[i]
            if used + len(mesh.faces) > budget:
                continue
            used += len(mesh.faces)
            tex = _mesh_base_texture(mesh)
            bitmap = _texture_png(tex, tex_dirs, out_dir, converted) if tex else None
            materials.append(("%s_m%d" % (label, i), bitmap))
            geoms.append(("%s_%02d" % (label, i), len(materials) - 1, mesh))

    ase_path = os.path.join(out_dir, name + ".ase")
    with open(ase_path, "w") as out:
        out.write("*3DSMAX_ASCIIEXPORT\t200\n")
        out.write('*COMMENT "converted from PES stadium by tools/pes21_import"\n')
        out.write("*SCENE {\n\t*SCENE_FILENAME \"%s\"\n" % name)
        out.write("\t*SCENE_FIRSTFRAME 0\n\t*SCENE_LASTFRAME 100\n")
        out.write("\t*SCENE_FRAMESPEED 30\n\t*SCENE_TICKSPERFRAME 160\n")
        out.write("\t*SCENE_BACKGROUND_STATIC 0.000\t0.000\t0.000\n")
        out.write("\t*SCENE_AMBIENT_STATIC 0.000\t0.000\t0.000\n}\n")
        out.write("*MATERIAL_LIST {\n")
        out.write("\t*MATERIAL_COUNT %d\n" % len(materials))
        for m, (mat_name, bitmap) in enumerate(materials):
            out.write("\t*MATERIAL %d {\n" % m)
            out.write('\t\t*MATERIAL_NAME "%s"\n' % mat_name)
            out.write('\t\t*MATERIAL_CLASS "Standard"\n')
            out.write("\t\t*MATERIAL_AMBIENT 0.588\t0.588\t0.588\n")
            out.write("\t\t*MATERIAL_DIFFUSE 0.588\t0.588\t0.588\n")
            out.write("\t\t*MATERIAL_SPECULAR 0.000\t0.000\t0.000\n")
            out.write("\t\t*MATERIAL_SHINE 0.010\n")
            out.write('\t\t*MATERIAL_SHADING Blinn\n')
            if bitmap:
                out.write("\t\t*MAP_DIFFUSE {\n")
                out.write('\t\t\t*MAP_NAME "%s"\n' % mat_name)
                out.write('\t\t\t*MAP_CLASS "Bitmap"\n')
                out.write('\t\t\t*BITMAP "media/objects/stadiums/%s/%s"\n'
                          % (name, bitmap))
                out.write("\t\t\t*MAP_TYPE Screen\n\t\t}\n")
            out.write("\t}\n")
        out.write("}\n")

        for geom_name, mat_index, mesh in geoms:
            _write_geomobject(out, geom_name, mat_index, mesh)
    return ase_path, len(geoms), sum(1 for _, b in materials if b)


def _write_geomobject(out, name, mat_index, mesh):
    vertex_index = {}
    vertices = []
    uvs = []
    for face in mesh.faces:
        for vertex in face.vertices:
            key = id(vertex)
            if key not in vertex_index:
                vertex_index[key] = len(vertices)
                vertices.append(vertex.position)
                uvs.append(vertex.uv[0] if vertex.uv else None)

    out.write("*GEOMOBJECT {\n")
    out.write('\t*NODE_NAME "%s"\n' % name)
    out.write("\t*NODE_TM {\n")
    out.write('\t\t*NODE_NAME "%s"\n' % name)
    out.write("\t\t*TM_ROW0 1.0\t0.0\t0.0\n\t\t*TM_ROW1 0.0\t1.0\t0.0\n")
    out.write("\t\t*TM_ROW2 0.0\t0.0\t1.0\n\t\t*TM_ROW3 0.0\t0.0\t0.0\n\t}\n")
    out.write("\t*MESH {\n")
    out.write("\t\t*MESH_NUMVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_NUMFACES %d\n" % len(mesh.faces))
    out.write("\t\t*MESH_VERTEX_LIST {\n")
    for i, pos in enumerate(vertices):
        out.write("\t\t\t*MESH_VERTEX %d\t%.4f\t%.4f\t%.4f\n"
                  % (i, pos.x, -pos.z, pos.y))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_FACE_LIST {\n")
    for i, face in enumerate(mesh.faces):
        a, b, c = [vertex_index[id(v)] for v in face.vertices]
        out.write("\t\t\t*MESH_FACE %d: A: %d B: %d C: %d "
                  "AB: 1 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID 0\n"
                  % (i, a, b, c))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMTVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_TVERTLIST {\n")
    for i, uv in enumerate(uvs):
        u, v = (uv.u, 1.0 - uv.v) if uv is not None else (0.0, 0.0)
        out.write("\t\t\t*MESH_TVERT %d\t%.5f\t%.5f\t0.0\n" % (i, u, v))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMTVFACES %d\n" % len(mesh.faces))
    out.write("\t\t*MESH_TFACELIST {\n")
    for i, face in enumerate(mesh.faces):
        a, b, c = [vertex_index[id(v)] for v in face.vertices]
        out.write("\t\t\t*MESH_TFACE %d\t%d\t%d\t%d\n" % (i, a, b, c))
    out.write("\t\t}\n")
    gf_verts = [(pos.x, -pos.z, pos.y) for pos in vertices]
    tri_faces = [tuple(vertex_index[id(v)] for v in face.vertices)
                 for face in mesh.faces]
    ase_util.write_mesh_normals(out, gf_verts, tri_faces, smooth=False)
    out.write("\t}\n")
    out.write("\t*PROP_MOTIONBLUR 0\n\t*PROP_CASTSHADOW 1\n")
    out.write("\t*PROP_RECVSHADOW 1\n")
    out.write("\t*MATERIAL_REF %d\n" % mat_index)
    out.write("}\n")


OBJECT_TEMPLATE = """<object>

	<geometry>
		<filename>pitch.ase</filename>
		<name>pitch</name>
		<position>0, 0, 0</position>
		<rotation>0, 0, 0, 0</rotation>
		<properties>
			<physicable>false</physicable>
			<movable>false</movable>
		</properties>
	</geometry>

	<geometry>
		<filename>%(name)s.ase</filename>
		<name>%(name)s</name>
		<position>0, 0, 0</position>
		<rotation>0, 0, 0, 0</rotation>
		<properties>
			<physicable>false</physicable>
			<movable>false</movable>
		</properties>
	</geometry>

</object>
"""


def convert(scene_fmdl, out_dir, fmdl_lib, tex_dirs, name, extras=(),
            max_tris=None):
    os.makedirs(out_dir, exist_ok=True)
    fmdls = [(name, _load_fmdl(scene_fmdl, fmdl_lib))]
    for extra in extras:
        label = os.path.splitext(os.path.basename(extra))[0]
        fmdls.append((label, _load_fmdl(extra, fmdl_lib)))

    ase_path, geom_count, tex_count = write_ase(fmdls, out_dir, name, tex_dirs,
                                                max_tris)

    object_path = os.path.join(out_dir, name + ".object")
    open(object_path, "w").write(OBJECT_TEMPLATE % {"name": name})

    # GF draws its own grass: reuse the stock pitch geometry
    import shutil
    stock = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "..", "data", "media", "objects", "stadiums",
                         "test", "pitch.ase")
    shutil.copy(stock, os.path.join(out_dir, "pitch.ase"))
    return ase_path, geom_count, tex_count


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("fmdl")
    parser.add_argument("out_dir")
    parser.add_argument("--fmdl-lib", required=True)
    parser.add_argument("--textures", action="append", default=[],
                        help="directory of .ftex sourceimages (repeatable)")
    parser.add_argument("--name", default=None)
    parser.add_argument("--extra", action="append", default=[])
    parser.add_argument("--max-tris", type=int, default=None,
                        help="triangle budget; largest meshes kept first")
    args = parser.parse_args()

    name = args.name or "pes_" + os.path.splitext(os.path.basename(args.fmdl))[0]
    ase_path, geoms, textures = convert(args.fmdl, args.out_dir, args.fmdl_lib,
                                        args.textures, name, args.extra,
                                        args.max_tris)
    print("wrote %s: %d geomobjects, %d textures" % (ase_path, geoms, textures))

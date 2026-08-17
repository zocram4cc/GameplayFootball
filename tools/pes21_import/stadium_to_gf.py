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
      --fmdl-lib <pes-fmdl dir> --textures <sourceimages dir or any parent> \
      [--name pes_st060] [--extra plane.fmdl ...] \
      [--max-tris 60000] [--max-verts-per-geom 16000]

--textures accepts either the directory that holds the .ftex files or any
ancestor of it: stadium packs disagree on where sourceimages live
(sourceimages/#windx11, sourceimages/tga/#windx11, deeper still in mods), so
every .ftex-holding directory underneath what you pass is searched.
"""

import argparse
import os
import sys

import ase_util
import ftex

# untextured materials point here (the PES pack may not ship the ftex a mesh
# asks for); it is a stock GF asset, so it is always present
FALLBACK_BITMAP = "media/objects/stadiums/white.png"


def _load_fmdl(path, fmdl_lib):
    if fmdl_lib and fmdl_lib not in sys.path:
        sys.path.insert(0, fmdl_lib)
    import FmdlFile
    f = FmdlFile.FmdlFile()
    f.readFile(path)
    return f


# A stadium's textures arrive as either of these. Konami's own packs ship
# .ftex; community stadium exports are usually .dds, because that is what the
# authoring tools write and nobody re-encodes them on the way out. Looking for
# ftex alone found "texture dirs: none found" on any community stadium.
TEXTURE_SUFFIXES = (".ftex", ".dds")


def find_texture_dirs(*roots):
    """-> every directory at or under `roots` holding stadium textures.

    Stadium packs put sourceimages in varying subpaths (st060 and st011 both
    ship sourceimages/tga/#windx11, others drop the tga level), so callers can
    hand over the stadium directory and let this find the texture dir.
    """
    found = []
    for root in roots:
        if not root:
            continue
        if os.path.isfile(root):
            root = os.path.dirname(root)
        for dirpath, _dirnames, filenames in os.walk(root):
            if any(f.lower().endswith(TEXTURE_SUFFIXES) for f in filenames):
                if dirpath not in found:
                    found.append(dirpath)
    return sorted(found)


def _tex_stem(filename):
    """-> basename without extension, for Fox's windows-ish texture paths."""
    base = filename.replace("\\", "/").rstrip("/").rsplit("/", 1)[-1]
    return os.path.splitext(base)[0]


def build_ftex_index(tex_dirs):
    """-> {lowercase texture stem: path}, first directory listed wins.

    Indexes .ftex and .dds alike; a pack that ships both for the same stem gets
    the ftex, since that is Konami's own encoding of it.
    """
    index = {}
    for suffix in TEXTURE_SUFFIXES:
        for tex_dir in tex_dirs:
            try:
                entries = sorted(os.listdir(tex_dir))
            except OSError:
                continue
            for entry in entries:
                if entry.lower().endswith(suffix):
                    index.setdefault(os.path.splitext(entry)[0].lower(),
                                     os.path.join(tex_dir, entry))
    return index


def _texture_png(texture, ftex_index, out_dir, converted):
    """Converts the mesh's base ftex to png; returns the bitmap path."""
    base = _tex_stem(texture.filename)
    if base in converted:
        return converted[base]
    png_rel = "textures/%s.png" % base
    out_path = os.path.join(out_dir, png_rel)
    src = ftex_index.get(base.lower())
    if src:
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        try:
            if src.lower().endswith(".dds"):
                # Pillow reads the DXT formats these packs use; a stadium shell
                # wants plain RGB anyway, so flatten and drop the alpha.
                from PIL import Image
                with Image.open(src) as image:
                    image.convert("RGB").save(out_path)
            else:
                ftex.convert(src, out_path)
            converted[base] = png_rel
            return png_rel
        except Exception as err:
            sys.stderr.write("texture conversion failed for %s: %s\n" % (src, err))
    else:
        sys.stderr.write("no texture found for %r\n" % (texture.filename,))
    converted[base] = None
    return None


OUTLINE_TEXTURE = "outline"


def is_outline_pass(texture_name):
    """Whether a texture marks one of PES's cel-shading outline shells.

    An outline is the mesh again, pushed out along its normals a few centimetres
    - about 4 cm on Planet Namek - and drawn with its front faces culled, so only
    the far side survives and it reads as a line around the silhouette. Matched on
    whole words only: "outlined_turf" is artwork, and inverting real geometry by
    accident is worse than missing an outline.
    """
    if not texture_name:
        return False
    stem = str(texture_name).replace("\\", "/").split("/")[-1].rsplit(".", 1)[0].lower()
    words = [w for w in stem.replace("-", "_").split("_") if w]
    return OUTLINE_TEXTURE in words


def face_winding(a, b, c, reverse):
    """A face's indices, reversed for an outline shell so the engine - which culls
    back faces - culls the same side PES does."""
    return (c, b, a) if reverse else (a, b, c)


def _mesh_base_texture(mesh):
    textures = mesh.materialInstance.textures
    items = textures.items() if hasattr(textures, "items") else list(textures)
    for role, tex in items:
        if "Base_Tex" in role:
            return tex
    for role, tex in items:
        return tex
    return None


def split_faces(faces, max_verts):
    """Splits a face list into chunks of at most `max_verts` unique vertices.

    One fmdl mesh can hold more vertices than the engine wants in a single
    geometry; the chunks keep sharing one material, so this only changes how
    many GEOMOBJECTs the ASE has, never what it draws.
    """
    if not max_verts or len(faces) * 3 <= max_verts:
        return [faces]
    chunks = []
    current = []
    seen = set()
    for face in faces:
        new = {id(v) for v in face.vertices} - seen
        if current and len(seen) + len(new) > max_verts:
            chunks.append(current)
            current = []
            seen = set()
            new = {id(v) for v in face.vertices}
        seen |= new
        current.append(face)
    if current:
        chunks.append(current)
    return chunks


def mesh_extent(mesh):
    """Largest horizontal span of a mesh, in metres."""
    xs = [v.position.x for v in mesh.vertices]
    ys = [v.position.z for v in mesh.vertices]
    if not xs:
        return 0.0
    return max(max(xs) - min(xs), max(ys) - min(ys))


def write_ase(fmdls, out_dir, name, tex_dirs, max_tris=None,
              max_verts_per_geom=None, max_extent=None):
    converted = {}
    ftex_index = build_ftex_index(tex_dirs)
    materials = []          # (material name, bitmap path or None)
    geoms = []              # (geom name, material index, faces)

    budget = max_tris if max_tris else float("inf")
    # A PES stadium package models its surroundings too - the car park, the
    # asphalt apron, the ground plane - as a handful of enormous flat quads
    # spanning half a kilometre. They are cheap enough to always fit the
    # triangle budget while the stands they surround get skipped, and seen
    # edge-on from pitch level they streak across the sky. Keep the bowl.
    extent_limit = max_extent if max_extent else float("inf")
    used = 0
    skipped_budget = 0
    skipped_extent = 0
    for label, fmdl in fmdls:
        # biggest meshes first so a budget keeps the structural shell and
        # drops decorative detail last-to-first
        order = sorted(range(len(fmdl.meshes)),
                       key=lambda i: -len(fmdl.meshes[i].faces))
        for i in order:
            mesh = fmdl.meshes[i]
            if mesh_extent(mesh) > extent_limit:
                skipped_extent += 1
                continue
            if used + len(mesh.faces) > budget:
                skipped_budget += 1
                continue
            used += len(mesh.faces)
            tex = _mesh_base_texture(mesh)
            bitmap = _texture_png(tex, ftex_index, out_dir, converted) if tex else None
            materials.append(("%s_m%d" % (label, i), bitmap))
            mat_index = len(materials) - 1
            # Shells are written with reversed winding rather than dropped, so
            # the cel-shaded outline PES draws survives the import.
            outline = is_outline_pass(bitmap) or is_outline_pass(getattr(tex, "name", None))
            chunks = split_faces(mesh.faces, max_verts_per_geom)
            for part, faces in enumerate(chunks):
                geom_name = "%s_%02d" % (label, i)
                if len(chunks) > 1:
                    geom_name += "_p%02d" % part
                geoms.append((geom_name, mat_index, faces, outline))

    if skipped_budget or skipped_extent:
        print("  skipped %d mesh(es) over the %s-triangle budget, "
              "%d beyond the %sm extent limit"
              % (skipped_budget, max_tris, skipped_extent, max_extent))

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
            out.write("\t\t*MATERIAL_SHINESTRENGTH 0.0\n")
            out.write("\t\t*MATERIAL_SELFILLUM 0.0\n")
            out.write('\t\t*MATERIAL_SHADING Blinn\n')
            # every material needs a diffuse map: the engine's ASE loader
            # falls back to a stock "orange.jpg" that does not ship, and a
            # missing image file is fatal to the loader
            path = ("media/objects/stadiums/%s/%s" % (name, bitmap) if bitmap
                    else FALLBACK_BITMAP)
            out.write("\t\t*MAP_DIFFUSE {\n")
            out.write('\t\t\t*MAP_NAME "%s"\n' % mat_name)
            out.write('\t\t\t*MAP_CLASS "Bitmap"\n')
            out.write('\t\t\t*BITMAP "%s"\n' % path)
            out.write("\t\t\t*MAP_TYPE Screen\n\t\t}\n")
            out.write("\t}\n")
        out.write("}\n")

        outlines = 0
        for geom_name, mat_index, faces, outline in geoms:
            _write_geomobject(out, geom_name, mat_index, faces, outline)
            outlines += 1 if outline else 0
        if outlines:
            print("  %d outline shell(s) written with reversed winding" % outlines)
    return ase_path, len(geoms), sum(1 for _, b in materials if b)


def _write_geomobject(out, name, mat_index, faces, outline=False):
    vertex_index = {}
    vertices = []
    uvs = []
    for face in faces:
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
    out.write("\t\t*INHERIT_POS 0 0 0\n\t\t*INHERIT_ROT 0 0 0\n\t\t*INHERIT_SCL 0 0 0\n")
    out.write("\t\t*TM_ROW0 1.0\t0.0\t0.0\n\t\t*TM_ROW1 0.0\t1.0\t0.0\n")
    out.write("\t\t*TM_ROW2 0.0\t0.0\t1.0\n\t\t*TM_ROW3 0.0\t0.0\t0.0\n\t}\n")
    out.write("\t*MESH {\n")
    out.write("\t\t*MESH_NUMVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_NUMFACES %d\n" % len(faces))
    out.write("\t\t*MESH_VERTEX_LIST {\n")
    for i, pos in enumerate(vertices):
        out.write("\t\t\t*MESH_VERTEX %d\t%.4f\t%.4f\t%.4f\n"
                  % (i, pos.x, -pos.z, pos.y))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_FACE_LIST {\n")
    for i, face in enumerate(faces):
        a, b, c = face_winding(*[vertex_index[id(v)] for v in face.vertices], outline)
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
    out.write("\t\t*MESH_NUMTVFACES %d\n" % len(faces))
    out.write("\t\t*MESH_TFACELIST {\n")
    for i, face in enumerate(faces):
        a, b, c = face_winding(*[vertex_index[id(v)] for v in face.vertices], outline)
        out.write("\t\t\t*MESH_TFACE %d\t%d\t%d\t%d\n" % (i, a, b, c))
    out.write("\t\t}\n")
    gf_verts = [(pos.x, -pos.z, pos.y) for pos in vertices]
    # The normals are derived from the winding, so they have to be derived from
    # the winding that was actually written: an outline shell whose faces were
    # reversed but whose normals were not has its visible side lit as though it
    # faced away, which is how the shells came out flat grey.
    tri_faces = [face_winding(*[vertex_index[id(v)] for v in face.vertices], outline)
                 for face in faces]
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
            max_tris=None, max_verts_per_geom=None, max_extent=None):
    os.makedirs(out_dir, exist_ok=True)
    tex_dirs = find_texture_dirs(*tex_dirs)
    print("texture dirs: %s" % (tex_dirs or "none found"))
    fmdls = [(name, _load_fmdl(scene_fmdl, fmdl_lib))]
    for extra in extras:
        label = os.path.splitext(os.path.basename(extra))[0]
        fmdls.append((label, _load_fmdl(extra, fmdl_lib)))

    ase_path, geom_count, tex_count = write_ase(fmdls, out_dir, name, tex_dirs,
                                                max_tris, max_verts_per_geom,
                                                max_extent)

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
                        help="directory of .ftex sourceimages, or any parent "
                             "of it (repeatable)")
    parser.add_argument("--name", default=None)
    parser.add_argument("--extra", action="append", default=[])
    parser.add_argument("--max-extent", type=float, default=260.0,
                        help="drop meshes spanning more than this many metres "
                             "(the surrounding car park/apron; 0 disables)")
    parser.add_argument("--max-tris", type=int, default=None,
                        help="triangle budget; largest meshes kept first")
    parser.add_argument("--max-verts-per-geom", type=int, default=None,
                        help="split meshes above this vertex count into "
                             "several GEOMOBJECTs sharing one material")
    args = parser.parse_args()

    name = args.name or "pes_" + os.path.splitext(os.path.basename(args.fmdl))[0]
    ase_path, geoms, textures = convert(args.fmdl, args.out_dir, args.fmdl_lib,
                                        args.textures, name, args.extra,
                                        args.max_tris, args.max_verts_per_geom,
                                        args.max_extent)
    print("wrote %s: %d geomobjects, %d textures" % (ase_path, geoms, textures))

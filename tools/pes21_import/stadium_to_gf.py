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
import glob
import math
import os
import re
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


# media/shaders/simple.frag discards anything below this alpha.
ALPHA_DISCARD_THRESHOLD = 0.12


def alpha_is_a_cutout(minimum, maximum):
    """Whether an alpha channel shapes the mesh, given its darkest and brightest.

    Kept when something is transparent and something else survives the shader's
    discard; dropped when the channel says nothing (uniformly opaque) or when
    honouring it would erase the mesh (uniformly, or nearly, transparent).
    """
    if maximum <= ALPHA_DISCARD_THRESHOLD * 255.0:
        return False
    return minimum < 255


def png_mode_for(source_mode, alpha_extrema):
    """"RGBA" when the source carries a cutout, otherwise "RGB"."""
    if "A" not in source_mode or not alpha_extrema:
        return "RGB"
    return "RGBA" if alpha_is_a_cutout(alpha_extrema[0], alpha_extrema[1]) else "RGB"


def _save_with_alpha_if_useful(image, out_path):
    """Writes `image` as a PNG, keeping its alpha only when it is a cutout."""
    alpha_extrema = image.getchannel("A").getextrema() if "A" in image.mode else None
    image.convert(png_mode_for(image.mode, alpha_extrema)).save(out_path)


def _drop_useless_alpha(path):
    """Rewrites an already-converted PNG if its alpha channel says nothing.

    Converted in place, so the flattened copy is made before the file is written
    again - Pillow reads lazily, and saving over a still-open image truncates it.
    """
    from PIL import Image
    with Image.open(path) as image:
        if "A" not in image.mode:
            return
        mode = png_mode_for(image.mode, image.getchannel("A").getextrema())
        if mode == image.mode:
            return
        flattened = image.convert(mode)
    flattened.save(path)


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
                # Pillow reads the DXT formats these packs use. Keep the alpha
                # channel when it shapes the mesh - Namek's clouds, houses, ships
                # and pods are flat quads cut out entirely in alpha, and the
                # geometry shader honours that (simple.frag discards below 0.12).
                from PIL import Image
                with Image.open(src) as image:
                    _save_with_alpha_if_useful(image, out_path)
            else:
                ftex.convert(src, out_path)
                # ftex.convert keeps whatever channels the texture had, which can
                # be an alpha that would discard the whole mesh; judge it the same
                # way.
                _drop_useless_alpha(out_path)
            converted[base] = png_rel
            return png_rel
        except Exception as err:
            sys.stderr.write("texture conversion failed for %s: %s\n" % (src, err))
    else:
        sys.stderr.write("no texture found for %r\n" % (texture.filename,))
    converted[base] = None
    return None


OUTLINE_TEXTURE = "outline"

# PES pushes an outline shell about 4 cm along the normal, which is right for a
# character filmed from a few metres. Namek's scenery is 50 to 600 m out, and at
# 600 m four centimetres is well under a pixel: the shells draw, but the line
# never reads. Scaling the push with the distance the mesh will be seen from keeps
# the rim a few pixels wide wherever it is - roughly what PES's own shaders do by
# offsetting in screen space.
OUTLINE_RADIANS = 0.0022      # about three pixels at 720p
OUTLINE_MIN = 0.04            # PES's own offset, for anything close
OUTLINE_MAX = 2.5             # a kilometre-wide backdrop does not need more


def outline_offset(distance):
    """-> how far to push a shell whose mesh sits `distance` metres out."""
    return max(OUTLINE_MIN, min(OUTLINE_MAX, abs(distance) * OUTLINE_RADIANS))


def _widen_outline(positions, tri_faces):
    """Pushes a shell out along its own smooth normals, by how far it will be seen.

    The distance is measured from the pitch centre to the shell's own centre,
    which is what decides how many pixels four centimetres is worth. The winding
    passed in is the one actually written, so the normals point the way the
    visible side faces.
    """
    normals = [[0.0, 0.0, 0.0] for _ in positions]
    for a, b, c in tri_faces:
        pa, pb, pc = positions[a], positions[b], positions[c]
        u = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
        v = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        face = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        for index in (a, b, c):
            for axis in range(3):
                normals[index][axis] += face[axis]

    centre = [sum(p[axis] for p in positions) / len(positions) for axis in range(3)]
    distance = (centre[0] ** 2 + centre[1] ** 2 + centre[2] ** 2) ** 0.5
    # Outward is against these normals: the shells are written with reversed
    # winding - that is what turns their visible side towards the camera - so the
    # winding's own normal points into the mesh, and pushing along it shrank the
    # shell instead of growing it.
    offset = -outline_offset(distance)
    return [push_along_normal(p, n, offset) for p, n in zip(positions, normals)]


def push_along_normal(vertex, normal, offset):
    """-> the vertex moved `offset` metres along `normal` (unit or not)."""
    length = (normal[0] ** 2 + normal[1] ** 2 + normal[2] ** 2) ** 0.5
    if length < 1e-6:
        return vertex
    scale = offset / length
    return (vertex[0] + normal[0] * scale,
            vertex[1] + normal[1] * scale,
            vertex[2] + normal[2] * scale)


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


# A stadium's own sky is a mesh the camera stands inside, so it needs the same
# treatment as an outline shell (reversed winding, because its faces point
# outward) plus normals that leave it unlit - lit like a wall, Planet Namek's green
# sky comes out a white blowout. Thresholds are set to take the domes and nothing
# else - and size cannot do that on its own. Namek's sky is 1154 m across and
# reaches 624 m, but st019 is a custom ground whose bowl spans 1.5 x 5 km and
# reaches 1160 m, so measuring metres called that entire stadium sky and left two
# billboards standing in an empty scene.
#
# What tells a sky apart is which way it faces. PES authors a dome with its normals
# turned in on the camera inside it (Namek's two average 1.00 and 0.97 towards the
# pitch centre), while stands, terrain and scenery face every which way and average
# out near zero (st019's three bowl meshes: -0.02, -0.03, -0.05). That is a property
# of the geometry rather than of the ground's scale, so it holds for any pack.
SKY_MIN_SPAN = 300.0        # metres across, both ways
SKY_MIN_TOP = 50.0          # metres above the pitch
SKY_MIN_CAMERA_FACING = 0.8  # how squarely its normals have to look at the camera
SKY_CONSTANT_NORMAL = (0.0, 0.0, -1.0)  # away from the sun, as the engine's own sky.ase does


def is_sky_dome(span_x, span_y, top_z, contains_origin, camera_facing):
    """Whether a mesh is a sky the camera stands inside.

    Both spans have to be large: a wide flat apron is not a sky, and neither is a
    floodlight mast that is merely tall. It also has to surround the pitch - a
    backdrop off to one side is seen from outside and must keep its winding. And
    with the size settled, its normals have to look back at the camera the way a
    dome's do, or it is a stadium and not a sky.
    """
    if not contains_origin:
        return False
    if span_x < SKY_MIN_SPAN or span_y < SKY_MIN_SPAN:
        return False
    if top_z < SKY_MIN_TOP:
        return False
    return camera_facing >= SKY_MIN_CAMERA_FACING


def mesh_camera_facing(mesh):
    """How much a mesh's normals turn in on its own centre: 1 all the way, -1 away.

    A dome is authored for a camera inside it, so every normal points inward; a
    stand, a roof or a hillside points wherever it happens to face and the average
    collapses towards zero. Measured against the mesh's own centre rather than the
    pitch spot, so a dome that is not perfectly centred still reads as one.
    """
    positions = [(v.position.x, -v.position.z, v.position.y) for v in mesh.vertices]
    if not positions:
        return 0.0
    centre = [(min(c) + max(c)) / 2.0 for c in zip(*positions)]
    total = 0.0
    counted = 0
    for vertex, position in zip(mesh.vertices, positions):
        normal = getattr(vertex, "normal", None)
        if normal is None:
            continue
        away = [position[axis] - centre[axis] for axis in range(3)]
        length = math.sqrt(sum(component * component for component in away))
        if length < 1e-6:
            continue
        inward = [-component / length for component in away]
        facing = (normal.x * inward[0] + (-normal.z) * inward[1] + normal.y * inward[2])
        total += facing
        counted += 1
    return total / counted if counted else 0.0


def sample_sky_colours(png_path):
    """(zenith rgb, horizon rgb) from a sky dome's texture, each 0..1.

    A PES sky texture runs from the zenith at the top of the image to the horizon at
    the bottom, so the two ends are averaged over a band rather than a single row -
    clouds and moons live in the middle and must not drag either end.
    """
    try:
        from PIL import Image
    except ImportError:
        print("  sky: needs Pillow to sample the dome's colours")
        return None
    try:
        image = Image.open(png_path).convert("RGB")
    except Exception as exc:
        print("  sky: could not read %s: %s" % (os.path.basename(png_path), exc))
        return None
    # Averaged over bands, a tenth of the image each, so clouds and moons cannot
    # drag a single row. The zenith is the top of the texture; the horizon is the
    # brightest band, which is where a sky is brightest and where PES puts it -
    # namekbackground's yellow-green band sits nearer the middle than the bottom,
    # and taking the bottom row gave the dark ground below it instead.
    bands = 10
    band = max(1, image.height // bands)
    strips = []
    for i in range(bands):
        top_y = i * band
        strip = image.crop((0, top_y, image.width, min(top_y + band, image.height)))
        strips.append(strip.resize((1, 1), Image.BOX).getpixel((0, 0)))
    zenith = strips[0]
    horizon = max(strips, key=lambda rgb: 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2])
    to_unit = lambda rgb: tuple(c / 255.0 for c in rgb)
    return to_unit(zenith), to_unit(horizon)


def choose_scene_models(paths):
    """-> (the centre scene, its sibling models) out of a pack's scene files.

    A PES stadium is fifteen models, not one: its own scene graph names back1/2/3,
    center1/2/3, front1/2/3, left1/2/3 and right1/2/3 (Planet Namek's
    st017.fox2.xml lists exactly those). Taking center1 alone cost st002 a front
    section and st060 its two aeroplanes, which are part of that ground.

    A pack also holds each model twice - once under <pack>_fpk/ and once under
    <pack>_fpk_extracted/ - so siblings are taken by name, once each, in a settled
    order.
    """
    by_name = {}
    for path in sorted(paths):
        by_name.setdefault(os.path.basename(path), path)
    if not by_name:
        return (None, [])
    names = sorted(by_name)
    centre_name = next((n for n in names if "center1" in n.lower()),
                       next((n for n in names if "center" in n.lower()), names[0]))
    centre = by_name.pop(centre_name)
    return (centre, [by_name[n] for n in sorted(by_name)])


def geom_label(label, index):
    return "%s_%02d" % (label, index)


def mesh_bounds(mesh):
    """(span_x, span_y, top_z, contains_origin) in GF space, for is_sky_dome."""
    xs = [v.position.x for v in mesh.vertices]
    ys = [-v.position.z for v in mesh.vertices]  # GF y is -z in Fox space
    zs = [v.position.y for v in mesh.vertices]   # GF z (up) is Fox y
    if not xs:
        return (0.0, 0.0, 0.0, False)
    contains = min(xs) <= 0.0 <= max(xs) and min(ys) <= 0.0 <= max(ys)
    return (max(xs) - min(xs), max(ys) - min(ys), max(zs), contains)


def mesh_extent(mesh):
    """Largest horizontal span of a mesh, in metres."""
    xs = [v.position.x for v in mesh.vertices]
    ys = [v.position.z for v in mesh.vertices]
    if not xs:
        return 0.0
    return max(max(xs) - min(xs), max(ys) - min(ys))


def write_ase(fmdls, out_dir, name, tex_dirs, max_tris=None,
              max_verts_per_geom=None, max_extent=None, fallback_bitmap=None):
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
    # The furthest any kept vertex sits from the pitch centre. The engine's
    # gameplay far plane is 200-250 m; a pack whose sky is its own mesh needs the
    # frustum opened up or the sky is simply clipped away (see
    # src/onthepitch/stadiumfar.hpp).
    reach = 0.0
    sky_texture = None  # the converted texture of the tallest dome
    sky_top = 0.0
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
            for v in mesh.vertices:
                distance = (v.position.x ** 2 + v.position.y ** 2 + v.position.z ** 2) ** 0.5
                if distance > reach:
                    reach = distance
            tex = _mesh_base_texture(mesh)
            bitmap = _texture_png(tex, ftex_index, out_dir, converted) if tex else None
            materials.append(("%s_m%d" % (label, i), bitmap))
            mat_index = len(materials) - 1
            # Shells are written with reversed winding rather than dropped, so
            # the cel-shaded outline PES draws survives the import.
            outline = is_outline_pass(bitmap) or is_outline_pass(getattr(tex, "name", None))
            # The camera stands inside a sky, so it is inverted like a shell - and
            # drawn unlit, or its own colour is lost to the lighting.
            span_x, span_y, top_z, contains_origin = mesh_bounds(mesh)
            sky = is_sky_dome(span_x, span_y, top_z, contains_origin,
                              mesh_camera_facing(mesh))
            if sky:
                print("  sky dome: %s (%.0f x %.0f m, %.0f m up)"
                      % (geom_label(label, i), span_x, span_y, top_z))
                # the tallest dome is the sky proper; a lower one is the clouds
                if bitmap and top_z > sky_top:
                    sky_top = top_z
                    sky_texture = bitmap
            chunks = split_faces(mesh.faces, max_verts_per_geom)
            for part, faces in enumerate(chunks):
                geom_name = "%s_%02d" % (label, i)
                if len(chunks) > 1:
                    geom_name += "_p%02d" % part
                geoms.append((geom_name, mat_index, faces, outline, sky))

    if skipped_budget or skipped_extent:
        print("  skipped %d mesh(es) over the %s-triangle budget, "
              "%d beyond the %sm extent limit"
              % (skipped_budget, max_tris, skipped_extent, max_extent))

    # The sky the stadium wants painted behind it. Its dome is imported, but the
    # gradient in postprocess.frag is what actually draws, so the dome's own colours
    # are sampled here and written for the engine to paint with
    # (src/onthepitch/stadiumsky.hpp).
    if sky_texture:
        colours = sample_sky_colours(os.path.join(out_dir, sky_texture))
        if colours:
            zenith, horizon = colours
            with open(os.path.join(out_dir, "sky.txt"), "w") as skyfile:
                skyfile.write("zenith %.4f %.4f %.4f\n" % zenith)
                skyfile.write("horizon %.4f %.4f %.4f\n" % horizon)
            print("  sky: zenith %.2f %.2f %.2f, horizon %.2f %.2f %.2f (from %s)"
                  % (zenith + horizon + (sky_texture,)))

    if reach > 0.0:
        with open(os.path.join(out_dir, "farplane.txt"), "w") as far:
            far.write("%.0f\n" % (reach * 1.05))  # a little past the furthest vertex
        print("  geometry reaches %.0f m; wrote farplane.txt" % reach)

    # A dome the camera stands inside cannot live in the stadium's own object: the
    # engine splits that node's geometry into 24 m grid cells for culling and a
    # 1154 m dome does not survive it - it was never rasterised, which is why the sky
    # was the engine's fallback gradient and the clouds and moons were missing. The
    # engine loads <stadium>/sky/sky.object separately and keeps it out of the shadow
    # map, so that is where the domes are written.
    sky_geoms = [g for g in geoms if g[4]]
    if sky_geoms:
        sky_dir = os.path.join(out_dir, "sky")
        os.makedirs(sky_dir, exist_ok=True)
        with open(os.path.join(sky_dir, "sky.ase"), "w") as out:
            _write_ase_header(out, "sky")
            out.write("*MATERIAL_LIST {\n\t*MATERIAL_COUNT %d\n" % len(sky_geoms))
            for new_index, (_, mat_index, _, _, _) in enumerate(sky_geoms):
                _write_material(out, new_index, materials[mat_index][0], materials[mat_index][1],
                                name, fallback_bitmap)
            out.write("}\n")
            for new_index, (geom_name, _, faces, _, _) in enumerate(sky_geoms):
                # inside-out, because the camera is inside it, and unlit, or its own
                # colour is lost to the lighting
                _write_geomobject(out, geom_name, new_index, faces, True, True)
        open(os.path.join(sky_dir, "sky.object"), "w").write(object_text("sky", with_pitch=False))
        print("  %d sky dome(s) -> sky/sky.object (inside-out, unlit)" % len(sky_geoms))

    ase_path = os.path.join(out_dir, name + ".ase")
    with open(ase_path, "w") as out:
        _write_ase_header(out, name)
        out.write("*MATERIAL_LIST {\n")
        out.write("\t*MATERIAL_COUNT %d\n" % len(materials))
        for m, (mat_name, bitmap) in enumerate(materials):
            _write_material(out, m, mat_name, bitmap, name, fallback_bitmap)
        out.write("}\n")

        outlines = 0
        for geom_name, mat_index, faces, outline, sky in geoms:
            if sky:
                continue  # the domes go to their own object, below
            _write_geomobject(out, geom_name, mat_index, faces, outline, False)
            outlines += 1 if outline else 0
        if outlines:
            print("  %d outline shell(s) written with reversed winding" % outlines)
    return ase_path, len(geoms), sum(1 for _, b in materials if b)


def _write_ase_header(out, name):
    out.write("*3DSMAX_ASCIIEXPORT\t200\n")
    out.write('*COMMENT "converted from PES stadium by tools/pes21_import"\n')
    out.write("*SCENE {\n\t*SCENE_FILENAME \"%s\"\n" % name)
    out.write("\t*SCENE_FIRSTFRAME 0\n\t*SCENE_LASTFRAME 100\n")
    out.write("\t*SCENE_FRAMESPEED 30\n\t*SCENE_TICKSPERFRAME 160\n")
    out.write("\t*SCENE_BACKGROUND_STATIC 0.000\t0.000\t0.000\n")
    out.write("\t*SCENE_AMBIENT_STATIC 0.000\t0.000\t0.000\n}\n")


def _write_material(out, index, mat_name, bitmap, stadium_name, fallback_bitmap):
    out.write("\t*MATERIAL %d {\n" % index)
    out.write('\t\t*MATERIAL_NAME "%s"\n' % mat_name)
    out.write('\t\t*MATERIAL_CLASS "Standard"\n')
    out.write("\t\t*MATERIAL_AMBIENT 0.588\t0.588\t0.588\n")
    out.write("\t\t*MATERIAL_DIFFUSE 0.588\t0.588\t0.588\n")
    out.write("\t\t*MATERIAL_SPECULAR 0.000\t0.000\t0.000\n")
    out.write("\t\t*MATERIAL_SHINE 0.010\n")
    out.write("\t\t*MATERIAL_SHINESTRENGTH 0.0\n")
    out.write("\t\t*MATERIAL_SELFILLUM 0.0\n")
    out.write('\t\t*MATERIAL_SHADING Blinn\n')
    # every material needs a diffuse map: the engine's ASE loader falls back to a
    # stock "orange.jpg" that does not ship, and a missing image file is fatal
    path = ("media/objects/stadiums/%s/%s" % (stadium_name, bitmap) if bitmap
            else (fallback_bitmap or FALLBACK_BITMAP))
    out.write("\t\t*MAP_DIFFUSE {\n")
    out.write('\t\t\t*MAP_NAME "%s"\n' % mat_name)
    out.write('\t\t\t*MAP_CLASS "Bitmap"\n')
    out.write('\t\t\t*BITMAP "%s"\n' % path)
    out.write("\t\t\t*MAP_TYPE Screen\n\t\t}\n")
    out.write("\t}\n")


def _write_geomobject(out, name, mat_index, faces, outline=False, sky=False):
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
    # In engine space, and for an outline shell pushed out far enough to be seen
    # from where it will be seen (outline_offset).
    gf_positions = [(pos.x, -pos.z, pos.y) for pos in vertices]
    if outline and gf_positions:
        gf_positions = _widen_outline(gf_positions,
                                      [face_winding(*[vertex_index[id(v)] for v in face.vertices],
                                                    outline) for face in faces])

    out.write("\t\t*MESH_NUMVERTEX %d\n" % len(vertices))
    out.write("\t\t*MESH_NUMFACES %d\n" % len(faces))
    out.write("\t\t*MESH_VERTEX_LIST {\n")
    for i, pos in enumerate(gf_positions):
        out.write("\t\t\t*MESH_VERTEX %d\t%.4f\t%.4f\t%.4f\n" % (i, pos[0], pos[1], pos[2]))
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
    gf_verts = gf_positions
    # The normals are derived from the winding, so they have to be derived from
    # the winding that was actually written: an outline shell whose faces were
    # reversed but whose normals were not has its visible side lit as though it
    # faced away, which is how the shells came out flat grey.
    tri_faces = [face_winding(*[vertex_index[id(v)] for v in face.vertices], outline)
                 for face in faces]
    if sky:
        # One normal everywhere, pointing away from the sun: the dome keeps its
        # own colour instead of being lit like a wall.
        ase_util.write_mesh_normals(out, gf_verts, tri_faces, smooth=False,
                                    constant=SKY_CONSTANT_NORMAL)
    else:
        ase_util.write_mesh_normals(out, gf_verts, tri_faces, smooth=False)
    out.write("\t}\n")
    out.write("\t*PROP_MOTIONBLUR 0\n\t*PROP_CASTSHADOW 1\n")
    out.write("\t*PROP_RECVSHADOW 1\n")
    out.write("\t*MATERIAL_REF %d\n" % mat_index)
    out.write("}\n")


# Only a stadium gets a pitch. A shared package - the advertising ring, a sky dome -
# must not declare one: the engine splits every geometry under the stadium node for
# culling and names the pieces after it, so a second "pitch" collides
# ("Duplicate key 'pitch gridGeomData @ ...'") and the match dies at load.
GEOMETRY_ENTRY = """	<geometry>
		<filename>%(file)s</filename>
		<name>%(name)s</name>
		<position>0, 0, 0</position>
		<rotation>0, 0, 0, 0</rotation>
		<properties>
			<physicable>false</physicable>
			<movable>false</movable>
		</properties>
	</geometry>
"""


def object_text(name, with_pitch=True):
    parts = ["<object>\n\n"]
    if with_pitch:
        parts.append(GEOMETRY_ENTRY % {"file": "pitch.ase", "name": "pitch"})
        parts.append("\n")
    parts.append(GEOMETRY_ENTRY % {"file": name + ".ase", "name": name})
    parts.append("\n</object>\n")
    return "".join(parts)


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


# PES names a stadium's ground colour <stadium>_turf000_bsm: _bsm is the base
# map, _nrm the normal map, _srm the specular. "grassfin" is the standing blades
# of the 3D turf, not the ground.
TURF_BASE_HINTS = ("turf",)
TURF_EXCLUDE = ("grassfin",)


class _NamedTexture(object):
    """The shape _texture_png wants, for a texture chosen by name rather than
    reached through a mesh's material."""

    def __init__(self, filename):
        self.filename = filename
        self.name = filename


def _all_texture_names(tex_dirs):
    """Every texture the pack ships, by file name."""
    return sorted(os.path.basename(path) for path in build_ftex_index(tex_dirs).values())


def find_turf_texture(names):
    """The pack's own turf base map, or None.

    Prefers a name carrying the stadium's number, so a pack that ships both its
    own turf and a generic one gets its own.
    """
    candidates = []
    for name in names:
        stem = str(name).replace("\\", "/").split("/")[-1].rsplit(".", 1)[0].lower()
        if any(x in stem for x in TURF_EXCLUDE):
            continue
        if not any(h in stem for h in TURF_BASE_HINTS):
            continue
        if not stem.endswith("_bsm") and "_bsm_" not in stem and not stem.endswith("_bsm_alp"):
            continue  # _nrm/_srm would paint the pitch with a normal map
        candidates.append(name)
    if not candidates:
        return None
    # a stadium-specific name ("st017_turf000_bsm") beats a generic one ("turf_bsm")
    candidates.sort(key=lambda n: (0 if "turf000" in str(n).lower() else 1, str(n)))
    return candidates[0]


TURF_FILENAME = "turf.png"
def convert(scene_fmdl, out_dir, fmdl_lib, tex_dirs, name, extras=(),
            max_tris=None, max_verts_per_geom=None, max_extent=None,
            fallback_bitmap=None, with_pitch=True):
    os.makedirs(out_dir, exist_ok=True)
    tex_dirs = find_texture_dirs(*tex_dirs)
    print("texture dirs: %s" % (tex_dirs or "none found"))
    fmdls = [(name, _load_fmdl(scene_fmdl, fmdl_lib))]
    for extra in extras:
        label = os.path.splitext(os.path.basename(extra))[0]
        fmdls.append((label, _load_fmdl(extra, fmdl_lib)))

    ase_path, geom_count, tex_count = write_ase(fmdls, out_dir, name, tex_dirs,
                                                max_tris, max_verts_per_geom,
                                                max_extent, fallback_bitmap)

    object_path = os.path.join(out_dir, name + ".object")
    open(object_path, "w").write(object_text(name, with_pitch))

    if not with_pitch:
        return ase_path, geom_count, tex_count

    # GF's pitch geometry carries the line markings and the physics, so it is
    # kept as it is. Its *colour* is generated at match start by
    # proceduralpitch.cpp from a seamless grass tile, and the engine prefers a
    # turf.png sitting beside the stadium object over its own green grass (see
    # src/onthepitch/pitchturf.hpp) - so the pack's ground colour goes there.
    # Repointing the pitch materials instead does not work: the generator
    # overwrites the texture resources by the names pitch_0N.png, and with those
    # names gone the match dies as soon as the bitmaps are built.
    import shutil
    stock = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "..", "data", "media", "objects", "stadiums",
                         "test", "pitch.ase")
    shutil.copy(stock, os.path.join(out_dir, "pitch.ase"))

    turf = find_turf_texture(_all_texture_names(tex_dirs))
    if turf:
        png = _texture_png(_NamedTexture(turf), build_ftex_index(tex_dirs), out_dir, {})
        if png:
            shutil.copy(os.path.join(out_dir, png), os.path.join(out_dir, TURF_FILENAME))
            print("  pitch turf: %s -> %s" % (turf, TURF_FILENAME))
        else:
            print("  pitch turf %s could not be converted; keeping GF's grass" % turf)
    return ase_path, geom_count, tex_count


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("fmdl", help="a stadium scene .fmdl, or a directory of them "
                                          "(the whole scene: center1 and its siblings)")
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
    # PES assigns the advertising faces at runtime (bill_anime.json), so the board
    # models name source art that was never shipped. Pointing those at the engine's
    # ad_placeholder hands them to GF's own randomiser, which swaps in a panel from
    # media/textures/adboards - the same thing PES does, by the mechanism this
    # engine already has.
    parser.add_argument("--no-pitch", action="store_true",
                        help="for a shared package rather than a ground: omit the "
                             "pitch from the .object (two pitches collide at load)")
    parser.add_argument("--fallback-bitmap", default=None,
                        help="bitmap for materials whose texture cannot be found "
                             "(default: a white placeholder)")
    parser.add_argument("--max-verts-per-geom", type=int, default=None,
                        help="split meshes above this vertex count into "
                             "several GEOMOBJECTs sharing one material")
    args = parser.parse_args()

    scene = args.fmdl
    extras = list(args.extra)
    if os.path.isdir(scene):
        # A whole stadium: the centre scene and every sibling model the pack ships,
        # which is what its own scene graph asks for (choose_scene_models).
        found = glob.glob(os.path.join(scene, "**", "*.fmdl"), recursive=True)
        scene, siblings = choose_scene_models(found)
        if scene is None:
            print("no scene models under %s" % args.fmdl)
            sys.exit(1)
        extras = siblings + extras
        print("scene: %s%s" % (os.path.basename(scene),
                               (" + " + ", ".join(os.path.basename(s) for s in siblings))
                               if siblings else ""))

    name = args.name or "pes_" + os.path.splitext(os.path.basename(scene))[0]
    ase_path, geoms, textures = convert(scene, args.out_dir, args.fmdl_lib,
                                        args.textures, name, extras,
                                        args.max_tris, args.max_verts_per_geom,
                                        args.max_extent, args.fallback_bitmap,
                                        not args.no_pitch)
    print("wrote %s: %d geomobjects, %d textures" % (ase_path, geoms, textures))

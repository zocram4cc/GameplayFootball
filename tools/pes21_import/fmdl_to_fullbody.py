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

import stretched_cut
import os
import re
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


# An influence this small is noise: the engine drops any channel decoding to
# <= 0.01 anyway, and keeping it costs one of the three slots.
MIN_INFLUENCE = 0.02


def encode_color(joints):
    """[(jointID, weight)] -> three 0..1 floats (ASE color channels).

    The engine decodes each channel as jointID*10 + weight*9, renormalises the
    three so they sum to 1, skips any decoding to <= 0.01, and asserts the
    first is non-zero (humanoidbase.cpp).

    Negligible influences are dropped rather than raised to meet that assert.
    Clamping them up instead - which is what this did - turned a vertex 99% on
    one bone with two traces into 79/10.5/10.5 once the engine renormalised,
    dragging shoulder and hip vertices toward bones they barely belong to.
    What survives is renormalised here so the authored proportions come back
    out the other side.
    """
    kept = [(j, w) for j, w in joints if w > MIN_INFLUENCE]
    kept.sort(key=lambda jw: -jw[1])       # channel 0 carries the strongest
    kept = kept[:3]
    if not kept:
        # Every vertex must ride something: the engine asserts on channel 0.
        kept = [(joints[0][0] if joints else 0, 1.0)]
    total = sum(w for _, w in kept)
    channels = []
    for j, w in kept:
        w = min(1.0, max(MIN_INFLUENCE, w / total))
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
        # PES ships extra copies of each mesh for passes this engine does not
        # render - the antiblur pass, and an outline shell sitting just outside
        # the body. Keeping them doubles the model and the copies z-fight.
        material = getattr(mesh, "materialInstance", None)
        if is_non_render_pass(getattr(material, "name", ""), mesh_base_texture(mesh)):
            continue
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


def mesh_base_texture(mesh):
    """File name of a mesh's colour texture, without directory or suffix.

    Fmdl material textures come as (role, texture) pairs; the colour map is
    the SRGB base one. Normal/specular roles are ignored - the engine takes a
    diffuse map per material and nothing else here.
    """
    material = getattr(mesh, "materialInstance", None)
    for role, texture in (getattr(material, "textures", None) or ()):
        if "Base_Tex" in role or role.endswith("_SRGB"):
            return os.path.splitext(os.path.basename(texture.filename))[0]
    return None


def export_textures(fmdl_path, out_dir, names, prefix):
    """Writes each named source texture next to the model as a .png.

    The pack ships .dds beside the .fmdl; the engine reads PNG (simple,
    editable formats through and through). Anything that cannot be found or
    decoded is reported and skipped, leaving that material pointing at a file
    the loader will complain about rather than silently texturing it wrong.
    """
    try:
        from PIL import Image
    except ImportError:
        print("PIL not available: textures not exported")
        return {}

    source_dir = os.path.dirname(os.path.abspath(fmdl_path))
    written = {}
    for name in sorted(n for n in names if n):
        found = None
        for entry in os.listdir(source_dir):
            stem, ext = os.path.splitext(entry)
            if stem.lower() == name.lower() and ext.lower() in (".dds", ".png", ".tga"):
                found = os.path.join(source_dir, entry)
                break
        if not found:
            print("  texture %-24s NOT FOUND in %s" % (name, source_dir))
            continue
        try:
            image = Image.open(found)
            image.load()
            if image.mode != "RGBA":
                image = image.convert("RGBA")
            unique = "%s_%s.png" % (prefix, name)
            image.save(os.path.join(out_dir, unique))
            written[name] = unique
            print("  texture %-24s %dx%d" % (name, image.size[0], image.size[1]))
        except Exception as error:
            print("  texture %-24s FAILED (%s)" % (name, error))
    return written


# The engine's kit slot: HumanoidBase::SetKit replaces every mesh whose diffuse
# texture is this one with the team's kit (Team::FetchKit).
KIT_SLOT_TEXTURE = "media/objects/players/textures/kit_template.png"

# Passes PES draws that this engine has no equivalent for. Each is a copy of
# the body a hair outside the real one - keeping them means two surfaces
# fighting for the same pixels, which reads as torn patchwork along the seams.
NON_RENDER_PASSES = ("antiblur", "outline")


def is_non_render_pass(material_name, base_texture):
    """Is this mesh one of PES's extra passes rather than the model itself?

    Matched on whole words in either the material or its base texture: the
    shell is sometimes named by one and sometimes by the other, but a texture
    like "outlined_crest" is artwork and has to survive.
    """
    for field in (material_name or "", base_texture or ""):
        for word in re.split(r"[^a-z0-9]+", field.lower()):
            if word in NON_RENDER_PASSES:
                return True
    return False


def unresolved_group_texture(base_ase, fallback_texture):
    """Texture for a mesh whose own texture the pack does not ship.

    4cc models point their kit mesh at the shared PES kit map (u0XXXp0), which
    no pack contains. A whole-character import gets that kit dropped in beside
    it (import_team.install_kit_texture) and uses it directly. A face-slot
    import composited onto a base body has no such file - and the mesh is kit,
    so it belongs in the engine's kit slot, where the team's own kit is swapped
    in per match instead of being baked to one strip.
    """
    return KIT_SLOT_TEXTURE if base_ase else fallback_texture


def base_material_plan(base_material_count, group_textures, fallback_texture):
    """Materials to append for the imported groups, and each group's ref.

    Compositing onto a base body (--base) keeps the base's material list
    verbatim, so the imported meshes have to be numbered *after* it. Numbering
    them from zero instead hands them the base's own material 0 - the shirt,
    whose texture is kit_template.png, which is the slot
    HumanoidBase::SetKit swaps the team kit into. That is how /a/'s squad
    ended up wearing the jersey on their faces.

    Returns (appended_textures, refs), one entry each per group, and always at
    least one so a groupless import still has a material to point at.
    """
    textures = [t or fallback_texture for t in group_textures] or [fallback_texture]
    refs = [base_material_count + i for i in range(len(textures))]
    return textures, refs


def convert(fmdl_path, out_dir, fmdl_lib, texture, base_ase=None,
            max_tris=None, only_meshes=None, force_joint=None, max_edge=0.0,
            drop_base_parts=None):
    sys.path.insert(0, fmdl_lib)
    import FmdlFile
    fmdl = FmdlFile.FmdlFile()
    fmdl.readFile(fmdl_path)

    bone_to_joint = build_bone_map(fmdl)
    joint_positions = retarget.gf_world_bind()

    meshes = select_meshes(fmdl.meshes, max_tris, bone_to_joint, joint_positions)
    if only_meshes is not None:
        meshes = [m for i, m in enumerate(meshes) if i in only_meshes]

    # One group per source texture.
    #
    # A 4cc character is not one skinned mesh with one skin: it is a dozen
    # meshes across half a dozen materials - hair, dress, skin, shoes, props -
    # each with its own base texture. Flattening them onto a single material
    # (which is what this did) leaves every part but one sampling somebody
    # else's texture, and the character comes out in patches. Each texture
    # becomes its own ASE material and its own GEOMOBJECT instead.
    groups = []          # [(texture_name, vertices, faces, index)]
    group_of = {}
    for mesh in meshes:
        name = mesh_base_texture(mesh) or ""
        if name not in group_of:
            group_of[name] = len(groups)
            groups.append([name, [], [], {}])
        _, vertices, faces, index = groups[group_of[name]]
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

    if max_edge != 0.0:
        # The cut follows the mesh rather than a fixed metre value. An absolute 0.15 m
        # works on a fine mesh and destroys a coarse one: over the 90 models already
        # imported, 44 have their longest surviving edge sitting exactly on that cut,
        # and nine are coarse meshes where 0.15 m is only 1.6x to 3.6x their median
        # edge. The shards this is for were 1.25 m against a 1.9 cm median.
        # A negative --max-edge asks for the old absolute behaviour.
        dropped = 0
        for group in groups:
            vertices, faces = group[1], group[2]
            if max_edge < 0.0:
                limit = -max_edge
                kept = [tri for tri in faces
                        if max(math.dist(vertices[tri[0]][0], vertices[tri[1]][0]),
                               math.dist(vertices[tri[1]][0], vertices[tri[2]][0]),
                               math.dist(vertices[tri[2]][0], vertices[tri[0]][0])) <= limit]
            else:
                triangles = [tuple(vertices[i][0] for i in tri) for tri in faces]
                limit = stretched_cut.limit_for(triangles)
                kept = faces if limit <= 0.0 else [
                    tri for tri, points in zip(faces, triangles)
                    if max(math.dist(points[0], points[1]),
                           math.dist(points[1], points[2]),
                           math.dist(points[2], points[0])) <= limit]
            dropped += len(faces) - len(kept)
            group[2] = kept
        if dropped:
            print("dropped %d stretched triangle(s), threshold from each mesh's own "
                  "geometry" % dropped)

    # what the rest of the writer used to work on
    vertices = groups[0][1] if groups else []
    faces = groups[0][2] if groups else []

    os.makedirs(out_dir, exist_ok=True)

    # Each group's colour map, written beside the model. The engine's
    # resource manager keys surfaces by BASENAME, so these are prefixed with
    # the model's own directory name - two characters both shipping a
    # "skin_color" would otherwise share whichever loaded first.
    model_id = os.path.basename(os.path.normpath(out_dir))
    exported = export_textures(fmdl_path, out_dir, [g[0] for g in groups], model_id)
    texture_rel = os.path.dirname(texture)

    def group_texture_path(name):
        if name and name in exported:
            return "%s/%s" % (texture_rel, exported[name]) if texture_rel else exported[name]
        return unresolved_group_texture(base_ase, texture)

    # the engine's resource cache keys geometry by BASENAME, so every model
    # needs a unique ase filename or it collides with the stock fullbody.ase
    unique = "fullbody_%s.ase" % os.path.basename(os.path.normpath(out_dir))
    ase_path = os.path.join(out_dir, unique)

    # --base: carry the stock body (materials, geometry, skin colors) over
    # verbatim; the import becomes an extra subgeom with its own material
    group_refs = None  # set when compositing onto a base body
    base_head = base_geoms = None
    if base_ase:
        import re
        base_text = open(base_ase).read()
        geom_at = base_text.find("*GEOMOBJECT {")
        base_head = base_text[:geom_at]
        base_geoms = base_text[geom_at:]

        # A face-slot import brings its own head. Left in place, the stock
        # body's face, scalp, hair and eyes sit inside it and fight it for
        # depth, which reads as a dark, doubled head. Drop them.
        if drop_base_parts:
            kept_geoms = []
            for block in base_geoms.split("*GEOMOBJECT {"):
                if not block.strip():
                    continue
                name_match = re.search(r'\*NODE_NAME "([^"]*)"', block)
                name = name_match.group(1) if name_match else ""
                if name in drop_base_parts:
                    continue
                kept_geoms.append("*GEOMOBJECT {" + block)
            dropped = base_geoms.count("*GEOMOBJECT {") - len(kept_geoms)
            base_geoms = "".join(kept_geoms)
            if dropped:
                print("dropped %d base part(s): %s"
                      % (dropped, ", ".join(sorted(drop_base_parts))))
        count_match = re.search(r"\*MATERIAL_COUNT\s+(\d+)", base_head)
        base_material_count = int(count_match.group(1))
        appended, group_refs = base_material_plan(
            base_material_count,
            [group_texture_path(group[0]) for group in groups],
            texture)
        base_head = base_head.replace(
            count_match.group(0),
            "*MATERIAL_COUNT %d" % (base_material_count + len(appended)))
        close_at = base_head.rstrip().rfind("}")
        plate_materials = "".join(
            "\t*MATERIAL %d {\n%s\t}\n" % (base_material_count + i,
                                           MATERIAL_BLOCK % {"texture": tex})
            for i, tex in enumerate(appended))
        base_head = base_head[:close_at] + plate_materials + base_head[close_at:]

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
            out.write("*MATERIAL_LIST {\n\t*MATERIAL_COUNT %d\n" % max(1, len(groups)))
            for slot, group in enumerate(groups):
                out.write("\t*MATERIAL %d {\n" % slot)
                out.write(MATERIAL_BLOCK % {"texture": group_texture_path(group[0])})
                out.write("\t}\n")
            if not groups:
                out.write("\t*MATERIAL 0 {\n")
                out.write(MATERIAL_BLOCK % {"texture": texture})
                out.write("\t}\n")
            out.write("}\n")

        for slot, group in enumerate(groups):
            texture_name, vertices, faces, _ = group
            if not faces:
                continue
            node_name = "fullbody_%s" % (texture_name or "import")
            out.write("*GEOMOBJECT {\n")
            out.write('\t*NODE_NAME "%s"\n' % node_name)
            out.write("\t*NODE_TM {\n\t\t*NODE_NAME \"%s\"\n" % node_name)
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
            out.write("\t*PROP_RECVSHADOW 1\n\t*MATERIAL_REF %d\n}\n"
                      % (group_refs[slot] if group_refs else slot))

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
    parser.add_argument("--drop-base-parts", default="",
                        help="comma-separated NODE_NAMEs to omit from --base; "
                             "a face-slot import wants eyes,face,scalp,hair "
                             "gone or the stock head fights the imported one")
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
                           max_edge=args.max_edge,
                           drop_base_parts=set(
                               x.strip() for x in args.drop_base_parts.split(",") if x.strip()))
    print("wrote fullbody (%d imported vertices, %d faces%s)" %
          (verts, faces, ", composited over base" if args.base else ""))

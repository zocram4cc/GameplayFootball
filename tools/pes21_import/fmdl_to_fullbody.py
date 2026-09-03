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

# A mesh whose farthest vertex is this far out is scenery, not character:
# lcg_2718's backdrop reaches 362 m around a body 1.8 m tall. See the
# drop_stray handling in convert().
STRAY_DROP_RADIUS = 60.0

import ase_util
import retarget
import seams

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


# How far a vertex may sit from the joint its own bone mapping puts it on before
# the mapping is treated as a slot artifact rather than authoring. A hand's
# width: healthy skin binds a vertex to a joint it is practically touching.
STRAY_BINDING = 0.15


def rebind_stray(position, mapped, joint_positions):
    """-> True when a vertex's mapped joints are all far and a closer one exists.

    4cc packs a whole character into the *boots* slot, so the export's bone
    mapping is whatever that slot allows. Measured on the SMBG pack: the only
    leg bones k2587's mesh names at all are `sk_foot_l/r`, so its shins and
    knees are weighted to the feet - 785 vertices a side on the ankle and none
    on the knee. The knee then cannot bend and the leg swings rigidly from hip
    to foot, which on screen is a player sliding rather than striding.

    Nothing in that source says "knee", so there is no mapping to honour: the
    geometry's own position is the only truth about which joint owns it, and
    that is what nearest_joints reads. A healthy model is untouched, because
    its vertices sit on the joints they are mapped to.
    """
    if position is None or not mapped:
        return False
    names = {jid: name for name, jid in JOINT_ID.items()}
    best_mapped = min(
        (math.dist(position, joint_positions[names[jid]])
         for jid in mapped if names.get(jid) in joint_positions),
        default=None)
    if best_mapped is None or best_mapped <= STRAY_BINDING:
        return False
    nearest = min(math.dist(position, pos) for pos in joint_positions.values())
    return nearest < best_mapped


def vertex_joints(vertex, bone_to_joint, joint_positions=None):
    """-> [(jointID, weight)], the strongest MAX_INFLUENCES, normalized."""
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

    if rebind_stray(position, weights.keys(), joint_positions):
        return nearest_joints(position, joint_positions)

    top = sorted(weights.items(), key=lambda kv: -kv[1])[:MAX_INFLUENCES]
    total = sum(w for _, w in top)
    if total <= 0:
        if position is not None:
            return nearest_joints(position, joint_positions)
        return [(JOINT_ID["middle"], 1.0)]
    return [(j, w / total) for j, w in top]


# An influence this small is noise: the engine drops any channel decoding to
# <= 0.01 anyway, and keeping it costs one of the slots.
MIN_INFLUENCE = 0.02

# How many bones may drive one vertex. PES's own maximum, measured over the base
# package's parts (hand_l, hand_r, arm, glove_pl_short_l, eye, facial - 14,175
# vertices): the count of non-zero bone weights is 1, 2, 3 or 4 and never a fifth.
# The vertex colours can only carry three, so the fourth reaches the engine
# through the sidecar weight file (skinweights.hpp).
MAX_INFLUENCES = 4


def decode_color(color):
    """Three ASE colour channels -> [(jointID, weight)], as the engine reads them.

    The inverse of encode_color, and the engine's own arithmetic
    (humanoidbase.cpp): round the channel to 0..255, the joint is that over ten and
    the weight the remainder over nine, and what survives is renormalised. Used where
    a pass has to reason about weights that have already been written - reconciling a
    seam between two parts, where what matters is what the engine will actually skin
    with rather than what the source said.
    """
    joints = []
    for channel in color:
        raw = int(round(channel * 255))
        joint = raw // 10
        weight = (raw - joint * 10) / 9.0
        if weight > MIN_INFLUENCE:
            joints.append((joint, weight))
    total = sum(weight for _, weight in joints)
    if total <= 0.0:
        return []
    return [(joint, weight / total) for joint, weight in joints]


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

    A finger joint cannot be written at all - the encoding stops at joint 25 -
    so it collapses onto the wrist it hangs off, which is the hand this engine
    drew before the fingers were rigged. The real weights go in the sidecar
    (render_weights below); these colours are what is left for a model or an
    engine that has none.
    """
    collapsed = {}
    for joint, weight in joints:
        joint = retarget.colour_fallback_joint(joint)
        collapsed[joint] = collapsed.get(joint, 0.0) + weight
    kept = [(j, w) for j, w in collapsed.items() if w > MIN_INFLUENCE]
    kept.sort(key=lambda jw: (-jw[1], jw[0]))   # channel 0 carries the strongest
    kept = kept[:3]
    if not kept:
        # Every vertex must ride something: the engine asserts on channel 0.
        kept = [(retarget.colour_fallback_joint(joints[0][0]) if joints else 0, 1.0)]
    total = sum(w for _, w in kept)
    channels = []
    for j, w in kept:
        w = min(1.0, max(MIN_INFLUENCE, w / total))
        channels.append((j * 10 + w * 9.0) / 255.0)
    while len(channels) < 3:
        channels.append(0.0)
    return channels


WEIGHTS_HEADER = "# gfweights 1"


def render_weights(vertices):
    """[(position, [(jointID, weight)])] -> the sidecar text, or None.

    None when nothing in the model names a joint the vertex colours could not
    have carried anyway: a sidecar that says only what the .ase already says is
    another file to keep in step for no gain.

    Positions are written with the six decimals the ASE writers use, because
    the engine looks a weight up by exact float equality on the position - the
    same decimal text parses to the same float.
    """
    lines = [WEIGHTS_HEADER]
    seen = set()
    needed = False
    for position, joints in vertices:
        # a position already in the file's own decimal form passes through: a
        # composite merges the base body's lines with its own
        key = tuple(c if isinstance(c, str) else "%.6f" % c for c in position)
        if key in seen:
            continue
        seen.add(key)
        kept = sorted(((j, w) for j, w in joints if w > 0.0),
                      key=lambda jw: (-jw[1], jw[0]))[:MAX_INFLUENCES]
        total = sum(w for _, w in kept)
        if total <= 0.0:
            continue
        if any(joint >= len(retarget.GF_BODY_NODES) for joint, _ in kept) or \
                len(kept) > 3:
            needed = True
        lines.append(" ".join(key) + " " +
                     " ".join("%d:%.6f" % (j, w / total) for j, w in kept))
    if not needed:
        return None
    return "\n".join(lines) + "\n"


def weights_path(ase_path):
    """The sidecar that belongs to a model: <model>.ase -> <model>.weights."""
    root, ext = os.path.splitext(ase_path)
    return (root if ext else ase_path) + ".weights"


def read_weights(path):
    """The inverse of render_weights: [(position strings, [(jointID, weight)])].

    Positions come back as the file's own text, so merging two sidecars cannot
    move a vertex by a rounding step.
    """
    if not os.path.exists(path):
        return []
    out = []
    with open(path) as handle:
        first = handle.readline().strip()
        if first != WEIGHTS_HEADER:
            return []
        for line in handle:
            fields = line.split()
            if len(fields) < 4:
                continue
            joints = []
            for field in fields[3:]:
                joint, _, weight = field.partition(":")
                joints.append((int(joint), float(weight)))
            out.append((tuple(fields[:3]), joints))
    return out


def write_sidecar(ase_path, vertices, base_ase=None):
    """Writes <model>.weights beside `ase_path`; -> how many vertices it holds.

    `base_ase`: on a composite (--base) the stock body's geometry is carried
    over verbatim, so its weights have to come too or the body under the import
    loses its hands. The import's own vertices go first, so where the two cover
    the same place the import wins - it is the surface being drawn.

    Zero means no sidecar was needed, and any file left by a previous run is
    removed: skinning a changed mesh from stale weights is worse than skinning
    it from its colours.
    """
    merged = list(vertices)
    if base_ase:
        merged += read_weights(weights_path(base_ase))
    text = render_weights(merged)
    path = weights_path(ase_path)
    if text is None:
        if os.path.exists(path):
            os.remove(path)
        return 0
    open(path, "w").write(text)
    return len(text.splitlines()) - 1


def count_finger_lines(path):
    """-> how many lines of the written sidecar carry a finger joint.

    Read from the file rather than recomputed from the part list, because the two
    have already disagreed once: the list carries a seam corner once per part that
    meets there, and its copies can disagree about a weak finger influence that the
    top-four cut then drops. Whatever render_weights decided, this is what it wrote.
    """
    if not os.path.exists(path):
        return 0
    count = 0
    for line in open(path):
        if line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) < 4:
            continue
        if any(int(field.split(":")[0]) >= len(retarget.GF_BODY_NODES)
               for field in fields[3:]):
            count += 1
    return count


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


def select_meshes(meshes, max_tris, source_dir=None):
    """Which of the fmdl's meshes make the model: the visible character, whole.

    Drops the redundant (duplicate copies, PES passes this engine does not
    render) and the hidden (kit-hiding, below), and NEVER a visible mesh:
    dbg_2014's character is 154,799 faces over 11 meshes, and cutting it to a
    100,000-triangle budget amputated its legs.

    A character over `max_tris` is refused rather than trimmed. Shipping it
    whole regardless was the other way to be wrong: the engine skins every
    unique vertex of every player on the CPU each body tick and has no LOD, so
    an unbounded import quietly breaches a ceiling nothing downstream enforces.
    Raising the cap is a decision for whoever runs the import, not a silent
    default.

    Kit-hiding: a multi-form 4cc fmdl carries every character the player can
    be (dbg_2009: goku, vegeta, broly, roshi, jackiechun...), each form's
    meshes pointing at its own kit-slot texture (<char>_u0XXXp0). PES shows
    one form per kit by shipping the others' textures fully transparent. A
    mesh whose resolved texture is such a hider is not part of the model.
    Needs `source_dir` (the fmdl's folder) to resolve textures; without it,
    nothing is hidden.
    """
    seen = set()
    kept = []
    hider = {}
    for mesh in meshes:
        # PES ships extra copies of each mesh for passes this engine does not
        # render - the antiblur pass, and an outline shell sitting just outside
        # the body. Keeping them doubles the model and the copies z-fight.
        material = getattr(mesh, "materialInstance", None)
        name = mesh_base_texture(mesh)
        if is_non_render_pass(getattr(material, "name", ""), name):
            continue
        sig = _mesh_signature(mesh)
        if sig in seen:
            continue
        seen.add(sig)
        if source_dir and name:
            if name not in hider:
                path = find_texture_file(source_dir, name)
                hider[name] = bool(path) and texture_is_hider(path)
            if hider[name]:
                continue
        kept.append(mesh)
    hidden = sorted(name for name, hides in hider.items() if hides)
    if hidden:
        print("  kit-hiding: %d mesh(es) not in this form, dropped (%s)"
              % (sum(1 for m in meshes if mesh_base_texture(m) in hidden),
                 ", ".join(hidden)))
    total = sum(len(m.faces) for m in kept)
    if max_tris and total > max_tris:
        # Refuse, rather than either of the two ways this has been wrong before.
        # Amputating to fit cut dbg_2014's legs off and lcg_2709's head; shipping
        # whole regardless replaced that with a silent breach of the runtime's
        # ceiling, and the runtime has no LOD to absorb it: HumanoidBase CPU-skins
        # every unique vertex of every player each body tick, so two squads of
        # over-budget characters is hundreds of megabytes of vertex data skinned
        # per tick. Measured on the DBG pack, the biggest single character is
        # 212k triangles and a starting eleven is 1.83M, so a real 4cc model fits
        # the default below comfortably and only a pathological one trips this.
        #
        # Failing here makes that the operator's decision - raise the cap
        # deliberately, or decimate the source - instead of the importer quietly
        # choosing for them. import_team reports it as a failed import.
        raise ValueError(
            "%d faces exceeds the %d triangle budget. The character is not "
            "amputated to fit: either raise --max-tris knowing 22 of these are "
            "skinned per tick, or decimate the source mesh." % (total, max_tris))
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


# Fox Engine names a shader after the BRDF it uses, and the unlit one is Constant:
# fox3DFW_ConstantSRGB_NDR_Solid against fox3DDF_Blin_Fuzzblock, _GGX and
# _Blin_Translucent. That is the flat-shaded look the 4cc anime models are drawn in,
# and the pack says so itself rather than leaving it to be guessed.
SHADELESS_SHADER = "constant"


def is_shadeless(mesh):
    """Whether PES draws this mesh unlit."""
    material = getattr(mesh, "materialInstance", None)
    if material is None:
        return False
    for name in (getattr(material, "shader", "") or "",
                 getattr(material, "technique", "") or ""):
        if SHADELESS_SHADER in name.lower():
            return True
    return False


def material_block(texture, shadeless=False):
    """The ASE material for one mesh.

    An unlit mesh asks for full self-illumination, which is how this engine says the
    same thing: aseloader.cpp reads MATERIAL_SELFILLUM into materialparams.z,
    simple.frag writes it to the aux buffer, and the lighting pass takes it as the
    self-illumination factor.
    """
    return (MATERIAL_BLOCK % {"texture": texture}).replace(
        "*MATERIAL_SELFILLUM 0.0", "*MATERIAL_SELFILLUM 1.0" if shadeless else
        "*MATERIAL_SELFILLUM 0.0")


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


TEXTURE_EXTS = (".dds", ".png", ".tga")

# The bare kit-slot texture name (u0<team>p0; 4cc packs placeholder the team as
# XXX). It names no file anywhere: the engine swaps the team's kit into that
# slot at run time, so it must never be resolved to baked art - k2010 ships a
# u0XXXp1.dds that would otherwise be picked up.
BARE_KIT_SLOT_RE = re.compile(r"^u0\w{3}p0$", re.IGNORECASE)


def texture_name_candidates(name):
    """The file stems a mesh's texture name may ship under.

    Multi-form 4cc models dress each character in a per-character kit-slot
    texture (<char>_u0XXXp0); the pack ships the actual art per kit as
    <char>_u0XXXp1/2/3. The import is a single static model, so it wears
    kit 1. The bare kit slot itself stays with the engine (see above).
    """
    if BARE_KIT_SLOT_RE.match(name):
        return []
    names = [name]
    if name.lower().endswith("p0"):
        names.append(name[:-1] + "1")
    return names


def texture_search_dirs(source_dir):
    """Where a pack may keep a mesh's texture, most specific first.

    Beside the .fmdl; then the pack's Common/u0<team>p1/ (per-kit shared
    textures - hdg ships armor_bsm there and nothing beside the fmdl); then
    Common/ itself (dbg keeps its shared character art there, flat).
    The pack root is two levels up from the player folder (Boots/kNNNN/..).
    """
    dirs = [source_dir]
    common = os.path.join(os.path.dirname(os.path.dirname(source_dir)), "Common")
    if os.path.isdir(common):
        for entry in sorted(os.listdir(common)):
            if re.match(r"^u0\w{3}p1$", entry, re.IGNORECASE) and \
                    os.path.isdir(os.path.join(common, entry)):
                dirs.append(os.path.join(common, entry))
        dirs.append(common)
    return dirs


def find_texture_file(source_dir, name):
    """The file a mesh's texture name resolves to in this pack, or None."""
    candidates = [n.lower() for n in texture_name_candidates(name)]
    if not candidates:
        return None
    for directory in texture_search_dirs(source_dir):
        entries = sorted(os.listdir(directory))
        for wanted in candidates:
            for entry in entries:
                stem, ext = os.path.splitext(entry)
                if stem.lower() == wanted and ext.lower() in TEXTURE_EXTS:
                    return os.path.join(directory, entry)
    return None


# How opaque a texture may be and still count as a kit-hider. Measured, not
# guessed: all 26 hider textures in the DBG pack peak at alpha 2, and the
# nearest genuine artwork to the line is k2016's aura_scroll at 38. Anything
# in between is unclaimed, so the threshold sits low in the gap.
HIDER_MAX_ALPHA = 8


def texture_is_hider(path):
    """Whether this texture exists to hide its mesh: fully transparent.

    That is the 4cc kit-hiding convention - real art with alpha in it (dbg's
    logo is 59% opaque, kidbuu 57%) stays art. The tolerance is not defensive
    rounding: every hider in the DBG pack peaks at alpha 2 rather than 0.

    A texture that cannot be read is not treated as a hider, but it is said
    out loud. Swallowing the error silently turns kit-hiding off for a whole
    pack and reimports every multi-form character as a chimera, with a clean
    parse and a healthy mesh count reporting nothing wrong - which is the
    exact failure AGENTS.md exists to prevent.
    """
    try:
        from PIL import Image
        image = Image.open(path)
        image.load()
        alpha = image.convert("RGBA").getchannel("A")
        return alpha.getextrema()[1] <= HIDER_MAX_ALPHA
    except Exception as error:
        print("  cannot read %s (%s: %s); treating it as visible art, so any "
              "mesh it hides will be imported"
              % (os.path.basename(path), type(error).__name__, error))
        return False


def export_textures(fmdl_path, out_dir, names, prefix):
    """Writes each named source texture next to the model as a .png.

    The pack ships .dds - beside the .fmdl, or shared under the pack's
    Common/ (find_texture_file) - and the engine reads PNG (simple, editable
    formats through and through). Anything that cannot be found or decoded is
    reported and skipped, leaving that material pointing at a file the loader
    will complain about rather than silently texturing it wrong.
    """
    try:
        from PIL import Image
    except ImportError:
        print("PIL not available: textures not exported")
        return {}

    source_dir = os.path.dirname(os.path.abspath(fmdl_path))
    written = {}
    for name in sorted(n for n in names if n):
        found = find_texture_file(source_dir, name)
        if not found:
            print("  texture %-24s NOT FOUND in %s or its pack" % (name, source_dir))
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
            drop_base_parts=None, extra_fmdls=None, drop_stray=False):
    sys.path.insert(0, fmdl_lib)
    import FmdlFile
    fmdl = FmdlFile.FmdlFile()
    fmdl.readFile(fmdl_path)

    bone_to_joint = build_bone_map(fmdl)
    # fmdl vertices are authored in the RENDER bind; nearest-joint fallbacks
    # must measure against that pose, not the rig's anim-pose bind.
    joint_positions = retarget.gf_world_render_bind()

    meshes = select_meshes(fmdl.meshes, max_tris,
                           source_dir=os.path.dirname(os.path.abspath(fmdl_path)))

    # Other slots of the same character, merged in as further meshes.
    #
    # A 4cc body export is not always the whole body. DBG's pack keeps each
    # player's forearms, hands and all nineteen finger joints per side in
    # Gloves/glove_l.fmdl and glove_r.fmdl - 20,770 vertices each - and the
    # Boots export stops at the elbow. Imported alone it is a man with no hands
    # and no forearms, which is exactly how DBG has looked. The gloves are not
    # a separate model to the engine: they are more of this character, so they
    # join its mesh list and go through the same grouping, the same joint
    # binding and the same shard cut as the body's own meshes.
    for extra in (extra_fmdls or []):
        other = FmdlFile.FmdlFile()
        other.readFile(extra)
        # Bone tables are keyed by name and the two agree on theirs, so a
        # merge is a union; the gloves carry the finger bones the body lacks.
        bone_to_joint.update(build_bone_map(other))
        picked = select_meshes(other.meshes, None,
                              source_dir=os.path.dirname(os.path.abspath(extra)))
        print("merged %s: %d mesh(es), %d vertices"
              % (os.path.basename(extra), len(picked),
                 sum(len(m.vertices) for m in picked)))
        meshes = meshes + picked

    if only_meshes is not None:
        meshes = [m for i, m in enumerate(meshes) if i in only_meshes]


    if drop_stray:
        # A backdrop defines the model's bounds, and every view of the character
        # frames itself on the pair of them - lcg_2718's backdrop reaches 362 m
        # and frames the player down to a dot. A mesh whose farthest vertex is
        # tens of metres from a body whose standing height is 1.8 m is not part
        # of the character; PES draws such an export over its own body, and the
        # composite under it is what that body is.
        def stray(mesh):
            return max(
                math.sqrt(v.position.x ** 2 + v.position.y ** 2 + v.position.z ** 2)
                for v in mesh.vertices) > STRAY_DROP_RADIUS
        kept = [m for m in meshes if not stray(m)]
        if len(kept) != len(meshes):
            print("dropped %d stray mesh(es) past %.0f m"
                  % (len(meshes) - len(kept), STRAY_DROP_RADIUS))
        meshes = kept

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
    # Whether PES draws each group unlit, in step with `groups`.
    #
    # Grouped by texture *and* shading, not texture alone. Shading is per mesh and a
    # texture routinely carries both kinds: over 2HUG's 23 exports, 65 of 68 texture
    # groups mix lit and unlit meshes and not one is wholly unlit. Folding them
    # together loses the distinction whichever way it is resolved - every group unlit,
    # or none - so a texture whose meshes disagree becomes two materials.
    group_shadeless = []
    for mesh in meshes:
        name = mesh_base_texture(mesh) or ""
        shadeless = is_shadeless(mesh)
        key = (name, shadeless)
        if key not in group_of:
            group_of[key] = len(groups)
            groups.append([name, [], [], {}])
            group_shadeless.append(shadeless)
        _, vertices, faces, index = groups[group_of[key]]
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
                    vertices.append((pos, uv, color, skin))
                tri.append(index[key])
            # Fox winds clockwise-front (D3D); GF culls GL-style, so reverse.
            # (4cc exports double every mesh so they hid this; Konami's
            # single-sided originals do not.)
            faces.append((tri[0], tri[2], tri[1]))

    # A character is grouped by texture, and two groups that cover the same place -
    # a sleeve's texture against a torso's - are weighted independently, so where
    # they meet one comes through the other as soon as the joint between them turns
    # (seams.py). Reconciled before anything is cut or written, over the whole
    # character at once.
    if len(groups) > 1:
        before = [[(v[0], v[3]) for v in group[1]] for group in groups]
        agreed = seams.reconcile(before)
        changed, migrated = seams.reconciled_count(before, agreed)
        for group, blended in zip(groups, agreed):
            group[1] = [v[:2] + (encode_color(joints), joints)
                        for v, (_, joints) in zip(group[1], blended)]
        if changed:
            print("  seams: %d vertex weight(s) reconciled between groups, %d changed bone"
                  % (changed, migrated))

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
        # base_material_plan returns texture paths; the flag is keyed by group name.
        # appended is a list of texture paths; two groups can share one when their
        # shading differs, so the first occurrence decides. It costs nothing on the
        # composite path this branch serves: HDG's armour has no unlit mesh in it.
        shadeless_by_path = {}
        for index, group in enumerate(groups):
            shadeless_by_path.setdefault(group_texture_path(group[0]), group_shadeless[index])
        appended_shadeless = [shadeless_by_path.get(tex, False) for tex in appended]
        plate_materials = "".join(
            "\t*MATERIAL %d {\n%s\t}\n" % (base_material_count + i,
                                           material_block(tex, appended_shadeless[i]))
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
                out.write(material_block(group_texture_path(group[0]),
                                         group_shadeless[slot]))
                out.write("\t}\n")
            if not groups:
                out.write("\t*MATERIAL 0 {\n")
                out.write(material_block(texture, False))
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
            for i, (pos, _, _, _) in enumerate(vertices):
                out.write("\t\t\t*MESH_VERTEX %d\t%.6f\t%.6f\t%.6f\n"
                      % (i, pos[0], pos[1], pos[2]))
            out.write("\t\t}\n\t\t*MESH_FACE_LIST {\n")
            for i, (a, b, c) in enumerate(faces):
                out.write("\t\t\t*MESH_FACE %d: A: %d B: %d C: %d AB: 1 BC: 1 CA: 1 "
                      "*MESH_SMOOTHING 1 *MESH_MTLID 0\n" % (i, a, b, c))
            out.write("\t\t}\n")
            out.write("\t\t*MESH_NUMTVERTEX %d\n" % len(vertices))
            out.write("\t\t*MESH_TVERTLIST {\n")
            for i, (_, uv, _, _) in enumerate(vertices):
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
            for i, (_, _, color, _) in enumerate(vertices):
                out.write("\t\t\t*MESH_VERTCOL %d\t%.3f\t%.3f\t%.3f\n"
                      % (i, color[0], color[1], color[2]))
            out.write("\t\t}\n")
            out.write("\t\t*MESH_NUMCVFACES %d\n" % len(faces))
            out.write("\t\t*MESH_CFACELIST {\n")
            for i, (a, b, c) in enumerate(faces):
                out.write("\t\t\t*MESH_CFACE %d\t%d\t%d\t%d\n" % (i, a, b, c))
            out.write("\t\t}\n")
            gf_verts = [pos for (pos, _, _, _) in vertices]
            ase_util.write_mesh_normals(out, gf_verts, faces, smooth=True)
            out.write("\t}\n")
            out.write("\t*PROP_MOTIONBLUR 0\n\t*PROP_CASTSHADOW 1\n")
            out.write("\t*PROP_RECVSHADOW 1\n\t*MATERIAL_REF %d\n}\n"
                      % (group_refs[slot] if group_refs else slot))

    # The weights the vertex colours could not carry. PES weights a vertex to up
    # to four bones and the finger joints are past what a colour can name, so the
    # real weights ride a sidecar and the colours are the fallback (skinweights.hpp).
    sidecar = write_sidecar(
        ase_path,
        [(v[0], v[3]) for group in groups for v in group[1]],
        base_ase=base_ase)
    if sidecar:
        # The composite path is where the base body's fingers ride through, so say
        # whether they made it - assemble() reports the same for the stock body.
        print("  skin weights: %d vertex/vertices in %s, %d of them on a finger"
              % (sidecar, os.path.basename(weights_path(ase_path)),
                 count_finger_lines(weights_path(ase_path))))

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
                        help="advisory triangle budget: exceeding it is "
                             "reported, never amputated")
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
    parser.add_argument("--drop-stray", action="store_true",
                        help="drop meshes whose farthest vertex is more than "
                             "60 m out. lcg_2718's backdrop reaches 362 m and "
                             "frames the character down to a dot; such an "
                             "export is drawn over PES's own body, and the "
                             "backdrop has no place in that picture.")
    parser.add_argument("--extra", default="",
                        help="comma-separated further .fmdl of the same "
                             "character, merged in as more meshes. DBG's pack "
                             "keeps each player's forearms, hands and fingers "
                             "in Gloves/glove_l.fmdl and glove_r.fmdl while the "
                             "Boots export stops at the elbow.")
    args = parser.parse_args()
    verts, faces = convert(args.fmdl, args.out_dir, args.fmdl_lib, args.texture,
                           args.base, args.max_tris,
                           only_meshes=({int(x) for x in args.only_meshes.split(",") if x.strip()}
                                        if args.only_meshes else None),
                           force_joint=args.force_joint,
                           max_edge=args.max_edge,
                           drop_base_parts=set(
                               x.strip() for x in args.drop_base_parts.split(",") if x.strip()),
                           extra_fmdls=[x.strip() for x in args.extra.split(",") if x.strip()],
                           drop_stray=args.drop_stray)
    print("wrote fullbody (%d imported vertices, %d faces%s)" %
          (verts, faces, ", composited over base" if args.base else ""))

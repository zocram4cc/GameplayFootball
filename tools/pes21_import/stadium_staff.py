"""Imports the touchline staff PES keeps in its common package.

A stadium pack does not carry the people standing beside the pitch. PES keeps one
copy in Asset/model/bg/common/staff and hands it to every ground; the 4cc mod
replaces the skins, which is why the reference broadcast has characters on the
touchline rather than coaches in club coats. Planet Namek's own staff pack is one
of the 48-byte empty overrides, so without the common set there is nobody beside
the pitch at all.

The models are single-mesh, about 2.1 m tall and rigged, but they stand still, so
they import as static geometry in their bind pose - one ASE holding every figure,
already placed, which the engine loads beside the stadium:

    stadium_staff.py <staff pack dir> <out dir> --fmdl-lib <pes-fmdl dir>
                     --textures <where the skins are> [--per-side 4]

The skins are not beside the models: 4cc keeps them in the stadium pack's own
sourceimages (4cc_30_stadiums.cpk, Asset/model/bg/st002/sourceimages), which is
why they are named after teams - staff_doomyuri1, staff_lizard, staff_jkraptor.

Placement is the part worth getting right: the technical areas, outside the
touchline, facing the pitch. The .ase is text, so the marks can be moved by hand
afterwards.
"""

import argparse
import glob
import math
import os
import sys

import ase_util
import stadium_to_gf


# gametypes.hpp: x runs goal to goal, y touchline to touchline
PITCH_HALF_X = 55.0
PITCH_HALF_Y = 36.0
# How far outside the touchline the technical areas sit.
OUTSIDE = 1.8
# How far along the pitch they spread from the halfway line.
SPREAD = 11.0


def placements(pitch_half_x, pitch_half_y, per_side=4):
    """-> [(x, y, yaw)] marks for the staff, both touchlines, facing the pitch."""
    marks = []
    if per_side <= 0:
        return marks
    for side in (-1.0, 1.0):
        y = side * (pitch_half_y + OUTSIDE)
        # yaw is the engine's: a figure at yaw a faces (sin a, -cos a), so a
        # figure on the far touchline faces back down the -y axis and one on the
        # near touchline faces up it.
        yaw = 0.0 if side > 0 else math.pi
        for i in range(per_side):
            t = 0.5 if per_side == 1 else i / float(per_side - 1)
            x = -SPREAD + t * 2.0 * SPREAD
            marks.append((x, y, yaw))
    return marks


def footprint_offset(vertices):
    """-> (dx, dy) that brings a model's footprint onto its own origin.

    PES's staff models do not stand on theirs: their mesh sits up to four metres
    in front of it, which put half a row of figures on the pitch when the origin
    was dropped on the mark. Height is left alone - that is what keeps their feet
    on the ground.
    """
    if not vertices:
        return (0.0, 0.0)
    xs = [v[0] for v in vertices]
    ys = [v[1] for v in vertices]
    return (-0.5 * (min(xs) + max(xs)), -0.5 * (min(ys) + max(ys)))


def mark_for_depth(side, depth, pitch_half_y, margin=OUTSIDE):
    """-> the y a figure of this depth stands on, so its near edge clears the line.

    Centring every model on the same mark puts the deep ones - PES's touchline
    camera crane is metres long - over the touchline. Each is set out by its own
    half-depth instead.
    """
    sign = 1.0 if side >= 0 else -1.0
    return sign * (pitch_half_y + margin + max(0.0, depth) * 0.5)


def place_vertex(vertex, mark, yaw):
    """Turns a model-space vertex by `yaw` and drops it on `mark` (x, y)."""
    x, y, z = vertex
    sin_yaw, cos_yaw = math.sin(yaw), math.cos(yaw)
    return (mark[0] + x * cos_yaw - y * sin_yaw,
            mark[1] + x * sin_yaw + y * cos_yaw,
            z)


def _fox_to_gf(position):
    # the same mapping the stadium converter uses: (x, y, z) -> (x, -z, y)
    return (position.x, -position.z, position.y)


def dressed_first(models, skins, available):
    """Models whose skins the pack ships, first; the others after, order kept.

    A pack carries more staff than there is room on a touchline (st002: 61 models
    for 8 marks) and the alphabet is no way to choose between them: the first
    eight of st002's wear PES's stock coach skins, which no archive here carries,
    so twenty figures came out plain white while the ones the author dressed
    himself waited further down the list. Nobody is dropped - a white figure is
    still better than an empty technical area.
    """
    dressed, bare = [], []
    for model in models:
        wanted = skins.get(model, [])
        (dressed if wanted and all(skin in available for skin in wanted) else bare).append(model)
    return dressed + bare


def only_dressed(models, skins, available):
    """The models whose skins the pack ships, and nobody else.

    A figure with no skin renders as a white mannequin, and six of the nine
    grounds converted borrow PES's shared staff, whose stock coach kit is in none
    of the archives to hand - so they stood eight blank white figures beside the
    pitch in every wide shot. An empty technical area is the better of the two.
    """
    return [model for model in models
            if skins.get(model) and all(skin in available for skin in skins[model])]


def model_skins(path, fmdl_lib):
    """-> the base texture stems a staff model asks for, one per mesh."""
    fmdl = stadium_to_gf._load_fmdl(path, fmdl_lib)
    stems = []
    for mesh in fmdl.meshes:
        texture = stadium_to_gf._mesh_base_texture(mesh)
        if texture is not None:
            stems.append(stadium_to_gf._tex_stem(texture.filename))
    return stems


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("common", help="the extracted bg/common directory")
    parser.add_argument("out", help="where to write staff.ase, its .object and textures")
    parser.add_argument("--fmdl-lib", default=None)
    parser.add_argument("--per-side", type=int, default=4)
    parser.add_argument("--name", default="staff")
    parser.add_argument("--asset-dir", default=None,
                        help="where this will be installed under "
                             "media/objects/stadiums, for the .ase's own texture paths "
                             "(e.g. pes_st017/staff)")
    parser.add_argument("--textures", action="append", default=[],
                        help="where the skins live; 4cc keeps them in the stadium pack's "
                             "sourceimages rather than beside the models. Repeatable: PES's "
                             "own staff wear skins from its shared packs, a 4cc pack's wear "
                             "its own")
    args = parser.parse_args()

    if args.fmdl_lib and args.fmdl_lib not in sys.path:
        sys.path.insert(0, args.fmdl_lib)

    models = sorted(p for p in glob.glob(os.path.join(args.common, "**", "*.fmdl"), recursive=True)
                    if "/ad/" not in p.replace("\\", "/"))
    if not models:
        print("no staff models under %s" % args.common)
        return 1
    marks = placements(PITCH_HALF_X, PITCH_HALF_Y, args.per_side)
    print("%d staff model(s), %d mark(s)" % (len(models), len(marks)))

    texture_dirs = stadium_to_gf.find_texture_dirs(args.common, *args.textures)
    ftex_index = stadium_to_gf.build_ftex_index(texture_dirs)
    converted = {}

    # Only the ones we can dress go on the touchline, best first.
    skins = {path: model_skins(path, args.fmdl_lib) for path in models}
    dressed = only_dressed(dressed_first(models, skins, set(ftex_index)), skins, set(ftex_index))
    print("%d of %d model(s) have the skins they ask for" % (len(dressed), len(models)))
    if not dressed:
        print("nobody to dress: leaving the touchline empty rather than filling it with "
              "white figures")
        return 1
    models = dressed

    os.makedirs(args.out, exist_ok=True)
    figures = []  # (mesh, mark, yaw, bitmap)
    for index, (x, y, yaw) in enumerate(marks):
        path = models[index % len(models)]
        fmdl = stadium_to_gf._load_fmdl(path, args.fmdl_lib)
        for mesh in fmdl.meshes:
            texture = None
            for _role, tex in mesh.materialInstance.textures:
                texture = tex
                break
            bitmap = (stadium_to_gf._texture_png(texture, ftex_index, args.out, converted)
                      if texture else None)
            figures.append((mesh, (x, y), yaw, bitmap, os.path.basename(path)))

    ase_path = os.path.join(args.out, args.name + ".ase")
    with open(ase_path, "w") as out:
        stadium_to_gf._write_ase_header(out, args.name)
        out.write("*MATERIAL_LIST {\n\t*MATERIAL_COUNT %d\n" % len(figures))
        for i, (_mesh, _mark, _yaw, bitmap, source) in enumerate(figures):
            stadium_to_gf._write_material(out, i, "staff_%02d_%s" % (i, source.split(".")[0]),
                                          bitmap, args.asset_dir or args.name, None)
        out.write("}\n")
        for i, (mesh, mark, yaw, _bitmap, source) in enumerate(figures):
            _write_figure(out, "%s_%02d" % (args.name, i), i, mesh, mark, yaw)
    print("wrote %s: %d figure(s)" % (ase_path, len(figures)))

    object_path = os.path.join(args.out, args.name + ".object")
    open(object_path, "w").write(stadium_to_gf.object_text(args.name, with_pitch=False))
    print("wrote %s" % object_path)
    return 0


def _write_figure(out, name, material_index, mesh, mark, yaw, off_pitch=True,
                  on_ground=False):
    """Writes one figure into an ASE, standing on `mark`.

    off_pitch sets it out past the touchline by its own depth, which is what a
    coach or a ball boy wants and what nothing else does: the corner flags belong
    on the corners and a camera six metres behind a goal belongs there, not out by
    the halfway line (see stadium_props).

    on_ground sets it down on the grass. The staff stand on their own origin, but
    PES hangs some props off an attach point instead - mob_prop_camera00 runs from
    1.63 m below its origin to 0.09 above, so on the ground it would be buried.
    """
    local = [_fox_to_gf(v.position) for v in mesh.vertices]
    dx, dy = footprint_offset(local)
    centred = [(v[0] + dx, v[1] + dy, v[2]) for v in local]
    if on_ground and centred:
        lift = -min(v[2] for v in centred)
        centred = [(v[0], v[1], v[2] + lift) for v in centred]
    depth = max(v[1] for v in centred) - min(v[1] for v in centred)
    stand = ((mark[0], mark_for_depth(mark[1], depth, PITCH_HALF_Y)) if off_pitch
             else (mark[0], mark[1]))
    vertices = [place_vertex(v, stand, yaw) for v in centred]
    index_of = {id(v): i for i, v in enumerate(mesh.vertices)}
    faces = [(index_of[id(f.vertices[0])], index_of[id(f.vertices[1])], index_of[id(f.vertices[2])])
             for f in mesh.faces]
    uvs = [(v.uv[0].u, 1.0 - v.uv[0].v) if v.uv else (0.0, 0.0) for v in mesh.vertices]

    out.write("*GEOMOBJECT {\n")
    out.write('\t*NODE_NAME "%s"\n' % name)
    # The loader wants a transform block per object, and refuses the file
    # without one ("subtree NODE_TM not found"). The figures are already placed
    # in world space, so it is the identity.
    out.write("\t*NODE_TM {\n")
    out.write('\t\t*NODE_NAME "%s"\n' % name)
    out.write("\t\t*INHERIT_POS 0 0 0\n\t\t*INHERIT_ROT 0 0 0\n\t\t*INHERIT_SCL 0 0 0\n")
    out.write("\t\t*TM_ROW0 1.0\t0.0\t0.0\n\t\t*TM_ROW1 0.0\t1.0\t0.0\n")
    out.write("\t\t*TM_ROW2 0.0\t0.0\t1.0\n\t\t*TM_ROW3 0.0\t0.0\t0.0\n\t}\n")
    out.write("\t*MESH {\n\t\t*TIMEVALUE 0\n")
    out.write("\t\t*MESH_NUMVERTEX %d\n\t\t*MESH_NUMFACES %d\n" % (len(vertices), len(faces)))
    out.write("\t\t*MESH_VERTEX_LIST {\n")
    for i, v in enumerate(vertices):
        out.write("\t\t\t*MESH_VERTEX %d\t%.4f\t%.4f\t%.4f\n" % (i, v[0], v[1], v[2]))
    out.write("\t\t}\n\t\t*MESH_FACE_LIST {\n")
    for i, f in enumerate(faces):
        out.write("\t\t\t*MESH_FACE %d:    A: %d B: %d C: %d AB: 1 BC: 1 CA: 1\t "
                  "*MESH_SMOOTHING 1 \t*MESH_MTLID 0\n" % (i, f[0], f[1], f[2]))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMTVERTEX %d\n\t\t*MESH_TVERTLIST {\n" % len(uvs))
    for i, uv in enumerate(uvs):
        out.write("\t\t\t*MESH_TVERT %d\t%.6f\t%.6f\t0.0000\n" % (i, uv[0], uv[1]))
    out.write("\t\t}\n")
    out.write("\t\t*MESH_NUMTVFACES %d\n\t\t*MESH_TFACELIST {\n" % len(faces))
    for i, f in enumerate(faces):
        out.write("\t\t\t*MESH_TFACE %d\t%d\t%d\t%d\n" % (i, f[0], f[1], f[2]))
    out.write("\t\t}\n")
    ase_util.write_mesh_normals(out, vertices, faces, smooth=True)
    out.write("\t}\n")
    out.write("\t*MATERIAL_REF %d\n" % material_index)
    out.write("}\n")


if __name__ == "__main__":
    sys.exit(main())

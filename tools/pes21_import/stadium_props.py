"""Imports the furniture PES stands around a pitch.

A stadium pack ships none of it: every 4cc pack's audi, cheer, standsFlag,
scarecrow and tv sub-pack is a 48-byte stub, because PES keeps one set in its own
packages and hands it to every ground. So the corner flags, the fourth official's
board, the television cameras, the barrier at the tunnel mouth and the paramedics
all live in base PES21 - dt12_g4.cpk, common/demo/prop and
common/demo/fixdemoobj - and this engine has never had any of them. There is not
even a corner flag in media/objects.

What can be taken is decided by whether the skins are installed. These are, and
they are small: gadget_cornerflag is 263 vertices, substitute_board_cmn 136,
mob_prop_tvcamera01 3191, doh_beltpole 1085 over 25 metres. PES's stock-kit
humans - the stewards, the press, the television crew - ask for clothing textures
no archive here carries (the same gap as the staff coach kit), so they are left
out rather than stood beside the pitch as white figures.

Placement is this module's. PES positions its props from demo data authored per
stadium, and what carries over is where they belong on a football pitch: corner
flags on the corners, cameras outside the perimeter looking in, the board and the
paramedics in the technical areas, the barrier along the tunnel side - which for
this engine is -y, the touchline the walk-on comes in over.

    stadium_props.py <extracted PES prop dir> <out dir> [--asset-dir pes_st017/props]

writes props.ase, props.object and its textures, which the engine loads beside the
stadium the way it loads staff/staff.object.
"""

import argparse
import glob
import math
import os
import sys

import ase_util
import stadium_staff
import stadium_crowd  # noqa: E402
import stadium_to_gf

# gametypes.hpp
PITCH_HALF_X = 55.0
PITCH_HALF_Y = 36.0
# How far outside the lines the furniture stands.
OUTSIDE = 2.5
# Behind a goal line, where a camera and its operator have room.
BEHIND_GOAL = 6.0

# The props a match has, by the file name PES gives them. Named rather than swept
# up: the same package holds the cup ceremony, the press-room gadgets and the
# league trophies, none of which belong on a pitch at kickoff.
WANTED = (
    "gadget_cornerflag",
    "substitute_board_cmn",
    "mob_prop_tvcamera00", "mob_prop_tvcamera01", "mob_prop_tvcamera02",
    "mob_prop_camera00", "mob_prop_camera01",
    "item_videocamera00",
    "doh_beltpole",
    "dm_medicalstaff_01", "dm_medicalstaff_02",
)

# Which mark each of them stands on.
ROLES = (
    ("cornerflag", ("gadget_cornerflag",)),
    ("camera", ("mob_prop_tvcamera", "mob_prop_camera", "item_videocamera")),
    ("bench", ("substitute_board", "dm_medicalstaff")),
    ("barrier", ("doh_beltpole",)),
)


# What PES carries out for the walkout and takes away again: the flag bearers and
# their banners, the arch over the tunnel mouth, the pennant display on the centre
# circle, and the tunnel itself. None of it belongs on the pitch once the match
# starts, so the engine drops the whole set at kickoff.
ENTRANCE_WANTED = (
    "doh_fb_home", "doh_fb_away",
    "banner_nationalflag_home", "banner_nationalflag_away", "banner_euro_competition",
    "tunnelarch_uefa_euro", "tunnelarch_afc_cl",
)

# The ring of pennant bearers, written once per competition emblem: which badge it
# flies depends on who is playing, so the engine loads the one the tie calls for
# (src/onthepitch/competitionemblem.hpp).
PENNANT_WANTED = ("circleflag_afc_cl_01",)

# The pennant ring's flag faces, as opposed to the bearers carrying them: PES's
# circleflag_afc_cl_01 is four of each, and the face is where a competition puts
# its own emblem. The 4cc mod does exactly that to its UEFA slot, where
# circleflag_uefa_cl_0_bsm carries a Winter Cup badge.
PENNANT_FACE_MARKS = ("circlef", "circleflag")

# The tunnel interior. PES's passages are 48 x 61 m rooms authored for their own
# ground's mouth; dropped at ours the wall filled the entrance camera's frame for
# the whole walk-on, so it takes --tunnel and a ground whose mouth it fits.
TUNNEL_EXTRAS = ("passage_01",)

# Nothing extra for a competition tie: the pennant ring is in the walkout set
# itself, and which emblem it carries is --emblem's business.
COMPETITION_EXTRAS = ()

ENTRANCE_ROLES = (
    ("flagbearer", ("doh_fb_", "doh_mfb_")),
    ("banner", ("banner_",)),
    ("arch", ("tunnelarch",)),
    ("pennant", ("circleflag",)),
    ("tunnel", ("passage",)),
)

# The tunnel mouth: where the walk-on comes in over the near touchline, which
# StagingAnchor puts four metres outside it at the halfway line.
MOUTH_Y = -(PITCH_HALF_Y + 4.0)


def placeholder_bitmap(texture_name, model_name):
    """-> the engine's own team cloth for a PES placeholder, or None for artwork.

    PES's doh_fb_home/away and its tunnel arches all reference sys_zero_bsm, which
    is not a texture so much as a slot: PES paints the club's own flag there at run
    time, and what Konami left in the file is the flag of the United States for the
    bearers and the FC Barcelona crest for the arch. Worse, textures are keyed by
    bare filename, so one sys_zero_bsm.png per stadium was shared between every
    model that names it and whichever converted last decided what the walkout
    carried. The engine paints these instead (src/onthepitch/teamflag.hpp).
    """
    if not stadium_crowd.is_placeholder_texture(texture_name):
        return None
    stem = os.path.splitext(os.path.basename(str(model_name)))[0].lower()
    side = "away" if "away" in stem else "home"
    return stadium_crowd.TEAM_FLAG_BITMAPS[side]


def is_pennant_face(texture_name):
    """Whether a pennant mesh is a flag face - what carries the competition emblem.

    The bearers holding them keep their own kit: on PES's ring the faces are
    acl_circlef_prop000_nomip_bsm and the bearers acl_circlef_prop001_bsm.
    """
    if not texture_name:
        return False
    stem = os.path.splitext(os.path.basename(str(texture_name)))[0].lower()
    if "prop001" in stem or "staff" in stem:
        return False
    return any(mark in stem for mark in PENNANT_FACE_MARKS)


# How much of the banner is the badge, and how much of it we hand to the
# competition's own. PES's face is a hex-patterned navy disc filling the square
# with its badge across the middle 60% of it; wiping a little wider than that
# clears the AFC mark, and the emblem then covers most of what was wiped.
FACE_WIPE_RADIUS = 0.43     # of the texture's width, from the centre
FACE_FIELD_SAMPLE = (0.45, 0.49)   # the annulus the field's own colour comes from
FACE_EMBLEM_SPAN = 0.62     # the emblem's longest side, of the texture's width
FACE_WIPE_FEATHER = 0.04    # softens the seam so there is no ring around the badge


def compose_flag_face(base, emblem):
    """-> PES's banner carrying a different competition's emblem.

    The centre-circle banner is not a badge on its own: it is a navy disc with a
    hexagon pattern and the competition's mark across the middle. Handing the
    engine a bare UI emblem instead gave it a mostly transparent PNG - and this
    material does not blend alpha - so the banner came out a dark blob on the
    centre spot. So the middle of PES's own face is wiped to the field's colour,
    the emblem painted over it, and the rim left exactly as PES drew it.

    The hexagons inside the wipe are lost; they are invisible from any camera
    that films a flat banner on the pitch, and the alternative is repainting
    PES's artwork rather than dressing it.
    """
    from PIL import Image

    base = base.convert("RGBA")
    if emblem is None:
        return base

    width, height = base.size
    span = float(min(width, height))
    centre = ((width - 1) / 2.0, (height - 1) / 2.0)

    field = _field_colour(base, centre, span)
    face = base.copy()
    face.paste(Image.new("RGBA", base.size, field),
               (0, 0), _wipe_mask(base.size, centre, span))

    emblem = emblem.convert("RGBA")
    fitted = int(round(span * FACE_EMBLEM_SPAN))
    scale = fitted / float(max(emblem.size))
    emblem = emblem.resize((max(1, int(round(emblem.size[0] * scale))),
                            max(1, int(round(emblem.size[1] * scale)))),
                           Image.LANCZOS)
    face.alpha_composite(emblem, (int(round(centre[0] - emblem.size[0] / 2.0)),
                                  int(round(centre[1] - emblem.size[1] / 2.0))))
    return face


def _field_colour(base, centre, span):
    """-> the banner's own colour, from a ring between its badge and its rim."""
    inner, outer = (r * span for r in FACE_FIELD_SAMPLE)
    channels = ([], [], [])
    pixels = base.load()
    for y in range(base.size[1]):
        for x in range(base.size[0]):
            distance = ((x - centre[0]) ** 2 + (y - centre[1]) ** 2) ** 0.5
            if not inner <= distance <= outer:
                continue
            pixel = pixels[x, y]
            if pixel[3] < 128:      # outside the disc, if this face has one
                continue
            for i in range(3):
                channels[i].append(pixel[i])
    if not channels[0]:
        return (0, 0, 0, 255)
    return tuple(sorted(c)[len(c) // 2] for c in channels) + (255,)


def _wipe_mask(size, centre, span):
    """-> how much of each pixel the field colour takes: all of the middle, none
    of the rim, and a feathered few per cent in between."""
    from PIL import Image

    mask = Image.new("L", size, 0)
    solid = span * (FACE_WIPE_RADIUS - FACE_WIPE_FEATHER)
    edge = span * FACE_WIPE_RADIUS
    pixels = mask.load()
    for y in range(size[1]):
        for x in range(size[0]):
            distance = ((x - centre[0]) ** 2 + (y - centre[1]) ** 2) ** 0.5
            if distance <= solid:
                pixels[x, y] = 255
            elif distance < edge:
                pixels[x, y] = int(round(255.0 * (edge - distance) / (edge - solid)))
    return mask


def _composed_face(base_rel, emblem_image, emblem_stem, out_dir, composed):
    """Writes PES's banner with this competition's emblem on it; -> its path."""
    from PIL import Image

    key = (base_rel, emblem_stem)
    if key in composed:
        return composed[key]
    stem = os.path.splitext(os.path.basename(base_rel))[0]
    out_rel = "textures/%s__%s.png" % (stem, emblem_stem)
    with Image.open(os.path.join(out_dir, base_rel)) as base:
        face = compose_flag_face(base, emblem_image)
    face.convert("RGB").save(os.path.join(out_dir, out_rel))
    print("  banner face: %s <- %s + %s" % (out_rel, base_rel, emblem_stem))
    composed[key] = out_rel
    return out_rel


def entrance_role(path):
    """-> what a walkout prop is ('flagbearer', 'banner', 'arch', 'pennant',
    'tunnel'), or None."""
    stem = os.path.splitext(os.path.basename(str(path)))[0].lower()
    for role, prefixes in ENTRANCE_ROLES:
        if any(stem.startswith(prefix) for prefix in prefixes):
            return role
    return None


def marks_for_entrance(role, pitch_half_x=PITCH_HALF_X, pitch_half_y=PITCH_HALF_Y):
    """-> where a walkout prop stands."""
    mouth = -(pitch_half_y + 4.0)
    if role == "flagbearer":
        # either side of the mouth, facing the pitch the cast walks onto
        return [(sx * 6.0, mouth - 1.0, _facing(sx * 6.0, mouth - 1.0)) for sx in (-1.0, 1.0)]
    if role == "banner":
        return [(sx * 3.0, mouth - 2.5, _facing(sx * 3.0, mouth - 2.5)) for sx in (-1.0, 1.0)]
    if role == "arch":
        return [(0.0, mouth - 1.5, _facing(0.0, mouth - 1.5))]
    if role == "pennant":
        # the display PES sets on the centre circle for the team picture
        return [(0.0, 0.0, math.pi)]
    if role == "tunnel":
        # behind the mouth, where the entrance camera starts before it comes out
        return [(0.0, mouth - 26.0, math.pi)]
    return []


def prop_role(path):
    """-> what a prop is for ('cornerflag', 'camera', 'bench', 'barrier'), or None."""
    stem = os.path.splitext(os.path.basename(str(path)))[0].lower()
    for role, prefixes in ROLES:
        if any(stem.startswith(prefix) for prefix in prefixes):
            return role
    return None


def _facing(x, y):
    """-> the yaw that turns something at (x, y) towards the centre spot.

    The engine's convention: a figure at yaw a faces (sin a, -cos a).
    """
    return math.atan2(-x, y)


def corner_flag_marks(pitch_half_x, pitch_half_y):
    """-> the four corners. A corner flag stands on the line, by the laws."""
    return [(sx * pitch_half_x, sy * pitch_half_y, _facing(sx * pitch_half_x, sy * pitch_half_y))
            for sx in (-1.0, 1.0) for sy in (-1.0, 1.0)]


def camera_marks(pitch_half_x, pitch_half_y, outside=OUTSIDE):
    """-> where the cameras stand: behind each goal, and the main one opposite.

    All outside the field and all looking at the middle of it.
    """
    marks = []
    for sx in (-1.0, 1.0):
        x = sx * (pitch_half_x + BEHIND_GOAL)
        marks.append((x, 0.0, _facing(x, 0.0)))
    y = pitch_half_y + outside + 1.5  # the broadcast side, away from the tunnel
    marks.append((0.0, y, _facing(0.0, y)))
    return marks


def technical_area_marks(pitch_half_x, pitch_half_y, outside=OUTSIDE):
    """-> the two technical areas, either side of the halfway line, tunnel side."""
    y = -(pitch_half_y + outside)
    return [(sx * 12.0, y, _facing(sx * 12.0, y)) for sx in (-1.0, 1.0)]


def barrier_mark(pitch_half_x, pitch_half_y, outside=OUTSIDE):
    """-> the barrier at the tunnel mouth, along the touchline it comes in over."""
    y = -(pitch_half_y + outside + 0.8)
    return (0.0, y, _facing(0.0, y))


def marks_for_role(role, pitch_half_x=PITCH_HALF_X, pitch_half_y=PITCH_HALF_Y):
    if role == "cornerflag":
        return corner_flag_marks(pitch_half_x, pitch_half_y)
    if role == "camera":
        return camera_marks(pitch_half_x, pitch_half_y)
    if role == "bench":
        return technical_area_marks(pitch_half_x, pitch_half_y)
    if role == "barrier":
        return [barrier_mark(pitch_half_x, pitch_half_y)]
    return []


def prop_placement(meshes):
    """-> how to place every mesh of one prop: {"dx", "dy", "lift"}.

    A prop is not one mesh. gadget_cornerflag is four - pole, ground disc, and the
    cloth in two pieces - and placing each on its own is what put the flag on the
    floor: the cloth is authored at z 1.28..1.62, where it hangs off the top of a
    1.62 m pole, and setting *it* down on the grass dropped it by 1.28 m. The
    footprint centring did the same sideways, pushing a flag that hangs to one side
    of the pole back onto the pole's axis.

    So both are measured once over every vertex the prop has, and every one of its
    meshes is placed with that: the pole still stands on the grass, and the flag keeps
    its height and its overhang.
    """
    everything = [v for mesh in meshes for v in mesh]
    if not everything:
        return {"dx": 0.0, "dy": 0.0, "lift": 0.0}
    dx, dy = stadium_staff.footprint_offset(everything)
    lift = -min(v[2] for v in everything)
    return {"dx": dx, "dy": dy, "lift": lift}


def place_prop_mesh(vertices, placement):
    """One mesh's vertices moved by its prop's shared placement."""
    return [(v[0] + placement["dx"], v[1] + placement["dy"], v[2] + placement["lift"])
            for v in vertices]


def write_prop(out, name, material_index, mesh, mark, yaw, placement=None):
    """Writes one piece of furniture into an ASE, standing exactly on its mark.

    `placement` is the prop's own, shared by every mesh in it (prop_placement). Left
    out, the mesh is placed on its own, which is right only for a prop that is one
    mesh.
    """
    stadium_staff._write_figure(out, name, material_index, mesh, mark, yaw, off_pitch=False,
                                on_ground=True, placement=placement)


def dressed_meshes(dressed):
    """-> the indices of the meshes worth drawing, given which are dressed.

    A prop with one placeholder panel is still worth having: PES's tunnel arch
    carries two textured meshes and two on dummy_embA/embH, the placeholders it
    swaps for the two teams' emblems. Judged all-or-nothing the arch was left
    behind; judged mesh by mesh it arrives without its blank panels.
    """
    return [index for index, ok in enumerate(dressed) if ok]


def assign(models, marks):
    """-> [(model, mark)], one prop per mark, sharing the models out over them.

    Taking every model to every mark stood a broadcast camera, a hand camera and a
    video camera on the same spot behind each goal.
    """
    if not models or not marks:
        return []
    return [(models[i % len(models)], mark) for i, mark in enumerate(marks)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("props", help="a directory of extracted PES prop models")
    parser.add_argument("out", help="where to write props.ase, its .object and textures")
    parser.add_argument("--fmdl-lib", default=None)
    parser.add_argument("--name", default="props")
    parser.add_argument("--asset-dir", default=None,
                        help="where this will be installed under media/objects/stadiums, "
                             "for the .ase's own texture paths (e.g. pes_st017/props)")
    parser.add_argument("--textures", action="append", default=[])
    parser.add_argument("--emblem", default=None,
                        help="a PNG for the pennant ring's flag faces: the competition's "
                             "own emblem, which for these packs is emblemLc/emb_0004 (the "
                             "4chan Stupor Cup clover) or emb_0008 (the /vg/ Football League)")
    parser.add_argument("--tunnel", action="store_true",
                        help="add PES's tunnel interior behind the mouth; it is a 48 x 61 m "
                             "room authored for another ground and will block the shot "
                             "unless it happens to fit")
    parser.add_argument("--competition", action="store_true",
                        help="add PES's continental dressing: the ring of pennant holders "
                             "it sets on the centre circle for a competition tie")
    parser.add_argument("--set", choices=("touchline", "entrance", "pennant"),
                        default="touchline",
                        help="the furniture that stays out all match, what PES carries out "
                             "for the walkout and takes away again, or the pennant ring on "
                             "its own (once per competition emblem)")
    args = parser.parse_args()

    wanted = {"touchline": WANTED, "entrance": ENTRANCE_WANTED,
              "pennant": PENNANT_WANTED}[args.set]
    if args.set == "entrance" and args.competition:
        wanted = wanted + COMPETITION_EXTRAS
    if args.set != "touchline" and args.tunnel:
        wanted = wanted + TUNNEL_EXTRAS
    role_of = prop_role if args.set == "touchline" else entrance_role
    marks_of = marks_for_role if args.set == "touchline" else marks_for_entrance

    if args.fmdl_lib and args.fmdl_lib not in sys.path:
        sys.path.insert(0, args.fmdl_lib)

    models = {}
    for path in sorted(glob.glob(os.path.join(args.props, "**", "*.fmdl"), recursive=True)):
        stem = os.path.splitext(os.path.basename(path))[0]
        if stem in wanted:
            models.setdefault(stem, path)
    if not models:
        print("none of the props we want are under %s" % args.props)
        return 1

    texture_dirs = stadium_to_gf.find_texture_dirs(args.props, *args.textures)
    ftex_index = stadium_to_gf.build_ftex_index(texture_dirs)
    converted = {}
    os.makedirs(args.out, exist_ok=True)

    # The competition's own emblem. It is not used as the banner - a bare UI badge
    # is transparent where the banner is navy - but painted onto PES's own face by
    # compose_flag_face, once per face texture the ring turns out to use.
    emblem_image = None
    emblem_stem = None
    if args.emblem:
        from PIL import Image
        emblem_image = Image.open(args.emblem).convert("RGBA")
        emblem_stem = os.path.splitext(os.path.basename(args.emblem))[0]
        print("  pennant emblem: %s (%dx%d)" % (os.path.basename(args.emblem),
                                                emblem_image.size[0], emblem_image.size[1]))
    composed = {}

    figures = []  # (mesh, mark, yaw, bitmap, source, engine's own cloth or None)
    skipped = []
    # Which models can play which part, and then one of them per mark.
    by_role = {}
    for stem in sorted(models):
        path = models[stem]
        role = role_of(path)
        if not role:
            continue
        fmdl = stadium_to_gf._load_fmdl(path, args.fmdl_lib)
        skins = [stadium_to_gf._mesh_base_texture(mesh) for mesh in fmdl.meshes]
        keep = dressed_meshes([t is not None and stadium_to_gf._tex_stem(t.filename) in ftex_index
                               for t in skins])
        if not keep:
            # An undressed prop is a white shape beside the pitch; better none.
            skipped.append(stem)
            continue
        if len(keep) < len(skins):
            print("  %-24s %d of %d mesh(es) undressed, left off"
                  % (stem, len(skins) - len(keep), len(skins)))
        meshes = [fmdl.meshes[i] for i in keep]
        by_role.setdefault(role, []).append((stem, meshes, [skins[i] for i in keep]))

    for role in sorted(by_role):
        marks = marks_of(role)
        for (stem, meshes, skins), mark in assign(by_role[role], marks):
            # One placement for the whole prop. Measured per mesh, a corner flag's
            # cloth is set down on the grass and centred on the pole - which is how
            # the flag came out lying on the floor beside it.
            placement = prop_placement(
                [[stadium_staff._fox_to_gf(v.position) for v in mesh.vertices]
                 for mesh in meshes])
            for mesh, texture in zip(meshes, skins):
                ident = getattr(texture, "filename", None) if texture else None
                own = placeholder_bitmap(ident, stem)
                if own:
                    # PES's run-time slot, not artwork: the engine paints the team's
                    # badge on it rather than us shipping Konami's stand-in.
                    figures.append((mesh, (mark[0], mark[1]), mark[2], None, stem, own,
                                    placement))
                    continue
                bitmap = stadium_to_gf._texture_png(texture, ftex_index, args.out, converted)
                # The competition's emblem goes on the pennant faces, and only on
                # them: the bearers holding them keep their own kit.
                if emblem_image and bitmap and is_pennant_face(
                        getattr(texture, "filename", None)):
                    bitmap = _composed_face(bitmap, emblem_image, emblem_stem,
                                            args.out, composed)
                figures.append((mesh, (mark[0], mark[1]), mark[2], bitmap, stem, None,
                                placement))
        print("  %-10s %d mark(s) from %d model(s): %s"
              % (role, len(marks), len(by_role[role]),
                 ", ".join(m[0] for m in by_role[role])))

    if skipped:
        print("  left out for want of their skins: %s" % ", ".join(skipped))
    if not figures:
        print("nothing to place")
        return 1

    ase_path = os.path.join(args.out, args.name + ".ase")
    with open(ase_path, "w") as out:
        stadium_to_gf._write_ase_header(out, args.name)
        out.write("*MATERIAL_LIST {\n\t*MATERIAL_COUNT %d\n" % len(figures))
        for i, (_mesh, _mark, _yaw, bitmap, source, own, _place) in enumerate(figures):
            # `own` is the engine's own cloth for one of PES's run-time slots; it is
            # already a path under media/, so it goes in as the fallback rather than
            # being prefixed with the stadium's asset directory.
            stadium_to_gf._write_material(out, i, "prop_%02d_%s" % (i, source), bitmap,
                                          args.asset_dir or args.name, own)
        out.write("}\n")
        for i, (mesh, mark, yaw, _bitmap, source, _own, place) in enumerate(figures):
            write_prop(out, "%s_%02d_%s" % (args.name, i, source), i, mesh, mark, yaw,
                       placement=place)
    print("wrote %s: %d piece(s) of furniture" % (ase_path, len(figures)))

    object_path = os.path.join(args.out, args.name + ".object")
    open(object_path, "w").write(stadium_to_gf.object_text(args.name, with_pitch=False))
    print("wrote %s" % object_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())

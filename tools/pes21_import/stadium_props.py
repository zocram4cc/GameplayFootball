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


def write_prop(out, name, material_index, mesh, mark, yaw):
    """Writes one piece of furniture into an ASE, standing exactly on its mark."""
    stadium_staff._write_figure(out, name, material_index, mesh, mark, yaw, off_pitch=False)


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
    args = parser.parse_args()

    if args.fmdl_lib and args.fmdl_lib not in sys.path:
        sys.path.insert(0, args.fmdl_lib)

    models = {}
    for path in sorted(glob.glob(os.path.join(args.props, "**", "*.fmdl"), recursive=True)):
        stem = os.path.splitext(os.path.basename(path))[0]
        if stem in WANTED:
            models.setdefault(stem, path)
    if not models:
        print("none of the props we want are under %s" % args.props)
        return 1

    texture_dirs = stadium_to_gf.find_texture_dirs(args.props, *args.textures)
    ftex_index = stadium_to_gf.build_ftex_index(texture_dirs)
    converted = {}
    os.makedirs(args.out, exist_ok=True)

    figures = []  # (mesh, mark, yaw, bitmap, source)
    skipped = []
    # Which models can play which part, and then one of them per mark.
    by_role = {}
    for stem in sorted(models):
        path = models[stem]
        role = prop_role(path)
        if not role:
            continue
        fmdl = stadium_to_gf._load_fmdl(path, args.fmdl_lib)
        skins = [stadium_to_gf._mesh_base_texture(mesh) for mesh in fmdl.meshes]
        if any(t is None or stadium_to_gf._tex_stem(t.filename) not in ftex_index for t in skins):
            # An undressed prop is a white shape beside the pitch; better none.
            skipped.append(stem)
            continue
        by_role.setdefault(role, []).append((stem, fmdl, skins))

    for role in sorted(by_role):
        marks = marks_for_role(role)
        for (stem, fmdl, skins), mark in assign(by_role[role], marks):
            for mesh, texture in zip(fmdl.meshes, skins):
                bitmap = stadium_to_gf._texture_png(texture, ftex_index, args.out, converted)
                figures.append((mesh, (mark[0], mark[1]), mark[2], bitmap, stem))
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
        for i, (_mesh, _mark, _yaw, bitmap, source) in enumerate(figures):
            stadium_to_gf._write_material(out, i, "prop_%02d_%s" % (i, source), bitmap,
                                          args.asset_dir or args.name, None)
        out.write("}\n")
        for i, (mesh, mark, yaw, _bitmap, source) in enumerate(figures):
            write_prop(out, "%s_%02d_%s" % (args.name, i, source), i, mesh, mark, yaw)
    print("wrote %s: %d piece(s) of furniture" % (ase_path, len(figures)))

    object_path = os.path.join(args.out, args.name + ".object")
    open(object_path, "w").write(stadium_to_gf.object_text(args.name, with_pitch=False))
    print("wrote %s" % object_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())

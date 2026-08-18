"""Seats PES's own crowd in an imported stadium.

Every stadium pack carries its stands in audi/audiarea.bin - sloped quads plus the
row spacing the game itself uses, 1.9 m in st060, 2.1 in st002, 0.7-0.9 in st011 -
but no spectators: like the staff and the touchline furniture, PES keeps one set
and hands it to every ground. They are in dt00_x64.cpk under
Asset/model/bg/common/audi: au_Low.fmdl is a spectator of 672 vertices,
au00..au16_parts the variants, each with a mouthOpen version for when the crowd is
singing, and audi_seat_model the seats they sit on.

Counted from those files, st041 has about 13,800 seats, st011 11,900 and st060
5,100. Placed as separate meshes that is over a million vertices in a text ASE,
which the loader spends minutes on. So each spectator model is written once and its
seats beside it as a placement list, and the engine draws one mesh many times
(src/utils/instancelist.hpp, one draw call per 256 seats).

    stadium_crowd.py <pack dir> --out <stadium dir> --models <extracted bg/common/audi>

writes crowd/crowd_NN.ase, crowd_NN.instances and crowd.object.
"""

import argparse
import glob
import math
import os
import sys

import crowd_gen
import stadium_to_gf

# How the seats are laid out. PES's own row spacing comes from audiarea.bin; the
# spacing along a row is not in there, and half a metre is a seat.
SEAT_STEP = 0.5
# Beyond this many spectators a ground is thinned rather than drawn whole: the
# uniform array takes 256 placements per draw call, so 12,000 is under fifty.
DEFAULT_CAP = 12000


def seats_for_stand(corners, row_step=1.9, seat_step=SEAT_STEP):
    """-> [(x, y, z, yaw)] a seat at a time, facing the pitch.

    The rows follow the stand's own slope, so the back rows come out higher than
    the front ones, which is what makes a bowl read as a bowl.
    """
    seats = []
    for start, end in crowd_gen.rows_for_stand(corners, row_step):
        length = math.dist(start, end)
        if length < 1.0:
            continue
        count = max(1, int(length / seat_step))
        for i in range(count):
            t = (i + 0.5) / count
            x = start[0] + (end[0] - start[0]) * t
            y = start[1] + (end[1] - start[1]) * t
            z = start[2] + (end[2] - start[2]) * t
            # facing the centre spot: a figure at yaw a faces (sin a, -cos a)
            seats.append((x, y, z, math.atan2(-x, y)))
    return seats


def share_out(seats, model_count):
    """-> one list of seats per model, dealt round robin.

    PES ships seventeen spectators and a stand should not be seventeen clones in a
    row, so neighbours get different models.
    """
    if model_count <= 0:
        return []
    shares = [[] for _ in range(model_count)]
    for index, seat in enumerate(seats):
        shares[index % model_count].append(seat)
    return shares


def thin_to(seats, cap):
    """-> at most `cap` seats, spread over the whole stand rather than one end of it."""
    if cap <= 0 or len(seats) <= cap:
        return list(seats)
    step = len(seats) / float(cap)
    return [seats[min(len(seats) - 1, int(i * step))] for i in range(cap)]


# PES's spectators declare no texture: the game binds a palette and the model's own
# UVs pick a colour out of it. Both palettes sit beside the models, 32 x 128 of
# swatches each - one for the low-detail crowd, one for the rest.
PALETTE_LOW = "au_l_col_bsm_rgba32"
PALETTE_HIGH = "au_h_col_bsm_rgba32"


def palette_for(model_path):
    """-> which of PES's crowd palettes a spectator model reads."""
    stem = os.path.basename(str(model_path)).lower()
    if stem.startswith("au_low") or stem.startswith("au_l"):
        return PALETTE_LOW
    return PALETTE_HIGH


def palette_offset(variant_index, variant_count):
    """-> (du, dv) moving a variant onto its own band of the palette.

    Every copy of one model samples the palette the same way, so without this a
    stand is one shirt colour repeated. PES varies it per spectator; we vary it per
    variant, which is as far as a placement list of x, y, z and yaw reaches.
    """
    if variant_count <= 1:
        return (0.0, 0.0)
    return (0.0, (variant_index % variant_count) / float(variant_count))


# How often a flag is held up among the spectators.
FLAG_EVERY = 60


def flag_places(seats, every=FLAG_EVERY):
    """-> the seats that hold a flag, spread through the crowd.

    PES's stand flags (mob_prop_teamflag_home01..05, away01) are props among the
    spectators rather than one per seat, and dt19 animates them waving; imported
    static they belong scattered the same way.
    """
    if not seats:
        return []
    step = max(1, int(every))
    places = seats[::step]
    return places if places else [seats[0]]


def _palette_png(stem, ftex_index, out_dir, converted):
    """-> the palette converted to a PNG beside the crowd, or None if it is absent."""
    class _Named(object):
        def __init__(self, filename):
            self.filename = filename

    if stem not in ftex_index:
        print("  palette %s is not in the models' sourceimages" % stem)
        return None
    return stadium_to_gf._texture_png(_Named(stem + ".ftex"), ftex_index, out_dir, converted)


def _write_instances(path, seats, header):
    with open(path, "w") as out:
        out.write("# %s\n" % header)
        out.write("# x y z yaw - one seat per line (src/utils/instancelist.hpp)\n")
        for x, y, z, yaw in seats:
            out.write("%.3f %.3f %.3f %.4f\n" % (x, y, z, yaw))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("pack", help="a stadium pack directory (it holds audi/)")
    parser.add_argument("--out", required=True, help="the converted stadium's directory")
    parser.add_argument("--models", required=True,
                        help="an extraction of PES's Asset/model/bg/common/audi")
    parser.add_argument("--fmdl-lib", default=None)
    parser.add_argument("--textures", action="append", default=[])
    parser.add_argument("--cap", type=int, default=DEFAULT_CAP)
    parser.add_argument("--asset-dir", default=None,
                        help="where this will be installed under media/objects/stadiums, "
                             "for the .ase's own texture paths (e.g. pes_st060/crowd)")
    parser.add_argument("--flags", default=None,
                        help="a directory holding PES's stand flags "
                             "(common/demo/prop/mob_prop_teamflag_*), scattered through "
                             "the crowd one seat in %d" % FLAG_EVERY)
    parser.add_argument("--variants", type=int, default=6,
                        help="how many of PES's spectators to seat")
    args = parser.parse_args()

    if args.fmdl_lib and args.fmdl_lib not in sys.path:
        sys.path.insert(0, args.fmdl_lib)

    areas = sorted(glob.glob(os.path.join(args.pack, "audi", "**", "audiarea*.bin"), recursive=True))
    if not areas:
        print("no audiarea.bin under %s/audi: this pack says nothing about its stands" % args.pack)
        return 1
    stands = crowd_gen.parse_stands(areas[0])
    if not stands:
        print("no stands in %s" % os.path.basename(areas[0]))
        return 1

    seats = []
    for corners, row_step in stands:
        seats.extend(seats_for_stand(corners, row_step))
    print("%d stand(s), %d seat(s)" % (len(stands), len(seats)))
    if not seats:
        return 1
    kept = thin_to(seats, args.cap)
    if len(kept) < len(seats):
        print("  thinned to %d, evenly over the stands (--cap)" % len(kept))

    # The spectators themselves: the parts models are the variants, and au_Low is
    # the cheap one. Prefer variety, cheapest first.
    models = sorted(glob.glob(os.path.join(args.models, "**", "au*.fmdl"), recursive=True))
    models = [m for m in models if "sky" not in os.path.basename(m).lower()]
    if not models:
        print("no spectator models under %s" % args.models)
        return 1
    models = models[:max(1, args.variants)]
    print("%d spectator model(s): %s"
          % (len(models), ", ".join(os.path.basename(m) for m in models)))

    texture_dirs = stadium_to_gf.find_texture_dirs(args.models, *args.textures)
    ftex_index = stadium_to_gf.build_ftex_index(texture_dirs)
    converted = {}
    out_dir = os.path.join(args.out, "crowd")
    os.makedirs(out_dir, exist_ok=True)

    shares = share_out(kept, len(models))
    entries = []
    for index, (model, share) in enumerate(zip(models, shares)):
        if not share:
            continue
        fmdl = stadium_to_gf._load_fmdl(model, args.fmdl_lib)
        name = "crowd_%02d" % index
        ase_path = os.path.join(out_dir, name + ".ase")
        meshes = list(fmdl.meshes)
        # The palette PES would have bound, and this variant's band of it.
        palette = palette_for(model)
        bitmap = _palette_png(palette, ftex_index, out_dir, converted)
        offset = palette_offset(index, len(models))
        with open(ase_path, "w") as out:
            stadium_to_gf._write_ase_header(out, name)
            out.write("*MATERIAL_LIST {\n\t*MATERIAL_COUNT %d\n" % len(meshes))
            for i, mesh in enumerate(meshes):
                stadium_to_gf._write_material(out, i, "%s_m%d" % (name, i), bitmap,
                                              args.asset_dir or "crowd", None)
            out.write("}\n")
            for i, mesh in enumerate(meshes):
                stadium_to_gf._write_geomobject(out, "%s_%02d" % (name, i), i, mesh.faces,
                                                uv_offset=offset)
        _write_instances(os.path.join(out_dir, name + ".instances"), share,
                         "%s: %d seat(s) of %s" % (name, len(share), os.path.basename(model)))
        entries.append((name, len(share)))
        print("  %s: %s over %d seat(s)" % (name, os.path.basename(model), len(share)))

    # The flags held up among them, if we were given PES's.
    if args.flags:
        flag_models = sorted(glob.glob(os.path.join(args.flags, "**", "mob_prop_teamflag*.fmdl"),
                                       recursive=True))
        seen = {}
        for path in flag_models:
            seen.setdefault(os.path.basename(path), path)
        flag_models = [seen[k] for k in sorted(seen)]
        flag_seats = flag_places(kept)
        if flag_models and flag_seats:
            flag_index = stadium_to_gf.build_ftex_index(
                stadium_to_gf.find_texture_dirs(args.flags))
            shares = share_out(flag_seats, len(flag_models))
            for index, (model, share) in enumerate(zip(flag_models, shares)):
                if not share:
                    continue
                fmdl = stadium_to_gf._load_fmdl(model, args.fmdl_lib)
                name = "crowd_flag_%02d" % index
                with open(os.path.join(out_dir, name + ".ase"), "w") as out:
                    stadium_to_gf._write_ase_header(out, name)
                    out.write("*MATERIAL_LIST {\n\t*MATERIAL_COUNT %d\n" % len(fmdl.meshes))
                    for i, mesh in enumerate(fmdl.meshes):
                        texture = stadium_to_gf._mesh_base_texture(mesh)
                        bitmap = (stadium_to_gf._texture_png(texture, flag_index, out_dir,
                                                            converted) if texture else None)
                        stadium_to_gf._write_material(out, i, "%s_m%d" % (name, i), bitmap,
                                                      args.asset_dir or "crowd", None)
                    out.write("}\n")
                    for i, mesh in enumerate(fmdl.meshes):
                        stadium_to_gf._write_geomobject(out, "%s_%02d" % (name, i), i, mesh.faces)
                _write_instances(os.path.join(out_dir, name + ".instances"), share,
                                 "%s: %d flag(s) of %s" % (name, len(share),
                                                           os.path.basename(model)))
                entries.append((name, len(share)))
                print("  %s: %s over %d place(s)" % (name, os.path.basename(model), len(share)))

    if not entries:
        return 1

    object_path = os.path.join(out_dir, "crowd.object")
    with open(object_path, "w") as out:
        out.write("<object>\n")
        for name, _count in entries:
            out.write("\n\t<geometry>\n\t\t<filename>%s.ase</filename>\n" % name)
            out.write("\t\t<instances>%s.instances</instances>\n" % name)
            out.write("\t\t<name>%s</name>\n" % name)
            out.write("\t\t<position>0, 0, 0</position>\n")
            out.write("\t\t<rotation>0, 0, 0, 0</rotation>\n")
            out.write("\t\t<properties>\n\t\t\t<physicable>false</physicable>\n")
            out.write("\t\t\t<movable>false</movable>\n\t\t</properties>\n\t</geometry>\n")
        out.write("\n</object>\n")
    print("wrote %s: %d seated" % (object_path, sum(c for _n, c in entries)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Imports a whole 4cc aesthetic export as a playable team.

The exports all share one layout - the one the 2HUG and HDG packs use:

    <pack>/Boots/<kNNNN - Player Name>/boots.fmdl      full-body model
    <pack>/Gloves/<gNNNN - Player Name>/glove_*.fmdl   (keepers, optional)
    <pack>/Faces/<fNNNN - Player Name>/face.fmdl       (optional)

so importing a team is mechanical: convert every boots model into the engine's
fullbody format, install it under a directory named after the pack and the
player's export id, and write the playermodels.cfg lines that bind them to
database players.

  python3 import_team.py <pack-dir> --prefix 2hug --fmdl-lib <pes-fmdl dir>
                         [--first-db-id 393] [--max-tris 20000] [--dry-run]

--first-db-id assigns consecutive database ids to the players in export order;
pass --db-ids to map them explicitly. Nothing is overwritten without --force.

The models land in data/media/players/custom/<prefix>_<export id>/ and the
config lines are appended to data/media/players/playermodels.cfg, matching how
the existing packs are installed (see docs/PES21_IMPORT.md).
"""

import argparse
import os
import re
import subprocess
import sys

import body_coverage
import retarget

EXPORT_DIR_RE = re.compile(r"^([kgf])(\d+)\s*-\s*(.+)$")


def find_players(pack_dir, kind="Boots", model_name="boots.fmdl"):
    """-> [(export_id, player_name, fmdl_path)] in export order."""
    root = os.path.join(pack_dir, kind)
    if not os.path.isdir(root):
        return []
    found = []
    for entry in sorted(os.listdir(root)):
        match = EXPORT_DIR_RE.match(entry)
        if not match:
            continue
        fmdl = os.path.join(root, entry, model_name)
        if not os.path.isfile(fmdl):
            continue
        found.append((match.group(2), match.group(3).strip(), fmdl))
    return found


# The verdicts body_coverage.py returns for an export that is not a whole body.
# PES draws a boots or glove export *over* its own body wearing the team's kit - the
# packs' kit textures are DXT1 and carry no alpha, so nothing is hiding that body -
# and binding the prop in its place leaves the prop and nothing else. lcg_2702 is
# `boots` plus `wings`; 2hug_1851 is one `medical_c` mesh.
NOT_A_BODY = ("needs base", "carries scenery")


def may_bind_as_body(verdict, composited=False):
    """Whether an export may replace a player's body.

    `composited` is for --base, which puts the stock skinned body underneath: the
    result clothes the rig whatever the export alone measured.
    """
    if composited:
        return True
    return verdict == "whole"


def describe_import(dest, prefix, export_id):
    """-> body_coverage's verdict on the .ase the import just wrote."""
    ase = os.path.join(dest, "fullbody_%s_%s.ase" % (prefix, export_id))
    if not os.path.isfile(ase):
        return "missing"
    vertices, _ = body_coverage.read_vertices(ase)
    return body_coverage.verdict(vertices, retarget.gf_world_bind())[0]


# A 4cc export names its portraits by team slot rather than by player, because the
# slot a team gets is not known when the pack is built: "XXX07 - Rodya.png". The
# models carry the same slot in their directory name, and their bindings already
# resolve a slot to a database ID - so that is what the portraits ride on.
PORTRAIT_SLOT_RE = re.compile(r"(\d{2})(?:\D|$)")


def portrait_slot(filename):
    """The slot a portrait file names, or None when it names none."""
    base = os.path.basename(filename)
    match = PORTRAIT_SLOT_RE.search(base)
    if not match:
        return None
    return int(match.group(1))


def model_number(model_dir):
    """The export number a model directory carries: .../lcg_2707 -> 2707."""
    base = os.path.basename(model_dir.rstrip("/"))
    digits = re.search(r"_(\d+)$", base)
    return int(digits.group(1)) if digits else None


def portrait_name(filename):
    """The nickname a portrait file carries: "XXX09 - Dante.png" -> "dante"."""
    base = os.path.splitext(os.path.basename(filename))[0]
    trimmed = re.sub(r"^\S+\s*-\s*", "", base).strip().lower()
    return re.sub(r"[^a-z0-9]+", "", trimmed)


def bind_portraits(model_bindings, portrait_files, prefix, names=None):
    """Maps database ID -> portrait path, for players this pack has a model for.

    model_bindings is {database ID: model directory} as playermodels.cfg holds it, and
    names is {database ID: player name} out of the roster - which is the authority on
    who a player is. A 4cc export names its portraits for the player ("XXX09 -
    Dante.png"), so the roster name is what binds them; the slot in the filename is a
    fallback for a pack that numbers its portraits and nothing more.
    """
    # Each pack numbers its own players from a base of its choosing - lcg from 2701,
    # 2hug from 1851, ink from 2426 - while the portraits always count from 01. So the
    # slot is the offset from the pack's lowest number, not the digits themselves.
    numbered = {}
    for database_id, model_dir in model_bindings.items():
        if ("/%s_" % prefix) not in model_dir:
            continue
        number = model_number(model_dir)
        if number is not None:
            numbered[number] = database_id
    base = min(numbered) if numbered else 0
    by_slot = {number - base + 1: database_id for number, database_id in numbered.items()}
    # Both sides usually carry the player's nickname, and that beats the numbering:
    # LCG's portraits run one ahead of its boots from slot 8 on, so binding by slot
    # gives ten players somebody else's face. Fall back to the slot only for a name
    # that appears on neither side or on both.
    # A portrait needs no model: any rostered player can have a face on the game plan,
    # so the name pass runs over the whole roster it was given.
    by_name = {}
    for database_id, player in (names or {}).items():
        nickname = portrait_name("x - %s" % player)
        if not nickname:
            continue
        # A name two players share is no evidence at all.
        by_name[nickname] = None if nickname in by_name else database_id

    # Names first, across the whole set, and only then slots for what is left. One
    # pass would let an unnamed portrait take a player by slot before the portrait
    # that names him is reached - which is exactly LCG's Papa Don and Dante.
    bound = {}
    taken = set()
    unmatched = []
    for name in portrait_files:
        nickname = portrait_name(name)
        database_id = by_name.get(nickname) if nickname else None
        if database_id is None or database_id in taken:
            unmatched.append(name)
            continue
        taken.add(database_id)
        bound[database_id] = "imports/%s/portraits/%s" % (prefix, name)
    # A pack whose portraits name their players has already shown its numbering to be
    # unreliable wherever a name failed to match - LCG's run one ahead from slot 8 -
    # so no guessing from there. Slots are for a pack that offers nothing else.
    if bound:
        return bound
    for name in unmatched:
        slot = portrait_slot(name)
        database_id = by_slot.get(slot) if slot is not None else None
        if database_id is None or database_id in taken:
            continue
        taken.add(database_id)
        bound[database_id] = "imports/%s/portraits/%s" % (prefix, name)
    return bound


def install_dir(game_dir, prefix, export_id):
    return os.path.join(game_dir, "media", "players", "custom",
                        "%s_%s" % (prefix, export_id))


def install_kit_texture(pack_dir, dest):
    """Puts the team's outfield kit in the model directory as body.png.

    A 4cc character model carries its own textures for hair, dress and so on,
    but the mesh wearing the actual football kit still points at the shared
    PES kit map (u0XXXp0), which no pack ships - the kit itself lives under
    Kit Textures as u0<team>p1. Without it that one mesh has nothing to
    sample and the loader stops on a missing file.
    """
    kit_dir = os.path.join(pack_dir, "Kit Textures")
    if not os.path.isdir(kit_dir):
        return False
    names = [n for n in sorted(os.listdir(kit_dir)) if n.lower().endswith((".dds", ".png"))]
    outfield = [n for n in names if "p1" in n.lower()] or names
    if not outfield:
        return False
    try:
        from PIL import Image
        image = Image.open(os.path.join(kit_dir, outfield[0]))
        image.load()
        if image.mode != "RGBA":
            image = image.convert("RGBA")
        os.makedirs(dest, exist_ok=True)
        image.save(os.path.join(dest, "body.png"))
        return True
    except Exception as error:
        print("  kit texture failed: %s" % error)
        return False


def import_player(fmdl, dest, fmdl_lib, max_tris, texture_rel, force=False, max_edge=0.15,
                  base_ase=None):
    ase = os.path.join(dest, "fullbody_%s.ase" % os.path.basename(dest))
    if os.path.exists(ase) and not force:
        return "exists"
    os.makedirs(dest, exist_ok=True)
    command = [sys.executable,
               os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "fmdl_to_fullbody.py"),
               fmdl, dest, "--fmdl-lib", fmdl_lib,
               "--texture", texture_rel, "--max-tris", str(max_tris),
               "--max-edge", str(max_edge)]
    if base_ase:
        # A face-slot model is a head and hair, nothing else. Imported on its
        # own it is a head floating where the body should be; it has to be
        # composited over a skinned body.
        command += ["--base", base_ase,
                    # the stock head would otherwise sit inside the imported one
                    "--drop-base-parts", "eyes,face,scalp,hair"]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        return "FAILED: " + (result.stderr.strip().splitlines() or ["?"])[-1]
    return "imported"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pack_dir")
    parser.add_argument("--prefix", required=True,
                        help="directory prefix, e.g. 2hug / hdg / a")
    parser.add_argument("--fmdl-lib", required=True)
    parser.add_argument("--game-dir", default="data")
    parser.add_argument("--first-db-id", type=int, default=0,
                        help="assign consecutive database ids from here")
    parser.add_argument("--db-ids", default="",
                        help="explicit comma-separated database ids, in export order")
    # 20,000 dropped whole meshes and took heads off models. select_meshes fits
    # meshes into the budget biggest-first and discards what will not fit, and a 4cc
    # character is not a 20,000-triangle budget: lcg_2709's source carries 205,774
    # faces over 20 meshes, of which the old budget kept 16,308 - its head, at 20,086
    # faces, was one of the meshes thrown away whole, leaving a robe with a floating
    # chefhat where the head should be. At 100,000 it comes in at 96,130 over four
    # meshes and is a whole character again.
    parser.add_argument("--max-tris", type=int, default=100000)
    parser.add_argument("--max-edge", type=float, default=0.15,
                        help="drop triangles with an edge longer than this "
                             "(metres); see fmdl_to_fullbody")
    parser.add_argument("--base", default="",
                        help="stock fullbody .ase to composite the import over; "
                             "required for face-slot models, which carry no body")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    players = find_players(args.pack_dir)
    if not players:
        print("no <kNNNN - Name>/boots.fmdl exports under", args.pack_dir)
        return 1

    explicit = [int(x) for x in args.db_ids.split(",") if x.strip()]
    lines = []
    for index, (export_id, name, fmdl) in enumerate(players):
        dest = install_dir(args.game_dir, args.prefix, export_id)
        db_id = (explicit[index] if index < len(explicit)
                 else (args.first_db_id + index if args.first_db_id else None))
        rel = os.path.relpath(dest, args.game_dir).replace(os.sep, "/")
        status = "dry-run"
        if not args.dry_run:
            # Only a whole-character pack needs the kit dropped in beside it:
            # its own mesh wears the kit. A face-slot import is a head, and the
            # body it is composited onto keeps the engine's kit slot, which the
            # team's kit is swapped into at run time (Team::FetchKit).
            if not args.base:
                install_kit_texture(args.pack_dir, dest)
            status = import_player(fmdl, dest, args.fmdl_lib, args.max_tris,
                                   rel + "/body.png", args.force, args.max_edge,
                                   args.base or None)
        # What the import actually produced decides whether it may stand in for a
        # body. An export that leaves the rig's joints bare is a prop PES draws over
        # its own body, and binding it in place of that body leaves only the prop.
        verdict = "whole" if args.dry_run else describe_import(dest, args.prefix, export_id)
        bindable = may_bind_as_body(verdict, composited=bool(args.base))
        print("%-6s %-28s %-34s %s%s" % (export_id, name[:28], rel, status,
                                         "" if bindable else "  NOT BOUND: " + verdict))
        if db_id is not None and bindable:
            lines.append("%d %s" % (db_id, rel))

    if lines and not args.dry_run:
        cfg = os.path.join(args.game_dir, "media", "players", "playermodels.cfg")
        existing = open(cfg).read() if os.path.exists(cfg) else ""
        with open(cfg, "a") as out:
            if existing and not existing.endswith("\n"):
                out.write("\n")
            for line in lines:
                if line.split()[0] + " " not in existing:
                    out.write(line + "\n")
        print("wrote %d playermodels.cfg entries" % len(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())

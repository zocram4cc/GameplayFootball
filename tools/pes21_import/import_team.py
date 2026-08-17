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
    parser.add_argument("--max-tris", type=int, default=20000)
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
        print("%-6s %-28s %-34s %s" % (export_id, name[:28], rel, status))
        if db_id is not None:
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

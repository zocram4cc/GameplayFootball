"""Imports a face-slot 4cc team, one fullbody per player, from its .ted.

A whole-character pack (lcg, hdg, 2hug) has a unique body per player, so
import_team.py is enough. A face-slot pack shares ONE body across the squad and
gives each player his own face and hair; import_team.py, which iterates the Boots
folder, imports that body once and leaves the other twenty-two players on the stock
face - which is what a showcase then shows.

This drives the import from the .ted instead (squad_models.assign), so each player
gets his own fullbody: the shared body composited over the stock body (--base) with
his face and hair, or - when his face is baked into the body - the body on its own.

    import_squad.py <team.ted> <pack dir> --prefix vn --fmdl-lib <pes-fmdl dir> \
        --base <stock fullbody.ase> [--game-dir data] [--db-ids ...] [--force]

Database ids default to the player ids the .ted says (82901..), which is what the
squad's own tactical rows will reference.
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import squad_models  # noqa: E402
import ted  # noqa: E402
from import_team import base_parts_to_drop, install_kit_texture, mesh_names  # noqa: E402


def import_player(fmdl, dest, rel, game_dir, fmdl_lib, max_tris, max_edge, base_ase,
                  face_dir=None, force=False):
    """One player's fullbody: the mesh composited over the stock body, with the
    pack's face dropped in when the player has a face folder."""
    ase = os.path.join(dest, "fullbody_%s.ase" % os.path.basename(dest))
    if os.path.exists(ase) and not force:
        return "exists"
    os.makedirs(dest, exist_ok=True)
    command = [sys.executable,
               os.path.join(HERE, "fmdl_to_fullbody.py"),
               fmdl, dest, "--fmdl-lib", fmdl_lib,
               "--texture", os.path.join(rel, "body.png"),
               "--max-tris", str(max_tris), "--max-edge", str(max_edge)]
    # A face-slot body is headless and needs the stock body beneath it.
    if base_ase:
        command += ["--base", base_ase]
        drop = base_parts_to_drop(mesh_names(fmdl, fmdl_lib))
        if drop:
            command += ["--drop-base-parts", ",".join(sorted(drop))]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        return "FAILED: " + (result.stderr.strip().splitlines() or ["?"])[-1]
    return "imported"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ted_path")
    parser.add_argument("pack_dir")
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--fmdl-lib", required=True)
    parser.add_argument("--base", required=True,
                        help="the stock fullbody .ase to composite face-slot bodies over")
    parser.add_argument("--game-dir", default="data")
    parser.add_argument("--max-tris", type=int, default=100000)
    parser.add_argument("--max-edge", type=float, default=0.15)
    parser.add_argument("--db-ids", default="")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    export, _plain = ted.read_export(args.ted_path)
    assignment = squad_models.assign(
        export["squad"],
        squad_models.read_faces(os.path.join(args.pack_dir, "Faces")),
        squad_models.read_boots(os.path.join(args.pack_dir, "Boots")))
    explicit = [int(x) for x in args.db_ids.split(",") if x.strip()]
    print("%s: %d player(s)" % (export["team"], len(assignment)))

    lines = []
    for index, (player_id, got) in enumerate(sorted(assignment.items())):
        body_dir = got["body"]
        if not body_dir:
            print("  %-6d NO BODY in the pack - skipped" % player_id)
            continue
        pair = got["face_dir"]
        body = os.path.join(args.pack_dir, "Boots", body_dir, "boots.fmdl")
        db = explicit[index] if index < len(explicit) else player_id
        dest = os.path.join(args.game_dir, "media", "players", "custom",
                            "%s_%s" % (args.prefix, db))
        rel = os.path.relpath(dest, args.game_dir).replace(os.sep, "/")
        status = "dry-run"
        if not args.dry_run:
            status = import_player(body, dest, rel, args.game_dir, args.fmdl_lib,
                                   args.max_tris, args.max_edge, args.base,
                                   face_dir=pair if pair else None, force=args.force)
            if status == "imported":
                install_kit_texture(args.pack_dir, dest)
                lines.append((db, rel))
        print("  %-6d %-28s %-22s %s" % (db, body_dir or "-", pair or "(baked face)", status))

    if lines and not args.dry_run:
        cfg = os.path.join(args.game_dir, "media", "players", "playermodels.cfg")
        existing = open(cfg).read() if os.path.exists(cfg) else ""
        with open(cfg, "a") as out:
            if existing and not existing.endswith("\n"):
                out.write("\n")
            for db, rel in lines:
                if ("\n%d " % db) not in existing and (" %d " % db) not in existing:
                    out.write("%d %s\n" % (db, rel))
        print("wrote %d playermodels.cfg entries" % len(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())

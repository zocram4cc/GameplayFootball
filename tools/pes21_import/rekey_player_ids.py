"""Re-key every installed 4cc squad to the player ids its export carries.

The engine reads players.id as the one key every generated binding hangs
off - playermodels.cfg, playerportraits.cfg, playercelebrations.cfg are all
"<database id> <path>" lines. install_team once let SQLite number the rows,
so every re-import renumbered the roster and every binding went stale
silently: all 46 dbg/smbg model bindings pointed at deleted rows and every
custom player fell back to the stock shared body, with no warning anywhere.

Two halves:

- install_team now writes the export's own ids (ted record id, 80301..), so a
  re-import rewrites the same rows. New installs never strand.
- Every database already on disk still carries the old autoincrement blocks.
  This re-keys each installed squad in the record order its rows were
  written in - live rows ordered by id against the export's record order -
  and rewrites the three cfg files through the same map.

The export id is the source of truth; nothing here matches by name (names
change mid championship, ids cannot). The one name use is the safety check:
if a row block's names no longer line up with the export's record order, the
positional map would be a guess, and a guess that moves ids is the bug this
exists to kill - so that team is refused, untouched, until the drift is
explained.

  python3 rekey_player_ids.py [--game-dir data] [--database PATH]

Run the tests: python3 -m unittest test_rekey_player_ids -v
"""

import argparse
import glob
import os
import sqlite3

import install_team
import ted

PLAYER_CFGS = ("playermodels.cfg", "playerportraits.cfg", "playercelebrations.cfg")


def rekey(database, game_dir, exports):
    """Move each team's rows onto the export ids. exports maps team name ->
    export dict (ted.read_export's shape). -> [(old id, new id)] for every
    row actually moved."""
    moved = []
    conn = sqlite3.connect(database)
    try:
        cur = conn.cursor()
        # The team row by art_tag, the same identity the installer files its
        # art under: the shipped row is "2HUG", its export says "/2hug/".
        by_tag = {install_team.art_tag(name): row
                  for row, name in cur.execute("select id, name from teams")}
        for team_name, export in exports.items():
            team_row = by_tag.get(install_team.art_tag(team_name))
            if team_row is None:
                continue
            rows = cur.execute(
                "select id, lastname from players where team_id = ? order by id",
                (team_row,)).fetchall()
            records = export["players"]
            if len(rows) != len(records):
                raise ValueError(
                    "team %s: %d row(s) in the database but %d record(s) in "
                    "the export - refusing to guess which id goes where"
                    % (team_name, len(rows), len(records)))
            for row, record in zip(rows, records):
                old, new = row[0], record["id"]
                if old == new:
                    continue  # already keyed to the export - a rerun is a no-op
                if row[1] != record["name"]:
                    raise ValueError(
                        "team %s: row %d is '%s' but export record %d is '%s' - "
                        "the row block has drifted off the export's record "
                        "order; refusing to move ids on a guess"
                        % (team_name, old, row[1], record["id"], record["name"]))
                moved.append((old, new))
        if moved:
            # Two passes through a temp offset make any overlap between the
            # old blocks and the export ids impossible, whatever they are.
            ceiling = cur.execute("select max(id) from players").fetchone()[0]
            offset = ceiling + 1 - min(old for old, _ in moved)
            for old, _ in moved:
                cur.execute("update players set id = id + ? where id = ?", (offset, old))
            for old, new in moved:
                cur.execute("update players set id = ? where id = ?", (new, old + offset))
            conn.commit()
    finally:
        conn.close()
    if moved:
        _rebind_cfgs(game_dir, dict(moved))
    return moved


def _rebind_cfgs(game_dir, id_map):
    """Rewrite the id column of every player cfg through the re-key map."""
    cfg_dir = os.path.join(game_dir, "media", "players")
    for name in PLAYER_CFGS:
        path = os.path.join(cfg_dir, name)
        if not os.path.isfile(path):
            continue
        out = []
        for line in open(path, encoding="utf-8"):
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                out.append(line)
                continue
            ident, _, rest = stripped.partition(" ")
            new_id = id_map.get(int(ident))
            out.append("%d %s\n" % (new_id if new_id is not None else int(ident), rest))
        with open(path, "w", encoding="utf-8") as f:
            f.writelines(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--game-dir", default="data")
    parser.add_argument("--database", default="")
    args = parser.parse_args()
    database = args.database or os.path.join(args.game_dir, "databases",
                                             "default", "database.sqlite")
    exports = {}
    for ted_path in sorted(glob.glob(os.path.join(args.game_dir, "imports",
                                                  "*", "tactical.ted"))):
        export, _ = ted.read_export(ted_path)
        exports[export["team"]] = export
    if not exports:
        print("no tactical.ted exports under %s/imports - nothing to re-key" % args.game_dir)
        return 1
    moved = rekey(database, args.game_dir, exports)
    for old, new in moved:
        print("%6d -> %6d" % (old, new))
    print("%d row(s) re-keyed" % len(moved))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

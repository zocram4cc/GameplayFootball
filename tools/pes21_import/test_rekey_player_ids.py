"""Tests for re-keying installed squads to their export's own player ids.

The engine reads players.id as the one key every generated binding
(playermodels.cfg, playerportraits.cfg, playercelebrations.cfg) hangs off.
Letting SQLite assign those ids renumbers the roster on every re-import and
silently strands every binding - all 46 dbg/smbg entries went dead that way
and every custom model fell back to the shared body, with no warning.

The fix is at install time (install_team writes the export's own ids), but
every database already on disk carries the old autoincrement blocks. This
re-keys each installed squad to the ids its tactical export carries, in the
same record order the rows were written in, and rewrites the three cfg files
through the same map. Never matches by name: the name can change mid
championship, the id cannot.

Run: python3 -m unittest test_rekey_player_ids -v
"""

import os
import shutil
import sqlite3
import tempfile
import unittest

import install_team
import rekey_player_ids
import test_install_team
from test_install_team import TEAM, TACTICS


class RekeyingAnInstalledSquad(unittest.TestCase):
    def setUp(self):
        self.game = tempfile.mkdtemp()
        handle, self.db = tempfile.mkstemp(suffix=".sqlite")
        os.close(handle)
        self.path = self.db
        conn = sqlite3.connect(self.path)
        conn.executescript(test_install_team.SCHEMA)
        conn.commit()
        conn.close()
        # A squad installed the old way: SQLite numbered the rows itself.
        _, by_shirt = install_team.install(self.path, TEAM, TACTICS)
        # Roll the ids back to a legacy autoincrement block, the way every
        # database on disk still looks before this tool runs.
        conn = sqlite3.connect(self.path)
        conn.execute("update players set id = id - 80300 + 700")
        conn.commit()
        conn.close()
        self.old_ids = sorted(row - 80300 + 700 for row in by_shirt.values())
        cfg_dir = os.path.join(self.game, "media", "players")
        os.makedirs(cfg_dir, exist_ok=True)
        with open(os.path.join(cfg_dir, "playermodels.cfg"), "w") as f:
            for n, row in enumerate(self.old_ids):
                f.write("%d media/players/custom/hdg_k%d\n" % (row, n + 1))
        with open(os.path.join(cfg_dir, "playerportraits.cfg"), "w") as f:
            f.write("%d imports/hdg/portraits/XXX05 - Someone.png\n" % self.old_ids[4])

    def tearDown(self):
        os.unlink(self.path)
        shutil.rmtree(self.game)

    def _rows(self):
        conn = sqlite3.connect(self.path)
        out = conn.execute(
            "select id, lastname from players where team_id="
            "(select id from teams where name='/hdg/') order by id").fetchall()
        conn.close()
        return out

    def test_rows_are_rekeyed_to_the_export_ids(self):
        moved = rekey_player_ids.rekey(self.path, self.game, {TEAM["team"]: TEAM})
        self.assertEqual([row for row, _ in moved], self.old_ids)
        self.assertEqual(
            [row for row, _ in self._rows()],
            [p["id"] for p in TEAM["players"]])
        # Everything else about the row survives the renumber.
        self.assertEqual([name for _, name in self._rows()],
                         ["Player %d" % n for n in range(1, 24)])

    def test_cfg_entries_follow_the_rows(self):
        rekey_player_ids.rekey(self.path, self.game, {TEAM["team"]: TEAM})
        cfg = open(os.path.join(self.game, "media", "players",
                                "playermodels.cfg")).read()
        for player in TEAM["players"]:
            self.assertIn("%d media/players/custom/hdg_k%d\n"
                          % (player["id"], player["id"] - 80300), cfg)
        portraits = open(os.path.join(self.game, "media", "players",
                                      "playerportraits.cfg")).read()
        self.assertIn("%d imports/hdg/portraits/XXX05 - Someone.png"
                      % TEAM["players"][4]["id"], portraits)

    def test_rekeying_again_changes_nothing(self):
        rekey_player_ids.rekey(self.path, self.game, {TEAM["team"]: TEAM})
        moved = rekey_player_ids.rekey(self.path, self.game, {TEAM["team"]: TEAM})
        self.assertEqual(moved, [])
        self.assertEqual(
            [row for row, _ in self._rows()],
            [p["id"] for p in TEAM["players"]])

    def test_a_mismatched_roster_is_refused(self):
        short = dict(TEAM, players=TEAM["players"][:10], squad=TEAM["squad"][:10],
                     game_plan=TEAM["game_plan"])
        with self.assertRaises(ValueError):
            rekey_player_ids.rekey(self.path, self.game, {TEAM["team"]: short})
        # Refused means untouched: the rows keep the ids they were installed
        # with, so a second pass can succeed once the export is whole again.
        self.assertEqual([row for row, _ in self._rows()], self.old_ids)


if __name__ == "__main__":
    unittest.main()

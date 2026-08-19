"""Tests for putting a 4cc team into the engine's database.

ted.py reads a team out of its tactical export - team 803 "/hdg/" (HBR), 23 players
80301..80323 wearing 1..23, their names and shirt names, five of them coloured, and
three formation presets. This puts that into the `teams` and `players` tables the
engine actually reads, beside the 4cc teams already there (/a/, /lcg/, /ink/, 2HUG).

Two things it must get right, both learned the hard way elsewhere in this toolchain:

Re-running it must not double the roster. An import that appends is an import you can
only run once, and every one of these gets run again the moment a pack is updated.

And the sliders have to be written, not left to the loader's defaults. All ten shipped
teams carry a tactics_xml, and not one of them mentions team_pressure, counter_attack
or support_distance: those three are injected in memory by EnsureTacticDefinition
after the XML is read, so a team that has never been saved has no stored value for
them at all. Writing them makes them data the pack author owns rather than a default
nobody can see.

Run: python3 -m unittest test_install_team -v
"""

import os
import sqlite3
import tempfile
import unittest

import install_team

SCHEMA = """
CREATE TABLE teams(id INTEGER PRIMARY KEY AUTOINCREMENT, league_id INTEGER,
  name VARCHAR(64), logo_url VARCHAR(512), kit_url VARCHAR(512), formation_xml TEXT,
  formation_factory_xml TEXT, tactics_xml TEXT, tactics_factory_xml TEXT,
  shortname VARCHAR(3), color1 VARCHAR(16), color2 VARCHAR(16));
CREATE TABLE players(id INTEGER PRIMARY KEY AUTOINCREMENT, team_id INTEGER,
  nationalteam_id INTEGER, firstname VARCHAR(64), lastname VARCHAR(64),
  role VARCHAR(32), age INTEGER, base_stat FLOAT, profile_xml TEXT, skincolor INTEGER,
  hairstyle VARCHAR(64), haircolor VARCHAR(64), height FLOAT, weight FLOAT,
  formationorder INTEGER, nationalteamformationorder INTEGER);
"""

# ted.read_export's own shape: the name lives under "team", not "name".
TEAM = {"team": "/hdg/", "abbreviation": "HBR", "team_id": 803, "manager": "MANAGER",
        "chants": ["DEATH TO SWEDEN"],
        "squad": [{"id": 80300 + n, "number": n} for n in range(1, 24)],
        "squad_order": list(range(1, 40)),
        "players": [{"name": "Player %d" % n, "shirt_name": "P%d" % n,
                     "name_colour": "#8b5f55ff" if n == 7 else None,
                     "name_colours": [], "extra": "PLACEHOLDER"}
                    for n in range(1, 24)],
        "formations": []}

TACTICS = {"team_pressure": 0.85, "counter_attack": 0.9, "support_distance": 0.15,
           "position_offense_depth_factor": 0.95}


class InstallingATeam(unittest.TestCase):
    def setUp(self):
        handle, self.path = tempfile.mkstemp(suffix=".sqlite")
        os.close(handle)
        conn = sqlite3.connect(self.path)
        conn.executescript(SCHEMA)
        conn.execute("insert into teams(name, shortname) values ('/ink/', 'INK')")
        conn.commit()
        conn.close()

    def tearDown(self):
        os.unlink(self.path)

    def rows(self, sql, *args):
        conn = sqlite3.connect(self.path)
        out = conn.execute(sql, args).fetchall()
        conn.close()
        return out

    def test_the_team_lands_with_its_name_and_code(self):
        install_team.install(self.path, TEAM, TACTICS)
        self.assertEqual(self.rows("select name, shortname from teams where name='/hdg/'"),
                         [("/hdg/", "HBR")])

    def test_the_teams_already_there_are_left_alone(self):
        install_team.install(self.path, TEAM, TACTICS)
        self.assertEqual(len(self.rows("select id from teams")), 2)
        self.assertEqual(self.rows("select shortname from teams where name='/ink/'"),
                         [("INK",)])

    def test_the_whole_squad_arrives(self):
        install_team.install(self.path, TEAM, TACTICS)
        players = self.rows("select lastname, formationorder from players "
                            "where team_id=(select id from teams where name='/hdg/') "
                            "order by formationorder")
        self.assertEqual(len(players), 23)
        self.assertEqual(players[0][0], "Player 1")

    def test_running_it_again_does_not_double_the_roster(self):
        install_team.install(self.path, TEAM, TACTICS)
        install_team.install(self.path, TEAM, TACTICS)
        self.assertEqual(len(self.rows("select id from teams where name='/hdg/'")), 1)
        self.assertEqual(len(self.rows(
            "select id from players where team_id="
            "(select id from teams where name='/hdg/')")), 23)

    def test_a_keeper_is_a_keeper_and_the_rest_are_not(self):
        install_team.install(self.path, TEAM, TACTICS)
        roles = [r[0] for r in self.rows(
            "select role from players where team_id="
            "(select id from teams where name='/hdg/') order by formationorder")]
        self.assertEqual(roles[0], "GK")
        self.assertNotIn("GK", roles[1:])

    def test_every_slider_is_written_and_not_left_to_a_default(self):
        install_team.install(self.path, TEAM, TACTICS)
        xml = self.rows("select tactics_xml from teams where name='/hdg/'")[0][0]
        for key, value in TACTICS.items():
            self.assertIn("<%s>" % key, xml)
        self.assertIn("0.850000", xml)

    def test_the_factory_copy_records_what_the_pack_shipped(self):
        # so a user can see what he has changed, which is what the slider UI shows
        install_team.install(self.path, TEAM, TACTICS)
        row = self.rows("select tactics_xml, tactics_factory_xml from teams "
                        "where name='/hdg/'")[0]
        self.assertEqual(row[0], row[1])

    def test_a_shirt_number_becomes_the_formation_order(self):
        install_team.install(self.path, TEAM, TACTICS)
        orders = sorted(r[0] for r in self.rows(
            "select formationorder from players where team_id="
            "(select id from teams where name='/hdg/')"))
        self.assertEqual(orders, list(range(23)))

    def test_a_team_with_no_name_is_refused(self):
        with self.assertRaises(ValueError):
            install_team.install(self.path, dict(TEAM, team=""), TACTICS)

    def test_a_team_with_no_squad_is_refused_rather_than_half_written(self):
        empty = dict(TEAM, squad=[], players=[])
        with self.assertRaises(ValueError):
            install_team.install(self.path, empty, TACTICS)
        self.assertEqual(len(self.rows("select id from teams")), 1)


class EveryPlayerNeedsAProfile(unittest.TestCase):
    """An empty profile_xml is fatal, not merely bare.

    PlayerData parses profile_xml into its stats and PlayerData::GetStat asserts the
    stat it is asked for exists - so a player with no profile kills the match at the
    first lookup rather than playing badly. The 22 keys are the ones the shipped
    teams carry (physical_*, technical_*, mental_*).
    """

    KEYS = ("physical_balance", "physical_reaction", "physical_acceleration",
            "physical_velocity", "physical_stamina", "physical_agility",
            "physical_shotpower", "technical_standingtackle", "technical_slidingtackle",
            "technical_ballcontrol", "technical_dribble", "technical_shortpass",
            "technical_highpass", "technical_header", "technical_shot",
            "technical_volley", "mental_calmness", "mental_workrate",
            "mental_resilience", "mental_defensivepositioning",
            "mental_offensivepositioning", "mental_vision")

    def test_every_stat_the_engine_asks_for_is_there(self):
        xml = install_team.profile_xml(0.62, "CF", 3)
        for key in self.KEYS:
            self.assertIn("<%s>" % key, xml)

    def test_nothing_lands_outside_zero_to_one(self):
        for role in ("GK", "CB", "CM", "CF"):
            for seed in range(12):
                xml = install_team.profile_xml(0.95, role, seed)
                for value in install_team.stat_values(xml).values():
                    self.assertGreaterEqual(value, 0.0)
                    self.assertLessEqual(value, 1.0)

    def test_the_same_player_gets_the_same_profile_every_run(self):
        # Pinned to a constant on purpose. Comparing two calls in one process would
        # pass even with Python's per-process string hashing, which would hand out a
        # different team on every run of the importer.
        value = install_team.stat_values(
            install_team.profile_xml(0.62, "CM", 5))["technical_shortpass"]
        self.assertAlmostEqual(value, 0.759908, places=5)

    def test_two_players_are_not_identical(self):
        self.assertNotEqual(install_team.profile_xml(0.62, "CM", 5),
                            install_team.profile_xml(0.62, "CM", 6))

    def test_a_keeper_handles_and_a_striker_shoots(self):
        keeper = install_team.stat_values(install_team.profile_xml(0.62, "GK", 0))
        striker = install_team.stat_values(install_team.profile_xml(0.62, "CF", 0))
        self.assertGreater(striker["technical_shot"], keeper["technical_shot"])
        self.assertGreater(keeper["mental_defensivepositioning"],
                           striker["mental_defensivepositioning"])

    def test_a_better_base_stat_lifts_the_profile(self):
        weak = install_team.stat_values(install_team.profile_xml(0.40, "CM", 2))
        strong = install_team.stat_values(install_team.profile_xml(0.90, "CM", 2))
        self.assertGreater(sum(strong.values()), sum(weak.values()))


class TheInstalledRosterCarriesProfiles(InstallingATeam):
    def test_no_player_is_left_without_stats(self):
        install_team.install(self.path, TEAM, TACTICS)
        profiles = self.rows("select profile_xml from players where team_id="
                             "(select id from teams where name='/hdg/')")
        self.assertEqual(len(profiles), 23)
        for (xml,) in profiles:
            self.assertIn("<technical_shot>", xml or "")

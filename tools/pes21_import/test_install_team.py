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

import import_team
import install_team
import ted

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

    def test_the_logo_and_kit_urls_are_set(self):
        # A NULL here is fatal, not cosmetic: the scoreboard asks the resource manager
        # for the empty path and it dies with "There is no loader for
        # databases/default/". That crashed a whole showcase run.
        install_team.install(self.path, TEAM, TACTICS)
        logo, kit = self.rows("select logo_url, kit_url from teams where name='/hdg/'")[0]
        self.assertEqual(logo, "images_teams/hdg/hdg_logo.png")
        self.assertEqual(kit, "images_teams/hdg/hdg")

    def test_the_tag_comes_from_the_team_name_and_not_the_code(self):
        # /lcg/ -> lcg, matching the directories the shipped teams already use
        self.assertEqual(install_team.art_tag("/hdg/"), "hdg")
        self.assertEqual(install_team.art_tag("/lcg/"), "lcg")
        self.assertEqual(install_team.art_tag("2HUG"), "2hug")

    def test_a_name_with_nothing_usable_in_it_still_gives_a_tag(self):
        self.assertTrue(install_team.art_tag("///"))

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


def full_stats(mode, **exceptions):
    """A player's full stats dict, as ted.read_players would actually decode
    it: every named PES stat at `mode`, except the given exceptions."""
    stats = {name: mode for name, _, _ in ted.STAT_FIELDS}
    stats.update(exceptions)
    return stats


class TheStatConversion(unittest.TestCase):
    """install_team converts each of a player's own decoded PES stats onto the
    engine's own keys one at a time - not a "gold/silver/bronze" template
    lookup. The 4cc's own tier rules (what a gold rates, how many silvers a
    squad gets) shift between tournaments and even between packs of the same
    one; a lookup table would need updating every time they did, and would
    flatten out any player a pack author hand-edited off his tier's template.
    """

    def test_every_engine_stat_is_present(self):
        xml = install_team.stat_profile_xml(full_stats(80), "CM", 0)
        for key in install_team.STAT_KEYS:
            self.assertIn("<%s>" % key, xml)

    def test_a_named_stat_reaches_its_own_engine_key(self):
        stats = install_team.stat_values(install_team.stat_profile_xml(
            full_stats(80, offensive_awareness=99), "CM", 0))
        self.assertGreater(stats["mental_offensivepositioning"],
                           install_team.pes_to_base(90))

    def test_ball_winning_feeds_both_tackle_keys(self):
        stats = install_team.stat_values(install_team.stat_profile_xml(
            full_stats(80, ball_winning=40), "CM", 0))
        self.assertLess(stats["technical_standingtackle"], install_team.pes_to_base(60))
        self.assertLess(stats["technical_slidingtackle"], install_team.pes_to_base(60))

    def test_a_real_per_player_deviation_survives_the_conversion(self):
        # HDG's own silver pair disagree on Defensive Awareness (60 vs 50) despite
        # sharing every other stat - a real difference in the file, and a tier
        # lookup keyed only on "silver" would have erased it
        a = install_team.stat_values(install_team.stat_profile_xml(
            full_stats(95, defensive_awareness=60), "CM", 9))
        b = install_team.stat_values(install_team.stat_profile_xml(
            full_stats(95, defensive_awareness=50), "CM", 9))
        self.assertNotEqual(a["mental_defensivepositioning"], b["mental_defensivepositioning"])

    def test_a_key_with_no_pes_analogue_uses_the_players_own_mean(self):
        # mental_vision has nothing in PES_TO_ENGINE_STAT pointing at it
        low = install_team.stat_values(install_team.stat_profile_xml(full_stats(40), "CM", 0))
        high = install_team.stat_values(install_team.stat_profile_xml(full_stats(99), "CM", 0))
        self.assertGreater(high["mental_vision"], low["mental_vision"])


class TheInstalledRosterCarriesRealStats(InstallingATeam):
    def test_a_players_base_stat_reflects_his_own_mean_rating(self):
        team = dict(TEAM, players=[
            dict(p, stats=full_stats(99)) if i == 0 else p
            for i, p in enumerate(TEAM["players"])])
        install_team.install(self.path, team, TACTICS)
        base_stat = self.rows(
            "select base_stat from players where team_id="
            "(select id from teams where name='/hdg/') and lastname='Player 1'")[0][0]
        self.assertAlmostEqual(base_stat, install_team.pes_to_base(99), places=5)

    def test_a_player_with_no_decoded_stats_still_gets_the_flat_baseline(self):
        # no export should ever crash the loader for lacking real stats, even
        # though every real pack checked decodes them cleanly
        install_team.install(self.path, TEAM, TACTICS)
        base_stat = self.rows(
            "select base_stat from players where team_id="
            "(select id from teams where name='/hdg/') and lastname='Player 1'")[0][0]
        self.assertEqual(base_stat, install_team.BASE_STAT)


class ModelsBindToTheRowsPlayersActuallyGot(unittest.TestCase):
    """A 4cc pack names its exports by shirt number, and the database assigns
    row ids on insert. Binding a model to a number guessed before the write is
    what silently unbound both squads: the ids moved and playermodels.cfg did
    not."""

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.path = os.path.join(self.dir, "db.sqlite")
        make_database(self.path)

    def test_install_reports_the_row_id_for_every_shirt(self):
        _, by_shirt = install_team.install(self.path, TEAM, TACTICS)
        self.assertEqual(sorted(by_shirt), sorted(s["number"] for s in TEAM["squad"]))

    def test_the_reported_rows_are_the_rows_in_the_database(self):
        _, by_shirt = install_team.install(self.path, TEAM, TACTICS)
        conn = sqlite3.connect(self.path)
        try:
            for shirt, row in by_shirt.items():
                found = conn.execute(
                    "select formationorder from players where id = ?", (row,)).fetchone()
                self.assertIsNotNone(found, "shirt %d points at no row" % shirt)
        finally:
            conn.close()

    def test_a_reinstall_reports_the_new_rows(self):
        """Re-importing deletes and re-inserts the squad, so every row id moves.
        The mapping has to come back from the write that just happened."""
        _, first = install_team.install(self.path, TEAM, TACTICS)
        _, second = install_team.install(self.path, TEAM, TACTICS)
        self.assertEqual(sorted(first), sorted(second))
        self.assertNotEqual(sorted(first.values()), sorted(second.values()))

    def test_the_export_id_of_a_pack_directory_is_its_shirt(self):
        self.assertEqual(import_team.shirt_number(2402), 2)
        self.assertEqual(import_team.shirt_number(2411), 11)
        self.assertEqual(import_team.shirt_number(2421), 21)


class ModelsBindToTheRowsPlayersActuallyGot(unittest.TestCase):
    """A 4cc pack names its model exports by shirt number and the database
    assigns row ids on insert. Binding a model to a number guessed before that
    write is what silently unbound both squads: the ids moved when the roster
    was reinstalled and playermodels.cfg went on pointing at the old ones."""

    def setUp(self):
        handle, self.path = tempfile.mkstemp(suffix=".sqlite")
        os.close(handle)
        conn = sqlite3.connect(self.path)
        conn.executescript(SCHEMA)
        conn.commit()
        conn.close()

    def tearDown(self):
        os.unlink(self.path)

    def test_every_shirt_in_the_squad_is_reported(self):
        _, by_shirt = install_team.install(self.path, TEAM, TACTICS)
        self.assertEqual(sorted(by_shirt),
                         sorted(entry["number"] for entry in TEAM["squad"]))

    def test_the_reported_rows_exist(self):
        _, by_shirt = install_team.install(self.path, TEAM, TACTICS)
        conn = sqlite3.connect(self.path)
        try:
            for shirt, row in sorted(by_shirt.items()):
                found = conn.execute("select id from players where id = ?",
                                     (row,)).fetchone()
                self.assertIsNotNone(found, "shirt %d points at no row" % shirt)
        finally:
            conn.close()

    def test_a_reinstall_reports_the_rows_it_just_wrote(self):
        """Re-importing deletes and re-inserts the squad, so every id moves.
        A mapping computed before the write would be stale here."""
        _, first = install_team.install(self.path, TEAM, TACTICS)
        _, second = install_team.install(self.path, TEAM, TACTICS)
        self.assertEqual(sorted(first), sorted(second))
        self.assertNotEqual(sorted(first.values()), sorted(second.values()))
        conn = sqlite3.connect(self.path)
        try:
            live = {row[0] for row in conn.execute("select id from players")}
        finally:
            conn.close()
        self.assertTrue(set(second.values()) <= live)

    def test_a_pack_export_id_names_its_shirt(self):
        """HDG ships k2402, k2411 and k2421 for shirts 2, 11 and 21."""
        self.assertEqual(import_team.shirt_number(2402), 2)
        self.assertEqual(import_team.shirt_number(2411), 11)
        self.assertEqual(import_team.shirt_number(2421), 21)


class TheTeamsArtLandsWhereTheEngineLooks(unittest.TestCase):
    """The database rows point at these files and nothing else creates them.
    A missing kit is silently substituted with flat white or flat black
    (team.cpp), and a missing logo kills the match outright."""

    def setUp(self):
        self.pack = tempfile.mkdtemp()
        self.game = tempfile.mkdtemp()
        from PIL import Image
        os.makedirs(os.path.join(self.pack, "Logo"))
        os.makedirs(os.path.join(self.pack, "Kit Textures"))
        # the full-size emblem and its two smaller mips
        for name, size in (("emblem_0XXX_r.png", 128),
                           ("emblem_0XXX_r_l.png", 64),
                           ("emblem_0XXX_r_ll.png", 32)):
            Image.new("RGBA", (size, size), (size, 0, 0, 255)).save(
                os.path.join(self.pack, "Logo", name))
        for name, shade in (("u0XXXp1.png", 10), ("u0XXXp2.png", 20),
                            ("u0XXXp3.png", 30), ("u0XXXg1.png", 40),
                            ("u0XXXp4.png", 50)):
            Image.new("RGBA", (8, 8), (shade, 0, 0, 255)).save(
                os.path.join(self.pack, "Kit Textures", name))

    def art_dir(self):
        return os.path.join(self.game, "databases", "default", "images_teams", "x")

    def test_the_logo_and_three_outfield_kits_are_written(self):
        written = import_team.install_art(self.pack, self.game, "x")
        self.assertEqual(written, ["x_logo.png", "x_kit_01.png", "x_kit_02.png",
                                   "x_kit_03.png", "x_gk.png"])
        for name in written:
            self.assertTrue(os.path.exists(os.path.join(self.art_dir(), name)), name)

    def test_the_keeper_kit_does_not_take_an_outfield_slot(self):
        """g1 is the keeper's. Installing it as _kit_03 - which a hand install
        did once - puts an outfield player in the keeper's shirt."""
        from PIL import Image
        import_team.install_art(self.pack, self.game, "x")
        keeper = Image.open(os.path.join(self.art_dir(), "x_gk.png")).getpixel((0, 0))
        third = Image.open(os.path.join(self.art_dir(), "x_kit_03.png")).getpixel((0, 0))
        self.assertEqual(keeper[0], 40)
        self.assertEqual(third[0], 30)

    def test_the_full_size_emblem_wins_over_its_mips(self):
        from PIL import Image
        import_team.install_art(self.pack, self.game, "x")
        logo = Image.open(os.path.join(self.art_dir(), "x_logo.png"))
        self.assertEqual(logo.size, (128, 128))

    def test_a_dry_run_writes_nothing_but_still_reports(self):
        written = import_team.install_art(self.pack, self.game, "x", dry_run=True)
        self.assertEqual(len(written), 5)
        self.assertFalse(os.path.exists(self.art_dir()))

    def test_a_pack_without_art_is_not_an_error(self):
        empty = tempfile.mkdtemp()
        self.assertEqual(import_team.install_art(empty, self.game, "x"), [])


class PortraitsBindToTheirPlayers(unittest.TestCase):
    """Packs name portraits two ways and both end in the shirt number: 2HUG
    ships player_78301.dds with the full PES id, HDG ships player_XXX21.dds
    with the team left as a literal placeholder."""

    def setUp(self):
        self.pack = tempfile.mkdtemp()
        self.game = tempfile.mkdtemp()
        os.makedirs(os.path.join(self.pack, "Portraits"))
        self.by_shirt = {1: 501, 2: 502, 21: 521}

    def add(self, name):
        from PIL import Image
        Image.new("RGBA", (8, 8), (1, 2, 3, 255)).save(
            os.path.join(self.pack, "Portraits", name))

    def test_a_full_pes_id_resolves_through_its_last_two_digits(self):
        self.add("player_78302.png")
        self.assertEqual(import_team.install_portraits(
            self.pack, self.game, "t", self.by_shirt),
            [(502, "imports/t/portraits/player_502.png")])

    def test_a_placeholder_team_id_resolves_the_same_way(self):
        self.add("player_XXX21.png")
        self.assertEqual(import_team.install_portraits(
            self.pack, self.game, "t", self.by_shirt),
            [(521, "imports/t/portraits/player_521.png")])

    def test_the_file_is_actually_written(self):
        self.add("player_XXX01.png")
        [(_, rel)] = import_team.install_portraits(
            self.pack, self.game, "t", self.by_shirt)
        self.assertTrue(os.path.exists(os.path.join(self.game, rel)))

    def test_a_portrait_for_nobody_in_the_squad_is_skipped(self):
        """A pack can carry more portraits than the ted has players."""
        self.add("player_XXX99.png")
        self.assertEqual(import_team.install_portraits(
            self.pack, self.game, "t", self.by_shirt), [])

    def test_a_pack_without_portraits_is_not_an_error(self):
        self.assertEqual(import_team.install_portraits(
            tempfile.mkdtemp(), self.game, "t", self.by_shirt), [])


class TheConfigsAreAppendedNotDoubled(unittest.TestCase):
    """Every one of these gets re-run the moment a pack is updated."""

    def setUp(self):
        handle, self.path = tempfile.mkstemp()
        os.close(handle)

    def tearDown(self):
        os.unlink(self.path)

    def test_new_ids_are_added(self):
        self.assertEqual(import_team.append_config(self.path, ["7 a", "8 b"]), 2)
        self.assertEqual(open(self.path).read(), "7 a\n8 b\n")

    def test_an_id_already_bound_is_not_written_twice(self):
        import_team.append_config(self.path, ["7 a"])
        self.assertEqual(import_team.append_config(self.path, ["7 c", "9 d"]), 1)
        self.assertEqual(open(self.path).read(), "7 a\n9 d\n")

    def test_a_file_without_a_trailing_newline_does_not_join_lines(self):
        open(self.path, "w").write("1 x")
        import_team.append_config(self.path, ["2 y"])
        self.assertEqual(open(self.path).read().splitlines(), ["1 x", "2 y"])

"""Puts a 4cc team into the engine's database: the team, its squad, its tactics.

ted.py reads a team out of its tactical export - HDG's gives team 803 "/hdg/" (HBR),
23 players 80301..80323 wearing 1..23, their names and shirt names, five of them
coloured, and three formation presets. This writes that into the `teams` and
`players` tables the engine reads, beside the 4cc teams already there (/a/, /lcg/,
/ink/, 2HUG).

Two things it is careful about.

It is idempotent. An import that appends is one you can run exactly once, and every
importer here gets run again the moment a pack is updated - so the team is matched by
name and its roster replaced rather than added to.

And it writes the tactics rather than leaving them to the loader. All ten shipped
teams carry a tactics_xml and not one mentions team_pressure, counter_attack or
support_distance: TeamData injects those three in memory after the XML is read
(EnsureTacticDefinition), so a team that has never been saved through the menu has no
stored value for them. Writing them makes the setup data the pack author owns instead
of a default nobody can see or edit.

  python3 install_team.py <file.ted> <database.sqlite> [--tactics k=v,k=v] [--dry-run]
"""

import argparse
import os
import re
import sqlite3
import sys
import zlib

import ted

# A squad's first man is the keeper; PES's squad order puts him there and so does the
# engine's formationorder.
KEEPER_ROLE = "GK"
# What the shipped 4cc teams carry.
BASE_STAT = 0.62
FIELD_ROLES = ("CB", "CB", "LB", "RB", "DM", "CM", "CM", "LM", "RM", "CF", "CF",
               "CB", "LB", "RB", "CM", "CM", "LM", "RM", "CF", "CF", "CM", "CF")


# The stats PlayerData parses out of profile_xml. GetStat asserts the stat it is
# asked for exists, so a player with an empty profile kills the match at the first
# lookup rather than merely playing badly - every one of these has to be written.
STAT_KEYS = (
    "physical_balance", "physical_reaction", "physical_acceleration",
    "physical_velocity", "physical_stamina", "physical_agility", "physical_shotpower",
    "technical_standingtackle", "technical_slidingtackle", "technical_ballcontrol",
    "technical_dribble", "technical_shortpass", "technical_highpass",
    "technical_header", "technical_shot", "technical_volley",
    "mental_calmness", "mental_workrate", "mental_resilience",
    "mental_defensivepositioning", "mental_offensivepositioning", "mental_vision")

# What a role is for. An offset on the base stat, so a keeper is not a striker who
# happens to stand in goal.
ROLE_BIAS = {
    "GK": {"technical_shot": -0.22, "technical_volley": -0.20, "technical_dribble": -0.18,
           "mental_defensivepositioning": +0.14, "physical_reaction": +0.12,
           "mental_offensivepositioning": -0.20},
    "CB": {"technical_standingtackle": +0.12, "technical_header": +0.10,
           "mental_defensivepositioning": +0.10, "technical_dribble": -0.08,
           "technical_shot": -0.10},
    "LB": {"physical_velocity": +0.08, "technical_slidingtackle": +0.08,
           "technical_shot": -0.08},
    "RB": {"physical_velocity": +0.08, "technical_slidingtackle": +0.08,
           "technical_shot": -0.08},
    "DM": {"technical_standingtackle": +0.10, "technical_shortpass": +0.06,
           "mental_defensivepositioning": +0.08},
    "CM": {"technical_shortpass": +0.10, "mental_vision": +0.10, "physical_stamina": +0.08},
    "LM": {"physical_acceleration": +0.10, "technical_dribble": +0.08},
    "RM": {"physical_acceleration": +0.10, "technical_dribble": +0.08},
    "CF": {"technical_shot": +0.14, "technical_volley": +0.10,
           "mental_offensivepositioning": +0.12, "technical_standingtackle": -0.10,
           "mental_defensivepositioning": -0.08},
}


def profile_xml(base_stat, role, seed):
    """One player's stats, as the engine's profile_xml.

    Deterministic in (base_stat, role, seed): an importer that reshuffles stats on
    every run makes a team unrepeatable, and this gets re-run whenever a pack is
    updated. The spread is a fixed hash of the stat name and the seed, so two players
    of the same role differ without either being random.
    """
    bias = ROLE_BIAS.get(role, {})
    lines = []
    for key in STAT_KEYS:
        # a settled +-0.05 wobble, from the name and the seed and nothing else
        # crc32, not hash(): Python randomises string hashing per process, so hash()
        # would give a different team every time the importer ran.
        digest = zlib.crc32(("%s:%d" % (key, seed)).encode()) & 0xffff
        wobble = (digest / 65535.0 - 0.5) * 0.10
        value = base_stat + bias.get(key, 0.0) + wobble
        lines.append("<%s>%.6f</%s>" % (key, min(1.0, max(0.0, value)), key))
    return "\n".join(lines) + "\n"


def stat_values(xml):
    """-> {stat: value} out of a profile_xml, for checking one."""
    return {m.group(1): float(m.group(2))
            for m in re.finditer(r"<([a-z_]+)>([\d.]+)</\1>", xml)}


def art_tag(name):
    """The directory a team's art lives under: "/hdg/" -> "hdg", "2HUG" -> "2hug".

    Matches what the shipped teams already use - images_teams/lcg, images_teams/ink -
    so a 4cc team's crest and kits sit beside theirs.
    """
    tag = "".join(c for c in name.lower() if c.isalnum())
    return tag or "team"


def tactics_xml(tactics):
    """The engine's own format: one tag per slider (TeamData::SaveTactics)."""
    return "".join("<%s>%.6f</%s>\n" % (k, v, k) for k, v in sorted(tactics.items()))


def install(database, team, tactics, dry_run=False):
    """Writes `team` into `database`. Returns the team's row id.

    Raises rather than half-writing when there is no squad to install: a team with no
    players crashes the engine at kickoff, and an empty roster means the export was
    misread.
    """
    # ted.read_export's own shape: the name is under "team".
    name = team.get("team") or team.get("name")
    if not name:
        raise ValueError("export carries no team name")
    if not team.get("squad") or not team.get("players"):
        raise ValueError("%s has no squad to install" % name)

    conn = sqlite3.connect(database)
    try:
        cur = conn.cursor()
        xml = tactics_xml(tactics)
        # A NULL logo or kit is fatal, not cosmetic: the scoreboard hands the empty
        # path to the resource manager and it dies with "There is no loader for
        # databases/default/", which took a whole showcase run down.
        tag = art_tag(name)
        logo = "images_teams/%s/%s_logo.png" % (tag, tag)
        kit = "images_teams/%s/%s" % (tag, tag)
        row = cur.execute("select id from teams where name = ?", (name,)).fetchone()
        if row:
            team_row = row[0]
            cur.execute("update teams set shortname = ?, tactics_xml = ?, "
                        "tactics_factory_xml = ?, logo_url = ?, kit_url = ? "
                        "where id = ?",
                        (team["abbreviation"][:3], xml, xml, logo, kit, team_row))
            cur.execute("delete from players where team_id = ?", (team_row,))
        else:
            league = cur.execute("select league_id from teams where league_id is not null "
                                 "limit 1").fetchone()
            cur.execute("insert into teams(league_id, name, shortname, tactics_xml, "
                        "tactics_factory_xml, logo_url, kit_url) "
                        "values (?, ?, ?, ?, ?, ?, ?)",
                        (league[0] if league else 1, name,
                         team["abbreviation"][:3], xml, xml, logo, kit))
            team_row = cur.lastrowid

        # The squad in order. The number is the shirt; formationorder is the slot, and
        # the keeper takes the first of them.
        for slot, entry in enumerate(team["squad"]):
            player = (team["players"][slot] if slot < len(team["players"])
                      else {"name": "Player %d" % entry["number"]})
            role = KEEPER_ROLE if slot == 0 else FIELD_ROLES[(slot - 1) % len(FIELD_ROLES)]
            cur.execute(
                "insert into players(team_id, nationalteam_id, firstname, lastname, role, "
                "age, base_stat, profile_xml, skincolor, hairstyle, haircolor, height, "
                "weight, formationorder, nationalteamformationorder) "
                "values (?, 0, '', ?, ?, 25, ?, ?, 1, 'short01', 'black', 1.8, 75.0, ?, 0)",
                (team_row, player["name"][:64], role, BASE_STAT,
                 profile_xml(BASE_STAT, role, slot), slot))
        if dry_run:
            conn.rollback()
        else:
            conn.commit()
        return team_row
    finally:
        conn.close()


def parse_tactics(text):
    out = {}
    for pair in text.split(","):
        pair = pair.strip()
        if not pair:
            continue
        key, _, value = pair.partition("=")
        out[key.strip()] = float(value)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ted")
    parser.add_argument("database")
    parser.add_argument("--tactics", default="",
                        help="sliders to write, k=v,k=v (default: neutral 0.5 for the "
                             "three the shipped database has no value for)")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    export, _ = ted.read_export(args.ted)
    tactics = parse_tactics(args.tactics) or {
        "team_pressure": 0.5, "counter_attack": 0.5, "support_distance": 0.5}
    row = install(args.database, export, tactics, args.dry_run)
    coloured = sum(1 for p in export["players"] if p.get("name_colour"))
    print("%s -> %s: team row %d, %d player(s), %d coloured name(s), %d slider(s)%s"
          % (os.path.basename(args.ted), os.path.basename(args.database), row,
             len(export["squad"]), coloured, len(tactics),
             " (dry run, rolled back)" if args.dry_run else ""))
    for key, value in sorted(tactics.items()):
        print("   %-34s %.2f" % (key, value))
    return 0


if __name__ == "__main__":
    sys.exit(main())

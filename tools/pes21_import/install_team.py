"""Puts a 4cc team into the engine's database: the team, its squad, its tactics.

ted.py reads a team out of its tactical export - HDG's gives team 803 "/hdg/" (HBR),
23 players 80301..80323 wearing 1..23, their names and shirt names, five of them
coloured, and three formation presets. This writes that into the `teams` and
`players` tables the engine reads, beside the 4cc teams already there (/a/, /lcg/,
/ink/, 2HUG).

Three things it is careful about.

It is idempotent. An import that appends is one you can run exactly once, and every
importer here gets run again the moment a pack is updated - so the team is matched by
name and its roster replaced rather than added to.

And it writes the tactics rather than leaving them to the loader. All ten shipped
teams carry a tactics_xml and not one mentions team_pressure, counter_attack or
support_distance: TeamData injects those three in memory after the XML is read
(EnsureTacticDefinition), so a team that has never been saved through the menu has no
stored value for them. Writing them makes the setup data the pack author owns instead
of a default nobody can see or edit.

And every player's stats are converted from his own decoded PES ratings
(stat_profile_xml), not looked up from a "gold/silver/bronze" template keyed on
which tier ted.py thinks he is. The 4cc's tier rules move between tournaments
and between packs of the same one; reading his real bytes needs no update when
they do, and does not flatten a hand-edited player back onto his tier's mould.

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
# The first 22 are the engine's own; the rest carry PES 2021's remaining
# attributes 1:1 (physical_form and the like are PES's non-7-bit ratings, put on
# the same 0..1 scale).
STAT_KEYS = (
    "physical_balance", "physical_reaction", "physical_acceleration",
    "physical_velocity", "physical_stamina", "physical_agility", "physical_shotpower",
    "technical_standingtackle", "technical_slidingtackle", "technical_ballcontrol",
    "technical_dribble", "technical_shortpass", "technical_highpass",
    "technical_header", "technical_shot", "technical_volley",
    "mental_calmness", "mental_workrate", "mental_resilience",
    "mental_defensivepositioning", "mental_offensivepositioning", "mental_vision",
    "physical_jump", "physical_contact", "physical_form", "physical_injuryresistance",
    "technical_tightpossession", "technical_setpiece", "technical_curl",
    "technical_interceptions", "technical_ballwinning",
    "technical_weakfootusage", "technical_weakfootaccuracy",
    "mental_aggression",
    "gk_awareness", "gk_catching", "gk_clearing", "gk_reflexes", "gk_coverage")
GK_KEYS = ("gk_awareness", "gk_catching", "gk_clearing", "gk_reflexes", "gk_coverage")

# What a role is for. An offset on the base stat, so a keeper is not a striker who
# happens to stand in goal. Every outfield role shares OUTFIELD_BIAS: PES rates
# an outfielder's goalkeeping at the floor, and a flat-stat team must not field
# ten spare keepers.
OUTFIELD_BIAS = {key: -0.45 for key in GK_KEYS}
ROLE_BIAS = {
    "GK": {"technical_shot": -0.22, "technical_volley": -0.20, "technical_dribble": -0.18,
           "mental_defensivepositioning": +0.14, "physical_reaction": +0.12,
           "mental_offensivepositioning": -0.20, "technical_tightpossession": -0.18,
           "technical_setpiece": -0.15, "technical_curl": -0.15,
           "technical_ballwinning": -0.20, "technical_interceptions": -0.10,
           "mental_aggression": -0.10, "gk_awareness": +0.15, "gk_catching": +0.15,
           "gk_clearing": +0.15, "gk_reflexes": +0.15, "gk_coverage": +0.15},
    "CB": {"technical_standingtackle": +0.12, "technical_header": +0.10,
           "mental_defensivepositioning": +0.10, "technical_dribble": -0.08,
           "technical_shot": -0.10, "physical_jump": +0.10, "physical_contact": +0.10,
           "technical_interceptions": +0.10, "technical_ballwinning": +0.10,
           "mental_aggression": +0.06, "technical_tightpossession": -0.08},
    "LB": {"physical_velocity": +0.08, "technical_slidingtackle": +0.08,
           "technical_shot": -0.08, "technical_ballwinning": +0.06, "technical_curl": +0.04},
    "RB": {"physical_velocity": +0.08, "technical_slidingtackle": +0.08,
           "technical_shot": -0.08, "technical_ballwinning": +0.06, "technical_curl": +0.04},
    "DM": {"technical_standingtackle": +0.10, "technical_shortpass": +0.06,
           "mental_defensivepositioning": +0.08, "technical_ballwinning": +0.10,
           "technical_interceptions": +0.08, "mental_aggression": +0.06},
    "CM": {"technical_shortpass": +0.10, "mental_vision": +0.10, "physical_stamina": +0.08,
           "technical_tightpossession": +0.06, "technical_setpiece": +0.04},
    "LM": {"physical_acceleration": +0.10, "technical_dribble": +0.08,
           "technical_tightpossession": +0.06, "technical_curl": +0.08},
    "RM": {"physical_acceleration": +0.10, "technical_dribble": +0.08,
           "technical_tightpossession": +0.06, "technical_curl": +0.08},
    "CF": {"technical_shot": +0.14, "technical_volley": +0.10,
           "mental_offensivepositioning": +0.12, "technical_standingtackle": -0.10,
           "mental_defensivepositioning": -0.08, "physical_jump": +0.06,
           "technical_ballwinning": -0.10, "technical_interceptions": -0.10},
}
for _role, _bias in ROLE_BIAS.items():
    if _role != KEEPER_ROLE:
        _bias.update(OUTFIELD_BIAS)


# ted.read_players decodes every one of a player's own PES ratings straight off
# his record - the same "Player entry" bytes the real EDIT format uses, not a
# 4cc-specific stat block. This maps those onto the engine's own stat keys one
# at a time, rather than classifying a player into "gold/silver/bronze" first
# and looking a template rating up: the 4cc's tier rules (what a gold rates,
# how many silvers a squad gets) shift between tournaments and even between
# packs of the same one, and a lookup table would need updating every time they
# did, silently, wherever an importer forgot to. It would also flatten out any
# player a pack author hand-edited off his tier's template - a real difference
# in the file, not a typo to be corrected away.
#
# Every PES 2021 attribute has its own key. Two PES stats fan out: Ball Winning
# feeds the engine's two tackle keys as well as its own, and Aggression feeds
# mental_workrate (the engine's pre-existing reading of it) as well as its own.
# PES 2021 has no Interceptions rating - that arrived with eFootball - so
# technical_interceptions starts from Defensive Awareness, the stat PES folds
# it into.
PES_TO_ENGINE_STAT = {
    "offensive_awareness": "mental_offensivepositioning",
    "ball_control": "technical_ballcontrol",
    "tight_possession": "technical_tightpossession",
    "low_pass": "technical_shortpass",
    "lofted_pass": "technical_highpass",
    "finishing": "technical_shot",
    "place_kicking": "technical_setpiece",
    "curl": "technical_curl",
    "speed": "physical_velocity",
    "acceleration": "physical_acceleration",
    "jump": "physical_jump",
    "physical_contact": "physical_contact",
    "balance": "physical_balance",
    "stamina": "physical_stamina",
    "ball_winning": "technical_ballwinning",
    "aggression": "mental_aggression",
    "defensive_awareness": "mental_defensivepositioning",
    "heading": "technical_header",
    "dribbling": "technical_dribble",
    "kicking_power": "physical_shotpower",
    "gk_awareness": "gk_awareness",
    "gk_catching": "gk_catching",
    "gk_clearing": "gk_clearing",
    "gk_reflexes": "gk_reflexes",
    "gk_reach": "gk_coverage",
}
# (engine key, PES stat) for the keys that borrow a second reading of a PES stat.
DERIVED_KEYS = (("technical_standingtackle", "ball_winning"),
                ("technical_slidingtackle", "ball_winning"),
                ("mental_workrate", "aggression"),
                ("technical_interceptions", "defensive_awareness"))
# The non-7-bit ratings, (engine key, ted ability name): shown 1..max -> 0..1.
ABILITY_KEYS = (("technical_weakfootusage", "weak_foot_usage"),
                ("technical_weakfootaccuracy", "weak_foot_accuracy"),
                ("physical_form", "form"),
                ("physical_injuryresistance", "injury_resistance"))
ABILITY_MAX = {name: shown_max for name, _, _, _, shown_max in ted.ABILITY_FIELDS}

# The Playing Styles PES allows a position (the engine's ten roles; PES's
# wingers and second strikers fold into LM/RM and CF), for a player whose
# record names none. PlayingStyles::InferPlayer in the engine makes the same
# call at load; this is the importer's copy so the database carries the answer.
ROLE_STYLES = {
    "GK": ("offensive_goalkeeper", "defensive_goalkeeper"),
    "CB": ("build_up", "the_destroyer", "extra_frontman", "none"),
    "LB": ("offensive_full_back", "full_back_finisher", "defensive_full_back"),
    "RB": ("offensive_full_back", "full_back_finisher", "defensive_full_back"),
    "DM": ("anchor_man", "box_to_box", "the_destroyer", "orchestrator"),
    "CM": ("box_to_box", "orchestrator", "hole_player", "classic_no_10"),
    "LM": ("roaming_flank", "cross_specialist", "prolific_winger", "creative_playmaker"),
    "RM": ("roaming_flank", "cross_specialist", "prolific_winger", "creative_playmaker"),
    "AM": ("creative_playmaker", "classic_no_10", "hole_player", "dummy_runner"),
    "CF": ("goal_poacher", "fox_in_the_box", "target_man", "dummy_runner"),
}
WIDE_ROLES = ("LB", "RB", "LM", "RM")


def pes_to_base(value):
    """A PES 40-99 stat value -> the engine's own 0..1 base_stat scale.

    Confirmed by the shipped teams' own flat BASE_STAT (0.62): that is
    (76.6 - 40) / 59, and 76.6 is a plausible "regular"-tier player's own mean
    rating under this exact formula, before any team had a real stat to read.
    """
    return min(1.0, max(0.0, (value - 40) / 59.0))


def wobble(key, seed):
    """A settled +-0.05, from the name and the seed and nothing else.

    crc32, not hash(): Python randomises string hashing per process, so hash()
    would give a different team every time the importer ran.
    """
    digest = zlib.crc32(("%s:%d" % (key, seed)).encode()) & 0xffff
    return (digest / 65535.0 - 0.5) * 0.10


def infer_playing_style(values, role, seed):
    """The style a player of `role` with these engine-key values would carry.

    A CF who heads better than he finishes is a Target Man and one quicker
    than he is accurate a Goal Poacher; otherwise a settled pick from the
    position's own list."""
    options = ROLE_STYLES.get(role, ("none",))
    if role == "CF":
        if values["technical_header"] > values["technical_shot"] + 0.05:
            return "target_man"
        if values["physical_velocity"] > values["technical_shot"] + 0.05:
            return "goal_poacher"
    return options[(zlib.crc32(("style:%d" % seed).encode()) & 0xffff) % len(options)]


def infer_com_styles(values, role):
    """The COM playing cards these engine-key values earn: each card goes to a
    player whose defining stats stand clearly above his own mean. A keeper
    carries none - PES gives none to keepers either."""
    if role == KEEPER_ROLE:
        return []
    outfield = [v for k, v in values.items() if k not in GK_KEYS]
    bar = sum(outfield) / len(outfield) + 0.08
    cards = []
    if values["technical_dribble"] > bar:
        cards.append("trickster")
    if values["technical_tightpossession"] > bar:
        cards.append("mazing_run")
    if (values["physical_velocity"] + values["physical_acceleration"]) / 2 > bar:
        cards.append("speeding_bullet")
    if role in WIDE_ROLES and values["technical_shot"] > bar:
        cards.append("incisive_run")
    if values["technical_highpass"] > bar:
        cards.append("long_ball_expert")
    if role in WIDE_ROLES and values["technical_curl"] > bar:
        cards.append("early_cross")
    if (values["physical_shotpower"] + values["technical_shot"]) / 2 > bar:
        cards.append("long_ranger")
    return cards[:5]


def render_profile(starts, role, seed, playing_style=None, com_styles=None):
    """-> profile_xml from a per-key 0..1 starting value each: the role's bias
    and the settled wobble go on top, then the styles - the given ones, or the
    ones the finished values earn."""
    bias = ROLE_BIAS.get(role, {})
    values = {key: min(1.0, max(0.0, starts[key] + bias.get(key, 0.0) + wobble(key, seed)))
              for key in STAT_KEYS}
    if playing_style is None:
        playing_style = infer_playing_style(values, role, seed)
    if com_styles is None:
        com_styles = infer_com_styles(values, role)
    lines = ["<%s>%.6f</%s>" % (key, values[key], key) for key in STAT_KEYS]
    lines.append("<playing_style>%s</playing_style>" % playing_style)
    lines.append("<com_styles>%s</com_styles>" % (",".join(com_styles) or "none"))
    return "\n".join(lines) + "\n"


def stat_profile_xml(stats, role, seed, abilities=None, playing_style=None,
                     com_styles=None):
    """One player's stats, as the engine's profile_xml, converted straight from
    his own decoded PES ratings.

    Same shape as profile_xml - role bias, then the same deterministic wobble -
    except each key that has a named PES analogue starts from that stat's own
    value, per player, rather than one flat number for the player's whole
    profile. A key with no PES analogue (mental_vision and the like) starts
    from this player's own mean rating across every stat he does have: his own
    overall level, not a guess at what that key specifically should be. The
    non-7-bit ratings come from `abilities` (ted.read_player_abilities) when
    the record carried them, and from that same mean otherwise. A "none"
    playing style is PES's own answer and is kept; only None (unread) infers.
    """
    mean_pes = sum(stats.values()) / len(stats)
    pes_by_key = {engine_key: stats[pes_stat]
                  for pes_stat, engine_key in PES_TO_ENGINE_STAT.items()}
    for engine_key, pes_stat in DERIVED_KEYS:
        pes_by_key[engine_key] = stats[pes_stat]
    starts = {key: pes_to_base(pes_by_key.get(key, mean_pes)) for key in STAT_KEYS}
    for engine_key, ability in ABILITY_KEYS:
        if abilities and ability in abilities:
            starts[engine_key] = (abilities[ability] - 1) / float(ABILITY_MAX[ability] - 1)
    return render_profile(starts, role, seed, playing_style, com_styles)


def profile_xml(base_stat, role, seed):
    """One player's stats, as the engine's profile_xml.

    Deterministic in (base_stat, role, seed): an importer that reshuffles stats on
    every run makes a team unrepeatable, and this gets re-run whenever a pack is
    updated. The spread is a fixed hash of the stat name and the seed, so two players
    of the same role differ without either being random.
    """
    return render_profile({key: base_stat for key in STAT_KEYS}, role, seed)


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
    """Writes `team` into `database`. -> (team row id, {shirt number: player row id}).

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
        # The pack's own colours, when it stated them. Not decoration: the
        # scoreboard, the crowd banners and the stats overlay all read these,
        # and TeamData falls back to black on white for a team without them.
        colour1 = team.get("colour1")
        colour2 = team.get("colour2")
        row = cur.execute("select id from teams where name = ?", (name,)).fetchone()
        if row:
            team_row = row[0]
            cur.execute("update teams set shortname = ?, tactics_xml = ?, "
                        "tactics_factory_xml = ?, logo_url = ?, kit_url = ?, "
                        "color1 = coalesce(?, color1), color2 = coalesce(?, color2) "
                        "where id = ?",
                        (team["abbreviation"][:3], xml, xml, logo, kit,
                         colour1, colour2, team_row))
            cur.execute("delete from players where team_id = ?", (team_row,))
        else:
            league = cur.execute("select league_id from teams where league_id is not null "
                                 "limit 1").fetchone()
            cur.execute("insert into teams(league_id, name, shortname, tactics_xml, "
                        "tactics_factory_xml, logo_url, kit_url, color1, color2) "
                        "values (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                        (league[0] if league else 1, name,
                         team["abbreviation"][:3], xml, xml, logo, kit,
                         colour1, colour2))
            team_row = cur.lastrowid

        # The squad in order. The number is the shirt; formationorder is the slot, and
        # the keeper takes the first of them. The row id each player lands on is
        # kept against that shirt number, because a 4cc pack names its model
        # exports by shirt (<k2411 - Name> is number 11) and playermodels.cfg has
        # to bind the model to the row the player actually got. Renumbering the
        # database and re-keying the models by hand is what broke them before.
        by_shirt = {}
        for slot, entry in enumerate(team["squad"]):
            player = (team["players"][slot] if slot < len(team["players"])
                      else {"name": "Player %d" % entry["number"]})
            role = KEEPER_ROLE if slot == 0 else FIELD_ROLES[(slot - 1) % len(FIELD_ROLES)]
            stats = player.get("stats")
            if stats:
                base_stat = pes_to_base(sum(stats.values()) / len(stats))
                profile = stat_profile_xml(stats, role, slot, player.get("abilities"),
                                           player.get("playing_style"),
                                           player.get("com_styles"))
            else:
                base_stat = BASE_STAT
                profile = profile_xml(BASE_STAT, role, slot)
            cur.execute(
                "insert into players(team_id, nationalteam_id, firstname, lastname, role, "
                "age, base_stat, profile_xml, skincolor, hairstyle, haircolor, height, "
                "weight, formationorder, nationalteamformationorder) "
                "values (?, 0, '', ?, ?, 25, ?, ?, 1, 'short01', 'black', 1.8, 75.0, ?, 0)",
                (team_row, player["name"][:64], role, base_stat, profile, slot))
            by_shirt[entry["number"]] = cur.lastrowid
        if dry_run:
            conn.rollback()
        else:
            conn.commit()
        return team_row, by_shirt
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
    row, _ = install(args.database, export, tactics, args.dry_run)
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

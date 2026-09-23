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

# Who actually starts, and where: PES's own Team Game Plan (ted.read_game_plan).
# Its lineup is the keeper, the ten outfield starters and the bench, in the
# order PES fields them, and Preset 1 names each starter's position and grid
# point. Before it was read, formationorder came from the export's LIST order,
# then from a misread of the byte after the lineup that fielded the wrong
# eleven on five of the seven VGL26 exports; /dbg/ took one shot a match with
# seven of its starters on the bench.
STARTING_SLOTS = 11


def seat_role(position, slot):
    """The role a player on `slot` plays: the position named for him, or the
    seat pattern where none is (fixtures, older exports), with the keeper's
    seat still the keeper's."""
    if position:
        return position
    return KEEPER_ROLE if slot == 0 else FIELD_ROLES[(slot - 1) % len(FIELD_ROLES)]


def seating(team):
    """-> (order, roles, coordinates): the seats PES's game plan hands out.

    `order` is every player index in formationorder - the lineup's keeper, ten
    outfield starters and bench, then anyone it leaves out. `roles` is one
    token per seat: the game plan's position for the eleven, the registered
    position for the rest, the seat pattern only where a record names none.
    `coordinates` is the eleven's PES grid points, or None when the export
    carries no usable game plan (older exports, fixtures) - then list order
    seats the squad and the engine's own default formation stands in.
    """
    squad = team.get("squad") or []
    players = team.get("players") or []
    plan = team.get("game_plan") or {}
    lineup = []
    for i in plan.get("lineup", []):
        if i < len(squad) and i not in lineup:
            lineup.append(i)
    usable = len(lineup) >= STARTING_SLOTS
    order = (lineup if usable else []) + [i for i in range(len(squad)) if i not in lineup]
    roles = []
    for slot, source in enumerate(order):
        planned = plan["positions"][slot] if usable and slot < STARTING_SLOTS else None
        registered = players[source].get("position") if source < len(players) else None
        roles.append(seat_role(planned or registered, slot))
    return order, roles, (plan["coordinates"] if usable else None)

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
    # PES's wingers: a wing-back's pace and crossing, a striker's touch. Its
    # second striker: the striker's eye minus his defensive load, a full step
    # off the front.
    "LW": {"physical_acceleration": +0.10, "technical_dribble": +0.08,
           "technical_tightpossession": +0.06, "technical_curl": +0.08,
           "technical_shot": +0.08, "technical_ballwinning": -0.10},
    "RW": {"physical_acceleration": +0.10, "technical_dribble": +0.08,
           "technical_tightpossession": +0.06, "technical_curl": +0.08,
           "technical_shot": +0.08, "technical_ballwinning": -0.10},
    "SS": {"technical_shot": +0.14, "technical_volley": +0.10,
           "mental_offensivepositioning": +0.12, "technical_standingtackle": -0.08,
           "mental_defensivepositioning": -0.08, "technical_ballwinning": -0.08,
           "technical_interceptions": -0.08},
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

# The Playing Styles PES allows a position, for a player whose record names
# none. PlayingStyles::InferPlayer in the engine makes the same call at load;
# this is the importer's copy so the database carries the answer.
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
    "LW": ("prolific_winger", "roaming_flank", "cross_specialist", "dummy_runner"),
    "RW": ("prolific_winger", "roaming_flank", "cross_specialist", "dummy_runner"),
    "SS": ("goal_poacher", "dummy_runner", "fox_in_the_box", "hole_player"),
    "CF": ("goal_poacher", "fox_in_the_box", "target_man", "dummy_runner"),
}
WIDE_ROLES = ("LB", "RB", "LM", "RM", "LW", "RW")

# The engine's slot order for position_familiarity (FormState::Slot): GK, CB,
# LB, RB, DM, CM, LM, RM, AM, LW, RW, SS, CF - one letter each, in that order.
FAMILIARITY_SLOTS = ("GK", "CB", "LB", "RB", "DM", "CM", "LM", "RM", "AM",
                     "LW", "RW", "SS", "CF")
FAMILIARITY_LETTERS = {0: "C", 1: "B", 2: "A"}


def familiarity_string(positions):
    """-> the engine's position_familiarity string from a record's playable
    list, or None when the record graded nothing: an explicit all-C tag would
    pin Unfamiliar everywhere, but an absent tag lets the engine infer from
    the player's role instead."""
    if not positions or not any(positions.values()):
        return None
    return "".join(FAMILIARITY_LETTERS[positions.get(slot, 0)]
                   for slot in FAMILIARITY_SLOTS)


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
    if role in ("CF", "SS"):
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


# Which skills a role can carry and the engine key each one leans on; the same
# table PlayerSkills::Infer uses, so a squad the database names no skills for
# comes out the same whether the importer or the engine fills them in.
ROLE_SKILLS = {
    "GK": (("gk_low_punt", "gk_clearing"), ("gk_high_punt", "gk_clearing"),
           ("gk_long_throw", "gk_clearing"), ("gk_penalty_saver", "gk_reflexes"),
           ("captaincy", "mental_calmness")),
    "CB": (("heading", "technical_header"), ("man_marking", "technical_ballwinning"),
           ("interception", "technical_interceptions"),
           ("acrobatic_clear", "technical_ballwinning"), ("captaincy", "mental_calmness"),
           ("fighting_spirit", "mental_resilience")),
    "LB": (("pinpoint_crossing", "technical_highpass"), ("long_throw", "physical_contact"),
           ("man_marking", "technical_ballwinning"),
           ("interception", "technical_interceptions"), ("scissors_feint", "technical_dribble"),
           ("track_back", "mental_workrate")),
    "DM": (("man_marking", "technical_ballwinning"),
           ("interception", "technical_interceptions"), ("long_range_drive", "physical_shotpower"),
           ("one_touch_pass", "technical_shortpass"), ("weighted_pass", "technical_shortpass"),
           ("captaincy", "mental_calmness")),
    "CM": (("one_touch_pass", "technical_shortpass"), ("weighted_pass", "technical_shortpass"),
           ("through_passing", "mental_vision"), ("no_look_pass", "mental_vision"),
           ("long_range_drive", "physical_shotpower"), ("track_back", "mental_workrate"),
           ("double_touch", "technical_dribble")),
    "LM": (("scissors_feint", "technical_dribble"), ("double_touch", "technical_dribble"),
           ("pinpoint_crossing", "technical_highpass"), ("outside_curler", "technical_curl"),
           ("cut_behind_turn", "technical_tightpossession"), ("rabona", "technical_dribble"),
           ("track_back", "mental_workrate")),
    "AM": (("marseille_turn", "technical_tightpossession"), ("double_touch", "technical_dribble"),
           ("through_passing", "mental_vision"), ("no_look_pass", "mental_vision"),
           ("chip_shot_control", "technical_shot"), ("dipping_shots", "technical_curl"),
           ("long_range_drive", "physical_shotpower")),
    "CF": (("first_time_shot", "technical_volley"), ("acrobatic_finishing", "technical_volley"),
           ("heading", "technical_header"), ("chip_shot_control", "technical_shot"),
           ("knuckle_shot", "physical_shotpower"), ("rising_shots", "technical_shot"),
           ("sombrero", "technical_ballcontrol"), ("super_sub", "physical_stamina")),
}
ROLE_SKILLS["RB"] = ROLE_SKILLS["LB"]
ROLE_SKILLS["RM"] = ROLE_SKILLS["LM"]
ROLE_SKILLS["LW"] = ROLE_SKILLS["LM"]
ROLE_SKILLS["RW"] = ROLE_SKILLS["RM"]
ROLE_SKILLS["SS"] = ROLE_SKILLS["CF"]


def infer_skills(values, role, seed):
    """The Player Skills a `role` player with these engine-key values would
    carry: two or three from the role's list, settled by the seed, leaning to
    the ones his own ratings back - so a flat-statted 4cc squad still gets a
    winger who steps over and a centre-half who heads."""
    options = ROLE_SKILLS.get(role, ())
    if not options:
        return []
    # Best-backed first, the seed breaking the ties a flat squad is full of.
    ranked = sorted(options,
                    key=lambda o: (-values.get(o[1], 0.5),
                                   zlib.crc32(("skill:%s:%d" % (o[0], seed)).encode())))
    count = 2 + (zlib.crc32(("skills:%d" % seed).encode()) & 1)
    return sorted(name for name, _ in ranked[:count])


def render_profile(starts, role, seed, playing_style=None, com_styles=None, skills=None,
                   familiarity=None):
    """-> profile_xml from a per-key 0..1 starting value each: the role's bias
    and the settled wobble go on top, then the styles and skills - the given
    ones, or the ones the finished values earn. `familiarity` is the engine's
    thirteen-letter position_familiarity string; None omits the tag so the
    engine infers from the role instead of being pinned Unfamiliar."""
    bias = ROLE_BIAS.get(role, {})
    values = {key: min(1.0, max(0.0, starts[key] + bias.get(key, 0.0) + wobble(key, seed)))
              for key in STAT_KEYS}
    if playing_style is None:
        playing_style = infer_playing_style(values, role, seed)
    if com_styles is None:
        com_styles = infer_com_styles(values, role)
    if skills is None:
        skills = infer_skills(values, role, seed)
    lines = ["<%s>%.6f</%s>" % (key, values[key], key) for key in STAT_KEYS]
    lines.append("<playing_style>%s</playing_style>" % playing_style)
    lines.append("<com_styles>%s</com_styles>" % (",".join(com_styles) or "none"))
    lines.append("<skills>%s</skills>" % (",".join(skills) or "none"))
    if familiarity:
        lines.append("<position_familiarity>%s</position_familiarity>" % familiarity)
    return "\n".join(lines) + "\n"


def stat_profile_xml(stats, role, seed, abilities=None, playing_style=None,
                     com_styles=None, skills=None, positions=None):
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
    playing style or an empty skill list is PES's own answer and is kept; only
    None (unread) infers.
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
    return render_profile(starts, role, seed, playing_style, com_styles, skills,
                          familiarity_string(positions))


def profile_xml(base_stat, role, seed, positions=None):
    """One player's stats, as the engine's profile_xml.

    `positions` is the record's playable list, graded 0/1/2 - or None where the
    record graded nothing, which omits the familiarity tag so the engine
    infers from the role instead of being pinned Unfamiliar. The flat baseline
    carries no PES stat of its own but still carries the player's own list.
    """
    return render_profile({key: base_stat for key in STAT_KEYS}, role, seed,
                          familiarity=familiarity_string(positions))


def stat_values(xml):
    """-> {stat: value} out of a profile_xml, for checking one."""
    return {m.group(1): float(m.group(2))
            for m in re.finditer(r"<([a-z_]+)>([\d.]+)</\1>", xml)}


# PES's game-plan grid onto the engine's -1..1 pitch (TeamData reads x as depth,
# own goal -1 to attack +1, and y as width, the team's own left positive).
# Vertical: the keeper's fixed line, 3, is the engine's keeper line (-1.0,
# formations.cpp keeperX); the line the seven VGL26 exports stand their centre
# forwards on, 47, is its attack line (0.7, attackX). That puts the exports'
# back lines (8-11) at -0.8..-0.69 against the engine's own defenceX of -0.7.
# Horizontal: 0x34 is the centre and the exports' full backs stand at 16 and
# 88, the editor's touchline, which the engine's stock formations draw at ±0.9.
# The engine then blends this 60/40 toward each role's default and clamps.
PES_GOAL_LINE = ted.GK_COORDINATE[0]
PES_FORWARD_LINE = 47
PES_CENTRE = ted.GK_COORDINATE[1]
PES_TOUCHLINE_OFFSET = 36
ENGINE_KEEPER_X = -1.0
ENGINE_ATTACK_X = 0.7
ENGINE_TOUCHLINE_Y = 0.9


def pitch_position(coordinate):
    """One PES (vertical, horizontal) grid point -> the engine's (x, y)."""
    vertical, horizontal = coordinate
    x = ENGINE_KEEPER_X + ((vertical - PES_GOAL_LINE) * (ENGINE_ATTACK_X - ENGINE_KEEPER_X)
                           / (PES_FORWARD_LINE - PES_GOAL_LINE))
    y = (PES_CENTRE - horizontal) * ENGINE_TOUCHLINE_Y / PES_TOUCHLINE_OFFSET
    return max(-1.0, min(1.0, x)), max(-1.0, min(1.0, y))


def formation_xml_for(eleven_roles, coordinates):
    """-> the engine's formation_xml for one seated eleven (keeper first), or
    "" without coordinates, which the engine reads as "use my own default".

    TeamData reads p1..p11 against the players ordered by formationorder, so
    seat N is pN+1.
    """
    if not coordinates:
        return ""
    out = []
    for i, (role, coordinate) in enumerate(zip(eleven_roles, coordinates)):
        x, y = pitch_position(coordinate)
        out.append("<p%d><position>%.2f,%.2f</position><role>%s</role></p%d>"
                   % (i + 1, x, y, role, i + 1))
    return "".join(out)


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
        # formationorder is the slot the engine fields him in, and 0-10 is the
        # starting eleven. PES's own game plan hands out the seats: the row id
        # each player lands on is still kept against his shirt number, because
        # a 4cc pack names its model exports by shirt (<k2411 - Name> is number
        # 11) and playermodels.cfg has to bind the model to the row the player
        # actually got. Renumbering the database and re-keying the models by
        # hand is what broke them before.
        order, roles, coordinates = seating(team)
        # The formation the engine draws, PES's own grid points on the engine's
        # pitch; empty where the export carries no game plan, where the
        # engine's own default stands in.
        formation = formation_xml_for(roles[:STARTING_SLOTS], coordinates)

        row = cur.execute("select id from teams where name = ?", (name,)).fetchone()
        if row:
            team_row = row[0]
            cur.execute("update teams set shortname = ?, tactics_xml = ?, "
                        "tactics_factory_xml = ?, formation_xml = ?, logo_url = ?, "
                        "kit_url = ?, "
                        "color1 = coalesce(?, color1), color2 = coalesce(?, color2) "
                        "where id = ?",
                        (team["abbreviation"][:3], xml, xml, formation, logo, kit,
                         colour1, colour2, team_row))
            cur.execute("delete from players where team_id = ?", (team_row,))
        else:
            league = cur.execute("select league_id from teams where league_id is not null "
                                 "limit 1").fetchone()
            cur.execute("insert into teams(league_id, name, shortname, tactics_xml, "
                        "tactics_factory_xml, formation_xml, logo_url, kit_url, "
                        "color1, color2) "
                        "values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                        (league[0] if league else 1, name,
                         team["abbreviation"][:3], xml, xml, formation, logo, kit,
                         colour1, colour2))
            team_row = cur.lastrowid

        by_shirt = {}
        for slot, source in enumerate(order):
            entry = team["squad"][source]
            player = (team["players"][source] if source < len(team["players"])
                      else {"name": "Player %d" % entry["number"]})
            role = roles[slot]
            stats = player.get("stats")
            if stats:
                base_stat = pes_to_base(sum(stats.values()) / len(stats))
                profile = stat_profile_xml(stats, role, slot, player.get("abilities"),
                                           player.get("playing_style"),
                                           player.get("com_styles"), player.get("skills"),
                                           player.get("positions"))
            else:
                base_stat = BASE_STAT
                profile = profile_xml(BASE_STAT, role, slot, player.get("positions"))
            cur.execute(
                "insert into players(id, team_id, nationalteam_id, firstname, lastname, role, "
                "age, base_stat, profile_xml, skincolor, hairstyle, haircolor, height, "
                "weight, formationorder, nationalteamformationorder) "
                "values (?, ?, 0, '', ?, ?, 25, ?, ?, 1, 'short01', 'black', 1.8, 75.0, ?, 0)",
                (player.get("id"), team_row, player["name"][:64], role, base_stat,
                 profile, slot))
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

"""Reads a 4cc tactical export (``.ted``).

The file is not a PES save, so pesXdecrypter does not open it - point that at a
``.ted`` and it segfaults on the garbage it makes of the header. The format is
its own thing, and a simple one:

  * 0x00..0x2f  a plaintext header. Word 0 is 24, word 1 is 1, then a u32 80,
    then the payload size (file size minus this header's 0x30), then a few
    small counts.
  * 0x30..end   the payload, XORed with a **repeating 32-byte key**.

Every file has its own key - of the seven /vg/ League 26 exports checked, all
seven differed - so there is no key to ship. It does not matter: the key is
recoverable from the file itself, because roughly three quarters of the payload
is zero padding. Taking the most common byte at each position modulo 32 gives
the key straight back, which is what `recover_key` does.

Inside the plaintext:

  0x0088  team name, NUL-terminated ("/gbfg/")
  0x0275  manager name
  0x05c5  squad order: a permutation of 1..39, the starting XI first and the
          bench after it - the same idea as the engine's formationorder
  0x08e8  player records, 312 bytes each - byte-for-byte the PES EDIT format's
          own "Player entry" (240 bytes) followed immediately by its "Player
          appearance entry" (72 bytes), documented in full at
          implyingrigged.info/wiki/Pro_Evolution_Soccer_2021/Edit_file. Not a
          bespoke 4cc format: whoever built the exporter copied the real
          record straight out of PES's own save structure, id and all.

Each player record's stats are read here field by field, from the same
byte:bit offsets the wiki gives for the Player entry - not classified into
"gold/silver/bronze" first. The 4cc's own tournament-to-tournament tier rules
(what a gold rates, how many silvers a squad gets) shift between seasons and
even between packs of the same season; a table of those rules would need
updating every time they did and would silently mis-rate any player a pack
author hand-edited off the template. Reading the real per-stat bytes needs
updating for neither.

  python3 ted.py <file.ted> [--json] [--dump-plain out.bin]
"""

import argparse
import json
import re
import struct
import sys
from collections import Counter

HEADER_SIZE = 0x30
KEY_LENGTH = 32

# One observed key, kept only so a caller can tell "the usual one" from a new
# one when reporting. Keys are per file; recover_key() is the real mechanism.
SAMPLE_KEY = bytes.fromhex(
    "738be68ecb2d9a56da9ef6e3eee1affd0b03cb03f4f044fe72e713a646850ff5")

TEAM_NAME_OFFSET = 0x0088
ABBREVIATION_OFFSET = 0x00ce
CHANTS_OFFSET = 0x0187
CHANT_SLOT = 16
CHANT_SLOTS = 4
TEAM_ID_OFFSET = 0x026c
MANAGER_OFFSET = 0x0275
SQUAD_IDS_OFFSET = 0x02c8
SQUAD_NUMBERS_OFFSET = 0x0368
SQUAD_MAX = 23
SQUAD_ORDER_OFFSET = 0x05c5
SQUAD_SIZE = 39
# The presets start with the team id repeated, then 12 slot indices and ten marks.
FORMATIONS_OFFSET = 0x03e0
FORMATION_SLOTS = 12
FORMATION_MARKS = 10
FORMATION_STRIDE = FORMATION_SLOTS + 2 * FORMATION_MARKS + 1

# The roster is a plain array of 312-byte records, each one the real PES
# "Player entry" + "Player appearance entry" pair (see the module docstring).
# 0x08f3 - REC_NAME's first, colour-hunted guess of where a record starts - was
# eleven bytes late: it lined a record's own Player Name field up with the
# wiki's 0x2b instead of its real 0x36, which is why every stat field read
# eleven bytes downstream of an id also came out wrong. Anchoring on the id
# instead - a plain 4-byte int, sequential across a roster (80301, 80302, ...)
# - fixes the whole record at once.
PLAYER_TABLE_OFFSET = 0x08e8
PLAYER_RECORD_SIZE = 312
REC_ID = 0x00
REC_NAME = 0x36
REC_SHIRT_NAME = 0x73
REC_EXTRA = 0xb0

# Player entry's own stat fields, at the wiki's byte:bit offsets exactly.
# PES: must be in range [40, 99] for every one of these; STAT_FIELDS omits the
# motion/edit-flag/registered-position fields the wiki interleaves between
# them, since those are not stats and this importer has no use for them.
STAT_FIELDS = (
    ("offensive_awareness", 0x0E, 0), ("ball_control", 0x0E, 7),
    ("tight_possession", 0x10, 0), ("low_pass", 0x10, 7),
    ("lofted_pass", 0x11, 6), ("finishing", 0x12, 5),
    ("place_kicking", 0x14, 0), ("curl", 0x14, 7),
    ("speed", 0x15, 6), ("acceleration", 0x16, 5),
    ("jump", 0x18, 0), ("physical_contact", 0x18, 7),
    ("balance", 0x19, 6), ("stamina", 0x1A, 5),
    ("ball_winning", 0x1C, 0), ("aggression", 0x1C, 7),
    ("gk_awareness", 0x1D, 6), ("gk_catching", 0x1E, 5),
    ("gk_reach", 0x20, 0), ("defensive_awareness", 0x24, 0),
    ("gk_clearing", 0x24, 7), ("heading", 0x25, 6),
    ("dribbling", 0x28, 0), ("gk_reflexes", 0x2C, 6),
    ("kicking_power", 0x2D, 5),
)

# The Player entry's non-7-bit ratings, at the same byte:bit addressing. PES
# stores each one zero-based and shows it one-based, so a stored 0 is the "1"
# on a player's card: (name, byte, bit, width, shown maximum).
ABILITY_FIELDS = (
    ("weak_foot_usage", 0x0F, 6, 2, 4),
    ("weak_foot_accuracy", 0x27, 6, 2, 4),
    ("form", 0x1F, 4, 3, 8),
    ("injury_resistance", 0x28, 7, 2, 3),
)

# The Playing Style is a 5-bit index into this table, 0 meaning "none"; the
# order is PES's own (the one every editor lists them in).
PLAYING_STYLE_FIELD = (0x22, 2, 5)
PLAYING_STYLES = (
    "none", "goal_poacher", "dummy_runner", "fox_in_the_box", "target_man",
    "creative_playmaker", "prolific_winger", "roaming_flank", "cross_specialist",
    "classic_no_10", "hole_player", "box_to_box", "the_destroyer", "orchestrator",
    "anchor_man", "build_up", "offensive_full_back", "full_back_finisher",
    "defensive_full_back", "extra_frontman", "offensive_goalkeeper",
    "defensive_goalkeeper")

# The COM Playing Styles ("playing cards"), one bit each, in PES's own order.
COM_STYLE_FIELDS = (
    ("trickster", 0x2F, 7), ("mazing_run", 0x30, 0), ("speeding_bullet", 0x30, 1),
    ("incisive_run", 0x30, 2), ("long_ball_expert", 0x30, 3), ("early_cross", 0x30, 4),
    ("long_ranger", 0x30, 5),
)

# The Player Skills: a 41-bit bitmask at 0x30:6 (right after the COM cards),
# in PES's own order - the wiki's "Bit 0 - Scissors Feint ... Bit 40 - Fighting
# Spirit". The engine's PlayerSkills::Skill enum uses the same order and the
# same tokens.
SKILL_FIELD = (0x30, 6)
SKILLS = (
    "scissors_feint", "double_touch", "flip_flap", "marseille_turn", "sombrero",
    "cross_over_turn", "cut_behind_turn", "scotch_move", "step_on_skill_control",
    "heading", "long_range_drive", "chip_shot_control", "long_range_shooting",
    "knuckle_shot", "dipping_shots", "rising_shots", "acrobatic_finishing",
    "heel_trick", "first_time_shot", "one_touch_pass", "through_passing",
    "weighted_pass", "pinpoint_crossing", "outside_curler", "rabona", "no_look_pass",
    "low_lofted_pass", "gk_low_punt", "gk_high_punt", "long_throw", "gk_long_throw",
    "penalty_specialist", "gk_penalty_saver", "gamesmanship", "man_marking",
    "track_back", "interception", "acrobatic_clear", "captaincy", "super_sub",
    "fighting_spirit")


# PES colours a name from markup inside the string, and 4cc packs use it:
#
#     0x11  escape
#     'c'   the tag: a colour follows
#     8 lowercase hex digits, RRGGBBAA
#     then the text
#
# So player 7 of HDG's export is 11 63 38 62 35 66 35 35 66 66 "I DIVE": #8b5f55,
# "I DIVE". Five of its twenty-three names carry one - 7 and 8 #8b5f55, 9 #cc9900,
# 10 and 11 #cccccc.
#
# Read as RRGGBBAA rather than AARRGGBB because all three tags end in "ff" while
# their leading six digits differ: a constant trailing pair is an opaque alpha,
# where the other reading would make them three different partial alphas over blue,
# purple and pale blue. Inference from three samples, not a spec.
NAME_ESCAPE = "\x11"
COLOUR_TAG_LENGTH = 1 + 8  # 'c' and RRGGBBAA
HEX_DIGITS = "0123456789abcdefABCDEF"


def recover_key(payload, key_length=KEY_LENGTH):
    """The repeating key, from the payload's own zero padding.

    Roughly three quarters of the payload is zero, so at each position modulo
    the key length the most common byte is the key byte itself.
    """
    return bytes(Counter(payload[i::key_length]).most_common(1)[0][0]
                 for i in range(key_length))


def decrypt(blob):
    """A whole .ted -> its plaintext payload."""
    if len(blob) <= HEADER_SIZE:
        raise ValueError("too short to be a .ted (%d bytes)" % len(blob))
    payload = blob[HEADER_SIZE:]
    key = recover_key(payload)
    plain = bytes(b ^ key[i % KEY_LENGTH] for i, b in enumerate(payload))
    # Sanity: the padding should now actually be zero. A file whose key was not
    # recoverable this way fails here rather than yielding plausible nonsense.
    if plain.count(0) < len(plain) // 4:
        raise ValueError("payload does not decrypt to mostly padding; "
                         "key recovery failed")
    return plain, key


def read_string(plain, offset, limit=64):
    end = plain.find(b"\x00", offset, offset + limit)
    if end < 0:
        end = offset + limit
    return plain[offset:end].decode("ascii", "replace").strip()


def get_bits(plain, byte_offset, bit_offset, nbits):
    """The wiki's own "byte:bit" addressing: bit 0 is a byte's least significant
    bit, and a field that does not end on a byte boundary continues into the
    next byte's low bits. Confirmed against the real, decrypted EDIT export:
    this reads Offensive Awareness, Ball Control and so on out of it correctly.
    """
    val = 0
    for i in range(nbits):
        total_bit = bit_offset + i
        byte_i = byte_offset + total_bit // 8
        bit_i = total_bit % 8
        if byte_i >= len(plain):
            break
        if plain[byte_i] & (1 << bit_i):
            val |= 1 << i
    return val


def read_player_id(plain, offset):
    if offset + 4 > len(plain):
        return 0
    return struct.unpack_from("<I", plain, offset)[0]


def read_player_stats(plain, offset):
    """-> {stat: value} for one record, straight off its own bits.

    Every value is PES's own 40-99 rating, read from exactly the byte:bit the
    wiki gives for the Player entry - not a "gold/silver/bronze" lookup. A
    tournament's tier rules, and how closely any one player actually sits to
    them, are both things this function has no opinion on.
    """
    return {name: get_bits(plain, offset + byte_off, bit_off, 7)
            for name, byte_off, bit_off in STAT_FIELDS}


def read_player_abilities(plain, offset):
    """-> {name: shown value} for the non-7-bit ratings (weak foot usage and
    accuracy 1-4, form 1-8, injury resistance 1-3), one-based as the card shows
    them. Kept apart from read_player_stats so a mean over the 40-99 ratings
    stays a mean over 40-99 ratings."""
    return {name: get_bits(plain, offset + byte_off, bit_off, width) + 1
            for name, byte_off, bit_off, width, _ in ABILITY_FIELDS}


def read_player_styles(plain, offset):
    """-> (playing style token, [com style tokens]); an index past the table
    (a corrupt or newer record) reads as "none"."""
    byte_off, bit_off, width = PLAYING_STYLE_FIELD
    index = get_bits(plain, offset + byte_off, bit_off, width)
    style = PLAYING_STYLES[index] if index < len(PLAYING_STYLES) else "none"
    com = [name for name, b, bit in COM_STYLE_FIELDS if get_bits(plain, offset + b, bit, 1)]
    return style, com


def read_player_skills(plain, offset):
    """-> [skill tokens] in PES order, one per set bit of the Player Skills field."""
    byte_off, bit_off = SKILL_FIELD
    bits = get_bits(plain, offset + byte_off, bit_off, len(SKILLS))
    return [name for i, name in enumerate(SKILLS) if bits >> i & 1]


def read_squad_order(plain):
    raw = plain[SQUAD_ORDER_OFFSET:SQUAD_ORDER_OFFSET + SQUAD_SIZE]
    return [b for b in raw]


def parse_name(field):
    """-> (text, [(index into text, "#rrggbbaa")]) for one name field.

    The colours are kept rather than thrown away: they are the pack author's, and a
    name is meant to be drawn in them. An escape that is not a colour, or a colour
    tag too short to be one, is left in the text as the literal it is - better a
    stray character than a guess that eats part of somebody's name.

    Colour changes are read wherever they appear, not only at the start: the markup
    allows it, even though HDG's export always opens with one and never changes.
    """
    text = []
    colours = []
    i = 0
    while i < len(field):
        ch = field[i]
        if ch != NAME_ESCAPE:
            if ch >= " ":
                text.append(ch)
            i += 1
            continue
        tag = field[i + 1:i + 1 + COLOUR_TAG_LENGTH]
        if (len(tag) == COLOUR_TAG_LENGTH and tag[0] == "c"
                and all(d in HEX_DIGITS for d in tag[1:])):
            colours.append((len("".join(text)), "#" + tag[1:].lower()))
            i += 1 + COLOUR_TAG_LENGTH
            continue
        # not a colour: drop the escape byte, keep what followed as text
        i += 1
    return "".join(text).strip(), colours


def strip_markup(text):
    """Just the readable name, with its markup and any control bytes gone."""
    return parse_name(text)[0]


def read_players(plain):
    """-> [{id, name, shirt_name, extra, stats, abilities, playing_style,
    com_styles, skills}] in record order, from their own fields.

    Reading the longest printable run instead was wrong twice on HDG's export: it
    dragged in the colour markup and the stray bytes of the packed block before a
    name, and where a shirt name was the longer of the two it returned that - player
    12 came out as "MY FAVOURITE" rather than "Brapdiver".
    """
    players = []
    offset = PLAYER_TABLE_OFFSET
    while offset + PLAYER_RECORD_SIZE <= len(plain):
        name, colours = parse_name(read_string(plain, offset + REC_NAME))
        if not name:
            break
        style, com = read_player_styles(plain, offset)
        players.append({
            "id": read_player_id(plain, offset + REC_ID),
            "name": name,
            "name_colour": colours[0][1] if colours else None,
            "name_colours": colours,
            "shirt_name": strip_markup(read_string(plain, offset + REC_SHIRT_NAME)),
            "extra": strip_markup(read_string(plain, offset + REC_EXTRA)),
            "stats": read_player_stats(plain, offset),
            "abilities": read_player_abilities(plain, offset),
            "playing_style": style,
            "com_styles": com,
            "skills": read_player_skills(plain, offset),
        })
        offset += PLAYER_RECORD_SIZE
    return players


def read_team_id(plain):
    if TEAM_ID_OFFSET + 4 > len(plain):
        return 0
    return struct.unpack_from("<I", plain, TEAM_ID_OFFSET)[0]


def read_abbreviation(plain):
    return read_string(plain, ABBREVIATION_OFFSET, 8)


def read_chants(plain):
    """-> the chant slots that hold one."""
    out = []
    for slot in range(CHANT_SLOTS):
        chant = read_string(plain, CHANTS_OFFSET + slot * CHANT_SLOT, CHANT_SLOT)
        if chant:
            out.append(chant)
    return out


def read_squad(plain):
    """-> [{id, number}] in team order, stopping at the first empty slot.

    This is the squad itself, as against read_squad_order's permutation: HDG's is
    ids 80301..80323 wearing 1..23.
    """
    out = []
    for slot in range(SQUAD_MAX):
        at = SQUAD_IDS_OFFSET + 4 * slot
        num_at = SQUAD_NUMBERS_OFFSET + 2 * slot
        if at + 4 > len(plain) or num_at + 2 > len(plain):
            break
        identifier = struct.unpack_from("<I", plain, at)[0]
        if identifier == 0:
            break
        out.append({"id": identifier,
                    "number": struct.unpack_from("<H", plain, num_at)[0]})
    return out


def read_formations(plain):
    """-> the formation presets: 12 slot indices and ten (x, y) marks apiece.

    The marks are kept in the units they were authored in - x about a centre of 52,
    y from 8 to 43 on HDG's export - and deliberately not rescaled. What those units
    are worth has to be calibrated against the engine's own formation coordinates,
    and guessing it would bake a wrong pitch into the import.
    """
    out = []
    at = FORMATIONS_OFFSET + 4  # the team id repeats at the head of the block
    while at + FORMATION_STRIDE <= len(plain):
        slots = list(plain[at:at + FORMATION_SLOTS])
        marks_at = at + FORMATION_SLOTS
        marks = [(plain[marks_at + 2 * m], plain[marks_at + 2 * m + 1])
                 for m in range(FORMATION_MARKS)]
        if not any(x or y for x, y in marks):
            break
        out.append({"slots": slots, "marks": marks})
        at += FORMATION_STRIDE
    return out


def read_export(path):
    plain, key = decrypt(open(path, "rb").read())
    return {
        "team": read_string(plain, TEAM_NAME_OFFSET),
        "team_id": read_team_id(plain),
        "abbreviation": read_abbreviation(plain),
        "manager": read_string(plain, MANAGER_OFFSET),
        "chants": read_chants(plain),
        "squad": read_squad(plain),
        "squad_order": read_squad_order(plain),
        "players": read_players(plain),
        "formations": read_formations(plain),
        "key": key.hex(),
        "key_is_sample": key == SAMPLE_KEY,
    }, plain


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ted")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--dump-plain", default=None,
                        help="write the decrypted payload here")
    args = parser.parse_args()

    export, plain = read_export(args.ted)
    if args.dump_plain:
        open(args.dump_plain, "wb").write(plain)

    if args.json:
        print(json.dumps(export, indent=2))
        return 0

    print("team     %s (%s), id %d" % (export["team"], export["abbreviation"],
                                       export["team_id"]))
    print("manager  %s" % export["manager"])
    print("key      %s%s" % (export["key"], "" if export["key_is_sample"] else "  (per-file)"))
    for chant in export["chants"]:
        print("chant    %s" % chant)
    print("order    %s" % " ".join("%d" % n for n in export["squad_order"][:11]))
    print("squad    %d" % len(export["squad"]))
    print("players  %d" % len(export["players"]))
    for i, player in enumerate(export["players"]):
        number = export["squad"][i]["number"] if i < len(export["squad"]) else 0
        print("   %2d  %-34s %s" % (number or i + 1, player["name"],
                                    player["shirt_name"]))
    for i, preset in enumerate(export["formations"]):
        print("form %d   %s" % (i, " ".join("%d,%d" % m for m in preset["marks"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())

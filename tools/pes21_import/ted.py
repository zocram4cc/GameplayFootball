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
  0x08e0  player records, 312 bytes each: bit-packed stats, then the player's
          name as plain ASCII near the end of the record

The stats are bit-packed and are NOT decoded here: without the record spec that
would be guesswork, and a wrong stat is worse than no stat. What this gives you
is the roster and the order, which is what the importer needs.

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

# The roster is a plain array of 312-byte records - 0x08e0 is 19 bytes early - and
# each carries three fixed fields rather than one printable run to be hunted for.
# Some records open with a KSSH or KSSP magic and some do not, so the magic is no
# use for finding them: on HDG's export it marks eight of the twenty-three.
PLAYER_TABLE_OFFSET = 0x08f3
PLAYER_RECORD_SIZE = 312
REC_NAME = 0x2b
REC_SHIRT_NAME = 0x68
REC_EXTRA = 0xa5

# 4cc writes a coloured name as cRRGGBBff<text>. The tag is markup, not a name.
COLOUR_TAG = re.compile(r"c[0-9a-fA-F]{6}ff")


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


def read_squad_order(plain):
    raw = plain[SQUAD_ORDER_OFFSET:SQUAD_ORDER_OFFSET + SQUAD_SIZE]
    return [b for b in raw]


def strip_markup(text):
    """A name with its 4cc colour tags taken out.

    Names arrive as cRRGGBBff<text> when the pack colours them, and the tag is not
    part of the man's name: "c8b5f55ffI DIVE" is I DIVE.
    """
    without_tags = COLOUR_TAG.sub("", text)
    # And control bytes are never part of a name: five of HDG's twenty-three open
    # with 0x11, which strip() leaves in place.
    return "".join(c for c in without_tags if c >= " ").strip()


def read_players(plain):
    """-> [{name, shirt_name, extra}] in record order, from their own fields.

    Reading the longest printable run instead was wrong twice on HDG's export: it
    dragged in the colour markup and the stray bytes of the packed block before a
    name, and where a shirt name was the longer of the two it returned that - player
    12 came out as "MY FAVOURITE" rather than "Brapdiver".
    """
    players = []
    offset = PLAYER_TABLE_OFFSET
    while offset + PLAYER_RECORD_SIZE <= len(plain):
        name = strip_markup(read_string(plain, offset + REC_NAME))
        if not name:
            break
        players.append({
            "name": name,
            "shirt_name": strip_markup(read_string(plain, offset + REC_SHIRT_NAME)),
            "extra": strip_markup(read_string(plain, offset + REC_EXTRA)),
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

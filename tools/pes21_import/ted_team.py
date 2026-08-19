"""Reads a 4cc .ted team file: the team, its squad, its formations, its roster.

The format was not documented anywhere. HDG_VGL26_Tactical.ted is 14,820 bytes:

  0x00  80-byte plaintext header, little-endian - 0x00 = 24, 0x02 = 1,
        0x04 = 80 (header size), 0x08 = 14740 (body size, and 14820 - 80 = 14740),
        0x0c = 10100, 0x10 = 4, 0x14 = 4, 0x20 = 25
  0x50  the body, XORed with a 32-byte repeating key

The key needs no guessing. Most of the plaintext is zero padding, so 61% of body
bytes repeat at a 32-byte period and the most common aligned 32-byte block *is* the
key; undoing it leaves 72.5% zeros and legible text. So it is derived from the file
rather than hard-coded, and the value from this file is kept only to check against.

  python3 ted_team.py <file.ted> [--out team.txt]

writes one stanza per fact, which is the format the rest of this toolchain reads.
"""

import argparse
import collections
import struct
import sys

HEADER_SIZE = 80
KEY_SIZE = 32

# The key this file yields. Kept as a check, not as an assumption.
KNOWN_KEY = bytes.fromhex(
    "94c05515d42661b49498ee8c58759bda"
    "aaeec09e581ddc0a12c699b6abeceec8")

# Where each fact sits in the deciphered body, measured on HDG_VGL26_Tactical.ted.
OFF_NAME = 0x0068
OFF_ABBREVIATION = 0x00ae
OFF_CHANTS = 0x0167
CHANT_SLOT = 16
CHANT_SLOTS = 4
OFF_TEAM_ID = 0x024c
OFF_MANAGER = 0x0255
OFF_SQUAD_IDS = 0x02a8
OFF_SQUAD_NUMBERS = 0x0348
SQUAD_MAX = 23
OFF_FORMATIONS = 0x03c0
FORMATION_SLOTS = 12          # slot indices, one per man plus a trailing count
FORMATION_MARKS = 10          # (x, y) pairs; the keeper is not among them
FORMATION_STRIDE = 12 + 2 * FORMATION_MARKS + 1
OFF_ROSTER = 0x08d3
RECORD_SIZE = 312
RECORD_MAGICS = (b"KSSH", b"KSSP")
REC_NAME = 0x2b
REC_SHIRT_NAME = 0x68
REC_EXTRA = 0xa5


def read_header(raw):
    """-> what the plaintext header says. Raises on anything that is not a .ted."""
    if len(raw) < HEADER_SIZE:
        raise ValueError("not a .ted: %d bytes is shorter than its header" % len(raw))
    fields = struct.unpack_from("<HHIII", raw, 0)
    header = {"version": fields[0], "flag": fields[1], "header_size": fields[2],
              "body_size": fields[3], "second_size": fields[4],
              "tail": struct.unpack_from("<I", raw, 0x20)[0]}
    if header["header_size"] != HEADER_SIZE:
        raise ValueError("not a .ted: header claims to be %d bytes"
                         % header["header_size"])
    return header


def derive_key(body):
    """-> the 32-byte repeating key, from the body's most common aligned block.

    Sound because the plaintext is mostly zero padding: the block that repeats most
    is the one whose plaintext is zeros, and that block is the key itself.
    """
    if len(body) < KEY_SIZE:
        raise ValueError("body of %d bytes cannot hold a key block" % len(body))
    blocks = collections.Counter(body[i:i + KEY_SIZE]
                                 for i in range(0, len(body) - KEY_SIZE + 1, KEY_SIZE))
    return blocks.most_common(1)[0][0]


def decipher(raw):
    """-> the plaintext body."""
    read_header(raw)
    body = raw[HEADER_SIZE:]
    key = derive_key(body)
    return bytes(body[i] ^ key[i % KEY_SIZE] for i in range(len(body)))


def _text(body, offset, limit=64):
    """A NUL-terminated string at offset, or "" past the end."""
    if offset >= len(body):
        return ""
    end = body.find(b"\x00", offset, min(offset + limit, len(body)))
    if end < 0:
        end = min(offset + limit, len(body))
    return body[offset:end].decode("latin1")


def team(body):
    """-> {id, name, abbreviation, manager, chants}."""
    chants = []
    for slot in range(CHANT_SLOTS):
        chant = _text(body, OFF_CHANTS + slot * CHANT_SLOT, CHANT_SLOT)
        if chant:
            chants.append(chant)
    identifier = 0
    if OFF_TEAM_ID + 4 <= len(body):
        identifier = struct.unpack_from("<I", body, OFF_TEAM_ID)[0]
    return {"id": identifier, "name": _text(body, OFF_NAME),
            "abbreviation": _text(body, OFF_ABBREVIATION),
            "manager": _text(body, OFF_MANAGER), "chants": chants}


def squad(body):
    """-> [{id, number}] in team order, stopping at the first empty slot."""
    out = []
    for slot in range(SQUAD_MAX):
        at = OFF_SQUAD_IDS + 4 * slot
        num_at = OFF_SQUAD_NUMBERS + 2 * slot
        if at + 4 > len(body) or num_at + 2 > len(body):
            break
        identifier = struct.unpack_from("<I", body, at)[0]
        if identifier == 0:
            break
        out.append({"id": identifier,
                    "number": struct.unpack_from("<H", body, num_at)[0]})
    return out


def player_offsets(body):
    """-> every record's offset, found by its magic rather than by a stride.

    The records are not one array. On HDG_VGL26_Tactical.ted they come in runs of
    two or three at 312 bytes apart, separated by gaps of 2,808 and 2,184 bytes that
    hold something else - so walking a fixed stride finds three of them and stops.
    """
    out = []
    at = 0
    while True:
        found = -1
        for magic in RECORD_MAGICS:
            where = body.find(magic, at)
            if where >= 0 and (found < 0 or where < found):
                found = where
        if found < 0 or found + RECORD_SIZE > len(body):
            break
        out.append(found)
        at = found + 4
    return out


def players(body):
    """-> the roster's records, one per magic found."""
    return [{"offset": at,
             "name": _text(body, at + REC_NAME),
             "shirt_name": _text(body, at + REC_SHIRT_NAME),
             "extra": _text(body, at + REC_EXTRA)}
            for at in player_offsets(body)]


def formations(body):
    """-> the formation presets: slot indices and ten (x, y) marks apiece.

    The marks are kept in the units they were authored in - x about a centre of 52,
    y from 8 to about 42 on this file - and deliberately not rescaled. What those
    units are worth has to be calibrated against the engine's own formation
    coordinates; guessing it would bake a wrong pitch into the import.
    """
    out = []
    at = OFF_FORMATIONS + 4  # the team id repeats at the head of the block
    while at + FORMATION_STRIDE <= len(body):
        slots = list(body[at:at + FORMATION_SLOTS])
        marks_at = at + FORMATION_SLOTS
        marks = [(body[marks_at + 2 * m], body[marks_at + 2 * m + 1])
                 for m in range(FORMATION_MARKS)]
        if not any(x or y for x, y in marks):
            break
        out.append({"slots": slots, "marks": marks})
        at += FORMATION_STRIDE
    return out


def manifest_text(header, info, roster, squad_list, presets):
    lines = ["# A 4cc team, read out of its .ted (tools/pes21_import/ted_team.py).",
             "team %d" % info["id"], "name %s" % info["name"]]
    if info["abbreviation"]:
        lines.append("abbreviation %s" % info["abbreviation"])
    if info["manager"]:
        lines.append("manager %s" % info["manager"])
    for chant in info["chants"]:
        lines.append("chant %s" % chant)
    for entry in squad_list:
        lines.append("squad %d %d" % (entry["id"], entry["number"]))
    for index, player in enumerate(roster):
        lines.append("player %d %s" % (index, player["name"]))
        if player["shirt_name"]:
            lines.append("shirtname %d %s" % (index, player["shirt_name"]))
    for index, preset in enumerate(presets):
        lines.append("formation %d slots %s"
                     % (index, " ".join(str(s) for s in preset["slots"])))
        lines.append("formation %d marks %s"
                     % (index, " ".join("%d,%d" % m for m in preset["marks"])))
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ted")
    parser.add_argument("--out", default=None)
    args = parser.parse_args()
    raw = open(args.ted, "rb").read()
    header = read_header(raw)
    body = decipher(raw)
    info = team(body)
    roster = players(body)
    squad_list = squad(body)
    presets = formations(body)
    print("%s: team %d %r (%s), manager %r, %d chant(s)"
          % (args.ted, info["id"], info["name"], info["abbreviation"],
             info["manager"], len(info["chants"])))
    print("  squad %d, roster %d record(s), %d formation preset(s)"
          % (len(squad_list), len(roster), len(presets)))
    if derive_key(raw[HEADER_SIZE:]) != KNOWN_KEY:
        print("  note: this file's key is not the one HDG_VGL26_Tactical.ted uses")
    out = args.out
    if out:
        open(out, "w").write(manifest_text(header, info, roster, squad_list, presets))
        print("  wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

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
MANAGER_OFFSET = 0x0275
SQUAD_ORDER_OFFSET = 0x05c5
SQUAD_SIZE = 39
PLAYER_TABLE_OFFSET = 0x08e0
PLAYER_RECORD_SIZE = 312


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


def read_players(plain):
    """Names in record order. A record's name is the longest printable run in
    it, which is robust against the bit-packed stats before it."""
    players = []
    offset = PLAYER_TABLE_OFFSET
    while offset + PLAYER_RECORD_SIZE <= len(plain):
        record = plain[offset:offset + PLAYER_RECORD_SIZE]
        runs = re.findall(rb"[\x20-\x7e]{3,}", record)
        runs = [r for r in runs if not r.startswith(b"w" * 4)]
        name = max(runs, key=len).decode("ascii", "replace").strip() if runs else ""
        if not name:
            break
        players.append(name)
        offset += PLAYER_RECORD_SIZE
    return players


def read_export(path):
    plain, key = decrypt(open(path, "rb").read())
    return {
        "team": read_string(plain, TEAM_NAME_OFFSET),
        "manager": read_string(plain, MANAGER_OFFSET),
        "squad_order": read_squad_order(plain),
        "players": read_players(plain),
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

    print("team     %s" % export["team"])
    print("manager  %s" % export["manager"])
    print("key      %s%s" % (export["key"], "" if export["key_is_sample"] else "  (per-file)"))
    print("order    %s" % " ".join("%d" % n for n in export["squad_order"][:11]))
    print("players  %d" % len(export["players"]))
    for i, name in enumerate(export["players"]):
        print("   %2d  %s" % (i + 1, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())

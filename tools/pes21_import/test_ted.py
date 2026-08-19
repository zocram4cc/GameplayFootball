"""Tests for reading a 4cc tactical export (.ted).

ted.py already recovered the container: a 0x30 plaintext header and a payload XORed
with a per-file 32-byte repeating key, recoverable because three quarters of the
payload is zero padding. What these tests pin is the record layout, which was being
guessed at.

Reading a player's name as "the longest printable run in the record" is robust
against the bit-packed stats but wrong twice over on HDG's export: it drags in the
4cc colour markup that precedes a name ("c8b5f55ffI DIVE", "ccc9900ffI'm not gonna
sugarcoat it") and stray bytes of the packed block ("fPBullet Sponge", "%Lobby
doko"), and where a shirt name is longer than the name it returns the shirt name
instead - player 12 came out as "MY FAVOURITE" rather than "Brapdiver".

The records are a plain array of 312 bytes from 0x08f3 - not 0x08e0, which is 19
bytes early - and each carries three fixed fields: the name at +0x2b, the shirt name
at +0x68 and a third string at +0xa5, usually "PLACEHOLDER". Some records open with
a KSSH or KSSP magic and some do not, which is why hunting the magic finds eight of
HDG's twenty-three.

The rest of the payload holds more than the roster and the order:

  0x00ce  abbreviation ("HBR")
  0x0187  four 16-byte chant slots
  0x026c  team id (803)
  0x02c8  squad player ids, uint32 (80301..80323)
  0x0368  squad shirt numbers, uint16 (1..23)
  0x03e0  the team id again, then formation presets: 12 slot indices and ten
          (x, y) marks apiece, three of them on this file

Run: python3 -m unittest test_ted -v
"""

import struct
import unittest

import ted


def payload(size=0x1000):
    return bytearray(size)


def encode(plain, key=b"\x5a" * 32):
    header = bytearray(ted.HEADER_SIZE)
    struct.pack_into("<HHII", header, 0, 24, 1, ted.HEADER_SIZE, len(plain))
    return bytes(header) + bytes(plain[i] ^ key[i % 32] for i in range(len(plain)))


class TheContainer(unittest.TestCase):
    def test_a_payload_of_zeros_comes_back_as_zeros(self):
        plain, key = ted.decrypt(encode(payload(256)))
        self.assertEqual(plain, bytes(256))
        self.assertEqual(key, b"\x5a" * 32)

    def test_the_key_is_per_file_and_recovered_from_it(self):
        own = bytes(range(32))
        plain, key = ted.decrypt(encode(payload(256), own))
        self.assertEqual(key, own)
        self.assertFalse(key == ted.SAMPLE_KEY)


def record(name, shirt, extra="PLACEHOLDER", magic=b""):
    rec = bytearray(ted.PLAYER_RECORD_SIZE)
    if magic:
        rec[0:4] = magic
    rec[ted.REC_NAME:ted.REC_NAME + len(name)] = name.encode()
    rec[ted.REC_SHIRT_NAME:ted.REC_SHIRT_NAME + len(shirt)] = shirt.encode()
    rec[ted.REC_EXTRA:ted.REC_EXTRA + len(extra)] = extra.encode()
    return bytes(rec)


class ThePlayerRecords(unittest.TestCase):
    def roster(self, *records):
        plain = payload(ted.PLAYER_TABLE_OFFSET)
        for rec in records:
            plain += rec
        return ted.read_players(bytes(plain))

    def test_a_name_comes_from_its_own_field(self):
        players = self.roster(record("Bullet Sponge", "JUST SHOOT IT"))
        self.assertEqual(players[0]["name"], "Bullet Sponge")
        self.assertEqual(players[0]["shirt_name"], "JUST SHOOT IT")

    def test_a_longer_shirt_name_does_not_become_the_name(self):
        # player 12: "Brapdiver" against a shirt name of "MY FAVOURITE"
        players = self.roster(record("Brapdiver", "MY FAVOURITE"))
        self.assertEqual(players[0]["name"], "Brapdiver")

    def test_the_colour_markup_is_not_part_of_the_name(self):
        # 4cc writes a name as cRRGGBBff<name>; the tag is not the man's name
        players = self.roster(record("c8b5f55ffI DIVE", "I DIVE"))
        self.assertEqual(players[0]["name"], "I DIVE")

    def test_markup_in_the_middle_of_a_name_goes_too(self):
        players = self.roster(record("cccccccffJohn Helldiver", "HELLDIVER"))
        self.assertEqual(players[0]["name"], "John Helldiver")

    def test_a_leading_control_byte_is_not_part_of_the_name(self):
        # players 7 to 11 of HDG's export start with 0x11, which strip() leaves
        players = self.roster(record("\x11I DIVE", "I DRIVE"))
        self.assertEqual(players[0]["name"], "I DIVE")

    def test_control_bytes_anywhere_go(self):
        players = self.roster(record("John\x02 Helldiver\x11", "JOHN"))
        self.assertEqual(players[0]["name"], "John Helldiver")

    def test_a_name_that_merely_starts_with_c_is_left_alone(self):
        players = self.roster(record("cabbage", "CABBAGE"))
        self.assertEqual(players[0]["name"], "cabbage")

    def test_records_with_and_without_the_magic_are_both_read(self):
        players = self.roster(record("A", "AA", magic=b"KSSH"),
                              record("B", "BB"),
                              record("C", "CC", magic=b"KSSP"))
        self.assertEqual([p["name"] for p in players], ["A", "B", "C"])

    def test_an_empty_record_ends_the_roster(self):
        players = self.roster(record("A", "AA"), bytes(ted.PLAYER_RECORD_SIZE))
        self.assertEqual(len(players), 1)


def team_payload():
    plain = payload(0x0500)
    plain[0x0088:0x0088 + 6] = b"/hdg/\x00"
    plain[0x00ce:0x00ce + 4] = b"HBR\x00"
    for i, chant in enumerate((b"DEATH TO SWEDEN", b"TOTAL BUG DEATH",
                               b"TOTAL BOT DEATH", b"ALL HAIL SE")):
        plain[0x0187 + i * 16:0x0187 + i * 16 + len(chant)] = chant
    struct.pack_into("<I", plain, 0x026c, 803)
    for k in range(23):
        struct.pack_into("<I", plain, 0x02c8 + 4 * k, 80301 + k)
        struct.pack_into("<H", plain, 0x0368 + 2 * k, k + 1)
    struct.pack_into("<I", plain, 0x03e0, 803)
    plain[0x03e4:0x03e4 + 12] = bytes([0, 1, 1, 2, 3, 4, 5, 5, 8, 0x0b, 0x0c, 3])
    plain[0x03f0:0x03f0 + 20] = bytes([0x34, 0x08, 0x40, 0x08, 0x28, 0x0b, 0x10, 0x0b,
                                       0x58, 0x10, 0x34, 0x15, 0x20, 0x15, 0x48, 0x1d,
                                       0x34, 0x2a, 0x25, 0x2a])
    return bytes(plain)


class TheTeamItself(unittest.TestCase):
    def test_the_id_and_the_abbreviation(self):
        self.assertEqual(ted.read_team_id(team_payload()), 803)
        self.assertEqual(ted.read_abbreviation(team_payload()), "HBR")

    def test_every_chant_it_ships(self):
        self.assertEqual(ted.read_chants(team_payload()),
                         ["DEATH TO SWEDEN", "TOTAL BUG DEATH", "TOTAL BOT DEATH",
                          "ALL HAIL SE"])

    def test_an_empty_chant_slot_is_not_a_chant(self):
        plain = bytearray(team_payload())
        plain[0x0187 + 32:0x0187 + 48] = bytes(16)
        self.assertEqual(len(ted.read_chants(bytes(plain))), 3)


class TheSquad(unittest.TestCase):
    def test_every_player_id_with_his_shirt_number(self):
        squad = ted.read_squad(team_payload())
        self.assertEqual(len(squad), 23)
        self.assertEqual(squad[0], {"id": 80301, "number": 1})
        self.assertEqual(squad[22], {"id": 80323, "number": 23})

    def test_it_stops_at_the_first_empty_slot(self):
        plain = bytearray(team_payload())
        struct.pack_into("<I", plain, 0x02c8 + 4 * 11, 0)
        self.assertEqual(len(ted.read_squad(bytes(plain))), 11)


class TheFormations(unittest.TestCase):
    """The marks are kept in the units they were authored in - x about a centre of
    52, y from 8 to 43 on this file - and deliberately not rescaled. What they are
    worth has to be calibrated against the engine's own formation coordinates;
    guessing would bake a wrong pitch into the import.
    """

    def test_a_preset_gives_up_its_slots_and_its_marks(self):
        preset = ted.read_formations(team_payload())[0]
        self.assertEqual(preset["slots"][:4], [0, 1, 1, 2])
        self.assertEqual(len(preset["marks"]), 10)
        self.assertEqual(preset["marks"][0], (52, 8))
        self.assertEqual(preset["marks"][9], (37, 42))

    def test_nothing_there_is_no_presets(self):
        self.assertEqual(ted.read_formations(bytes(0x0100)), [])

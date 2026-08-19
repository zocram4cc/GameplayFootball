"""Tests for reading a 4cc .ted team file.

HDG_VGL26_Tactical.ted is 14,820 bytes and was not a documented format. What it is:

  0x00  80-byte plaintext header, little-endian:
        0x00 = 24, 0x02 = 1, 0x04 = 80 (header size), 0x08 = 14740 (body size,
        and 14820 - 80 = 14740), 0x0c = 10100, 0x10 = 4, 0x14 = 4, 0x20 = 25
  0x50  the body, XORed with a 32-byte repeating key

The key is not a secret to be guessed: 61% of body bytes repeat at a 32-byte period
because most of the plaintext is zero padding, so the most common aligned 32-byte
block IS the key. Undoing it leaves 72.5% zero bytes and legible text.

Inside the deciphered body:

  0x0068  team name, NUL-terminated ("/hdg/")
  0x00ae  abbreviation ("HBR")
  0x0167  four 16-byte chant slots ("DEATH TO SWEDEN", "TOTAL BUG DEATH",
          "TOTAL BOT DEATH", "ALL HAIL SE")
  0x024c  team id (803)
  0x0255  manager name ("MANAGER")
  0x02a8  23 player ids, uint32 (80301..80323)
  0x0348  23 shirt numbers, uint16 (1..23)
  0x03c0  formation presets
  0x08d3  40 player records of 312 bytes, "KSSH"/"KSSP", name at +0x2a,
          shirt name at +0x68, a third string at +0xa5. The records are not one
          array: they come in runs of two or three, separated by gaps of 2,808 and
          2,184 bytes, so they are found by their magic and not by a stride.

Run: python3 -m unittest test_ted_team -v
"""

import struct
import unittest

import ted_team


def build(body):
    """A .ted of our own: the real header shape, and body XORed with the real key."""
    header = bytearray(80)
    struct.pack_into("<HHIII", header, 0, 24, 1, 80, len(body), 0)
    struct.pack_into("<I", header, 0x20, 25)
    cipher = bytes(body[i] ^ ted_team.KNOWN_KEY[i % 32] for i in range(len(body)))
    return bytes(header) + cipher


class TheContainer(unittest.TestCase):
    def test_the_header_says_where_the_body_starts_and_how_long_it_is(self):
        raw = build(bytes(320))
        head = ted_team.read_header(raw)
        self.assertEqual(head["header_size"], 80)
        self.assertEqual(head["body_size"], 320)

    def test_a_body_of_zeros_deciphers_to_zeros(self):
        # which is also the proof that the modal block is the key
        self.assertEqual(ted_team.decipher(build(bytes(320))), bytes(320))

    def test_the_key_is_derived_from_the_file_rather_than_assumed(self):
        body = bytearray(320)
        body[0:5] = b"/abc/"
        derived = ted_team.derive_key(build(body)[80:])
        self.assertEqual(derived, ted_team.KNOWN_KEY)

    def test_a_body_too_short_to_hold_a_block_is_refused(self):
        with self.assertRaises(ValueError):
            ted_team.decipher(bytes(80) + bytes(9))

    def test_something_that_is_not_a_ted_is_refused(self):
        with self.assertRaises(ValueError):
            ted_team.read_header(b"nowhere near long enough")


def team_body():
    """A body carrying the team block at the offsets the real file uses."""
    body = bytearray(0x0400)
    body[0x0068:0x0068 + 6] = b"/hdg/\x00"
    body[0x00ae:0x00ae + 4] = b"HBR\x00"
    for i, chant in enumerate((b"DEATH TO SWEDEN", b"TOTAL BUG DEATH",
                               b"TOTAL BOT DEATH", b"ALL HAIL SE")):
        at = 0x0167 + i * 16
        body[at:at + len(chant)] = chant
    struct.pack_into("<I", body, 0x024c, 803)
    body[0x0255:0x0255 + 8] = b"MANAGER\x00"
    for k in range(23):
        struct.pack_into("<I", body, 0x02a8 + 4 * k, 80301 + k)
        struct.pack_into("<H", body, 0x0348 + 2 * k, k + 1)
    return bytes(body)


class TheTeam(unittest.TestCase):
    def setUp(self):
        self.team = ted_team.team(team_body())

    def test_the_name_and_the_abbreviation(self):
        self.assertEqual(self.team["name"], "/hdg/")
        self.assertEqual(self.team["abbreviation"], "HBR")

    def test_the_id(self):
        self.assertEqual(self.team["id"], 803)

    def test_the_manager(self):
        self.assertEqual(self.team["manager"], "MANAGER")

    def test_every_chant_it_ships(self):
        self.assertEqual(self.team["chants"],
                         ["DEATH TO SWEDEN", "TOTAL BUG DEATH", "TOTAL BOT DEATH",
                          "ALL HAIL SE"])

    def test_an_empty_chant_slot_is_not_a_chant(self):
        body = bytearray(team_body())
        body[0x0187:0x0187 + 16] = bytes(16)
        self.assertEqual(len(ted_team.team(bytes(body))["chants"]), 3)


class TheSquad(unittest.TestCase):
    def test_every_player_with_his_shirt_number(self):
        squad = ted_team.squad(team_body())
        self.assertEqual(len(squad), 23)
        self.assertEqual(squad[0], {"id": 80301, "number": 1})
        self.assertEqual(squad[22], {"id": 80323, "number": 23})

    def test_the_list_stops_at_the_first_empty_slot(self):
        body = bytearray(team_body())
        struct.pack_into("<I", body, 0x02a8 + 4 * 11, 0)
        self.assertEqual(len(ted_team.squad(bytes(body))), 11)


def one_record(name, shirt, extra="PLACEHOLDER", magic=b"KSSH"):
    rec = bytearray(312)
    rec[0:4] = magic
    rec[0x2b:0x2b + len(name)] = name.encode()
    rec[0x68:0x68 + len(shirt)] = shirt.encode()
    rec[0xa5:0xa5 + len(extra)] = extra.encode()
    return bytes(rec)


class ThePlayerRecords(unittest.TestCase):
    def test_a_record_gives_up_its_names(self):
        body = bytearray(0x08d3) + bytearray(one_record("Bullet Sponge", "JUST SHOOT IT"))
        players = ted_team.players(bytes(body))
        self.assertEqual(len(players), 1)
        self.assertEqual(players[0]["name"], "Bullet Sponge")
        self.assertEqual(players[0]["shirt_name"], "JUST SHOOT IT")
        self.assertEqual(players[0]["extra"], "PLACEHOLDER")

    def test_both_record_magics_are_accepted(self):
        body = bytearray(0x08d3)
        body += one_record("A", "AA", magic=b"KSSH")
        body += one_record("B", "BB", magic=b"KSSP")
        self.assertEqual([p["name"] for p in ted_team.players(bytes(body))], ["A", "B"])

    def test_records_are_found_by_their_magic_and_not_by_a_stride(self):
        # on the real file they come in runs of two or three with 2,808- and
        # 2,184-byte gaps between them, so walking a stride finds three and stops
        body = bytearray(0x08d3)
        body += one_record("A", "AA")
        body += bytes(2808 - 312)
        body += one_record("B", "BB")
        self.assertEqual([p["name"] for p in ted_team.players(bytes(body))], ["A", "B"])

    def test_no_roster_at_all_is_no_players(self):
        self.assertEqual(ted_team.players(bytes(0x08d3)), [])


class TheFormations(unittest.TestCase):
    """Each preset is 11 slot indices then ten (x, y) marks, three variants apiece.

    The grid is recorded as authored - x around a centre of 52, y from 8 to about 42
    on this file - and deliberately not rescaled here: what those units are worth in
    metres has to be calibrated against the engine's own formation coordinates, and
    guessing it would bake a wrong pitch into the import.
    """

    def test_a_preset_gives_up_its_marks(self):
        body = bytearray(0x03c0 + 4)
        struct.pack_into("<I", body, 0x03c0, 803)
        block = bytes([0, 1, 1, 2, 3, 4, 5, 5, 8, 0x0b, 0x0c, 3])
        marks = bytes([0x34, 0x08, 0x40, 0x08, 0x28, 0x0b, 0x10, 0x0b, 0x58, 0x10,
                       0x34, 0x15, 0x20, 0x15, 0x48, 0x1d, 0x34, 0x2a, 0x25, 0x2a])
        body += block + marks + bytes([0x43])
        preset = ted_team.formations(bytes(body))[0]
        self.assertEqual(preset["slots"][:4], [0, 1, 1, 2])
        self.assertEqual(len(preset["marks"]), 10)
        self.assertEqual(preset["marks"][0], (52, 8))
        self.assertEqual(preset["marks"][9], (37, 42))

    def test_nothing_there_is_no_presets(self):
        self.assertEqual(ted_team.formations(bytes(0x0100)), [])

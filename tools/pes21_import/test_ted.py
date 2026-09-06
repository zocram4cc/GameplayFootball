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


def set_bits(rec, byte_offset, bit_offset, nbits, value):
    """The write side of ted.get_bits, for building synthetic records."""
    for i in range(nbits):
        total_bit = bit_offset + i
        byte_i = byte_offset + total_bit // 8
        bit_i = total_bit % 8
        if value & (1 << i):
            rec[byte_i] |= 1 << bit_i
        else:
            rec[byte_i] &= ~(1 << bit_i) & 0xff


def record(name, shirt, extra="PLACEHOLDER", magic=b"", player_id=None, stats=None,
           abilities=None, playing_style=None, com_styles=(), skills=()):
    rec = bytearray(ted.PLAYER_RECORD_SIZE)
    if magic:
        rec[0:4] = magic
    if player_id is not None:
        struct.pack_into("<I", rec, ted.REC_ID, player_id)
    for stat_name, byte_off, bit_off in ted.STAT_FIELDS:
        if stats and stat_name in stats:
            set_bits(rec, byte_off, bit_off, 7, stats[stat_name])
    for ability, byte_off, bit_off, width, _ in ted.ABILITY_FIELDS:
        if abilities and ability in abilities:
            set_bits(rec, byte_off, bit_off, width, abilities[ability] - 1)
    if playing_style is not None:
        byte_off, bit_off, width = ted.PLAYING_STYLE_FIELD
        set_bits(rec, byte_off, bit_off, width, ted.PLAYING_STYLES.index(playing_style))
    for com, byte_off, bit_off in ted.COM_STYLE_FIELDS:
        if com in com_styles:
            set_bits(rec, byte_off, bit_off, 1, 1)
    byte_off, bit_off = ted.SKILL_FIELD
    set_bits(rec, byte_off, bit_off, len(ted.SKILLS),
             sum(1 << ted.SKILLS.index(skill) for skill in skills))
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
        # 4cc writes a coloured name as 0x11 c RRGGBBAA <name>; the tag is markup
        players = self.roster(record("\x11c8b5f55ffI DIVE", "I DIVE"))
        self.assertEqual(players[0]["name"], "I DIVE")

    def test_the_same_tag_without_its_escape_is_ordinary_text(self):
        # and it has to be, or a name that begins "c8b5f55ff" could not be spelled
        players = self.roster(record("c8b5f55ffI DIVE", "I DIVE"))
        self.assertEqual(players[0]["name"], "c8b5f55ffI DIVE")

    def test_markup_before_a_name_goes_and_the_name_stays(self):
        players = self.roster(record("\x11cccccccffJohn Helldiver", "HELLDIVER"))
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


class ThePlayerStats(unittest.TestCase):
    """Every stat is read from its own byte:bit - the same offsets the wiki
    gives for the PES EDIT format's Player entry, because that is exactly what
    a record's first 240 bytes are, id included. No tier lookup: a hand-edited
    player who does not match his tier's own template is read exactly as he
    is, not corrected back to it - and a tournament that changes what a gold
    rates next season needs nothing here to change with it.
    """

    def roster(self, *records):
        plain = payload(ted.PLAYER_TABLE_OFFSET)
        for rec in records:
            plain += rec
        return ted.read_players(bytes(plain))

    def test_the_id_is_a_plain_four_byte_int(self):
        players = self.roster(record("Bullet Sponge", "JUST SHOOT IT", player_id=80301))
        self.assertEqual(players[0]["id"], 80301)

    def test_a_stat_comes_back_as_its_own_pes_value(self):
        players = self.roster(record("Gold", "G", stats={"offensive_awareness": 99}))
        self.assertEqual(players[0]["stats"]["offensive_awareness"], 99)

    def test_every_named_stat_is_present(self):
        players = self.roster(record("Nobody", "N"))
        for stat_name, _, _ in ted.STAT_FIELDS:
            self.assertIn(stat_name, players[0]["stats"])

    def test_adjacent_stats_sharing_a_byte_do_not_bleed_into_each_other(self):
        # Offensive Awareness (bits 0-6) and Ball Control (bits 7-13) share 0x0E
        players = self.roster(record("Two Stats", "T",
                                      stats={"offensive_awareness": 40, "ball_control": 99}))
        self.assertEqual(players[0]["stats"]["offensive_awareness"], 40)
        self.assertEqual(players[0]["stats"]["ball_control"], 99)

    def test_a_stat_that_crosses_a_byte_boundary_round_trips(self):
        # Lofted Pass starts at bit 6 of 0x11 and finishes inside 0x12
        players = self.roster(record("Crosses", "C", stats={"lofted_pass": 77}))
        self.assertEqual(players[0]["stats"]["lofted_pass"], 77)

    def test_two_players_can_disagree_with_their_own_tier(self):
        # real packs do this: one silver reads Defensive Awareness 60, its
        # squadmate under the same colour mark reads 50 - a real per-player
        # value, not a typo to be normalised away
        players = self.roster(
            record("Normal Silver", "A", stats={"defensive_awareness": 60}),
            record("Odd Silver", "B", stats={"defensive_awareness": 50}))
        self.assertEqual(players[0]["stats"]["defensive_awareness"], 60)
        self.assertEqual(players[1]["stats"]["defensive_awareness"], 50)

    def test_the_non_seven_bit_ratings_read_as_the_card_shows_them(self):
        # stored zero-based, shown one-based: a stored 0 is the card's "1"
        players = self.roster(
            record("Lefty", "L", abilities={"weak_foot_usage": 4, "weak_foot_accuracy": 1,
                                             "form": 8, "injury_resistance": 3}),
            record("Blank", "B"))
        self.assertEqual(players[0]["abilities"],
                         {"weak_foot_usage": 4, "weak_foot_accuracy": 1,
                          "form": 8, "injury_resistance": 3})
        self.assertEqual(players[1]["abilities"],
                         {"weak_foot_usage": 1, "weak_foot_accuracy": 1,
                          "form": 1, "injury_resistance": 1})

    def test_injury_resistance_does_not_bleed_into_dribbling(self):
        # Dribbling is bits 0-6 of 0x28 and Injury Resistance the two bits after it
        players = self.roster(record("Glass", "G", stats={"dribbling": 99},
                                      abilities={"injury_resistance": 1}))
        self.assertEqual(players[0]["stats"]["dribbling"], 99)
        self.assertEqual(players[0]["abilities"]["injury_resistance"], 1)

    def test_the_playing_style_index_names_pes_own_style(self):
        players = self.roster(record("Ten", "T", playing_style="classic_no_10"),
                              record("Sweeper", "S", playing_style="offensive_goalkeeper"),
                              record("Plain", "P"))
        self.assertEqual(players[0]["playing_style"], "classic_no_10")
        self.assertEqual(players[1]["playing_style"], "offensive_goalkeeper")
        self.assertEqual(players[2]["playing_style"], "none")

    def test_an_index_past_the_table_reads_as_none(self):
        rec = bytearray(record("Future", "F"))
        byte_off, bit_off, width = ted.PLAYING_STYLE_FIELD
        set_bits(rec, byte_off, bit_off, width, 31)
        self.assertEqual(self.roster(bytes(rec))[0]["playing_style"], "none")

    def test_the_com_cards_come_back_in_pes_order(self):
        # Trickster is the top bit of 0x2F, the other six the low bits of 0x30
        players = self.roster(
            record("Cards", "C", com_styles=("long_ranger", "trickster", "incisive_run")),
            record("Plain", "P"))
        self.assertEqual(players[0]["com_styles"], ["trickster", "incisive_run", "long_ranger"])
        self.assertEqual(players[1]["com_styles"], [])

    def test_the_skills_come_back_in_pes_bit_order_and_leave_the_cards_alone(self):
        # The 41-bit skill field starts at 0x30:6, right after Long Ranger at
        # 0x30:5: bit 0 is Scissors Feint, bit 40 Fighting Spirit.
        players = self.roster(
            record("Skilled", "S", com_styles=("long_ranger",),
                   skills=("fighting_spirit", "scissors_feint", "rabona", "gk_low_punt")),
            record("Plain", "P"))
        self.assertEqual(players[0]["skills"],
                         ["scissors_feint", "rabona", "gk_low_punt", "fighting_spirit"])
        self.assertEqual(players[0]["com_styles"], ["long_ranger"])
        self.assertEqual(players[1]["skills"], [])
        self.assertEqual(len(ted.SKILLS), 41)


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


class ColouredNames(unittest.TestCase):
    """PES colours a name from markup inside the string, and 4cc packs use it.

    Five of HDG's twenty-three names carry it. The grammar, read off the bytes:

        0x11  escape
        'c'   the tag: a colour follows
        8 lowercase hex digits, RRGGBBAA
        then the text

    So player 7 is  11 63 38 62 35 66 35 35 66 66 'I DIVE'  =  #8b5f55, "I DIVE".
    Players 7 and 8 are #8b5f55, 9 is #cc9900, 10 and 11 are #cccccc.

    RRGGBBAA rather than AARRGGBB because all three tags end in "ff" while their
    leading six digits differ: a constant trailing pair is an opaque alpha, whereas
    the other reading would make them three different partial alphas over blue,
    purple and pale blue. That is inference from three samples, not a spec.
    """

    def test_a_plain_name_has_no_colour(self):
        text, colours = ted.parse_name("Bullet Sponge")
        self.assertEqual(text, "Bullet Sponge")
        self.assertEqual(colours, [])

    def test_a_coloured_name_gives_up_both(self):
        text, colours = ted.parse_name("\x11c8b5f55ffI DIVE")
        self.assertEqual(text, "I DIVE")
        self.assertEqual(colours, [(0, "#8b5f55ff")])

    def test_the_grey_and_the_gold(self):
        self.assertEqual(ted.parse_name("\x11cccccccffJohn Helldiver"),
                         ("John Helldiver", [(0, "#ccccccff")]))
        self.assertEqual(ted.parse_name("\x11ccc9900ffI'm not gonna sugarcoat it"),
                         ("I'm not gonna sugarcoat it", [(0, "#cc9900ff")]))

    def test_a_colour_can_change_part_way_through(self):
        # the markup allows it even though HDG only ever opens with one
        text, colours = ted.parse_name("\x11cff0000ffRed\x11c00ff00ffGreen")
        self.assertEqual(text, "RedGreen")
        self.assertEqual(colours, [(0, "#ff0000ff"), (3, "#00ff00ff")])

    def test_an_escape_that_is_not_a_colour_is_left_as_text(self):
        # better a literal than a guess: an unknown tag is not silently eaten
        text, colours = ted.parse_name("\x11zsomething")
        self.assertEqual(text, "zsomething")
        self.assertEqual(colours, [])

    def test_a_truncated_colour_tag_is_left_as_text(self):
        text, colours = ted.parse_name("\x11c8b5")
        self.assertEqual(text, "c8b5")
        self.assertEqual(colours, [])

    def test_a_name_that_merely_begins_with_c_keeps_it(self):
        self.assertEqual(ted.parse_name("cabbage"), ("cabbage", []))

    def test_the_reader_carries_the_colour_through(self):
        plain = payload(ted.PLAYER_TABLE_OFFSET) + record("\x11c8b5f55ffI DIVE", "I DRIVE")
        player = ted.read_players(bytes(plain))[0]
        self.assertEqual(player["name"], "I DIVE")
        self.assertEqual(player["name_colour"], "#8b5f55ff")

    def test_an_uncoloured_player_has_no_colour_to_carry(self):
        plain = payload(ted.PLAYER_TABLE_OFFSET) + record("Mothdiver", "MOTH")
        self.assertIsNone(ted.read_players(bytes(plain))[0]["name_colour"])

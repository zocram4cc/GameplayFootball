"""Tests for the .ted tactical-export reader.

The format was reversed rather than documented, so these lock down what was
actually established: a plaintext header, a payload under a repeating 32-byte
key that every file chooses for itself, and the key recoverable from the
payload's own zero padding.

Run: python3 -m unittest test_ted -v
"""

import unittest

import ted


def build_ted(team=b"/test/", manager=b"GAFFER", players=(b"ALPHA", b"BETA"),
              order=None, key=None):
    """A synthetic .ted, assembled the way the real ones are laid out."""
    key = key or bytes(range(1, ted.KEY_LENGTH + 1))
    size = ted.PLAYER_TABLE_OFFSET + ted.PLAYER_RECORD_SIZE * (len(players) + 1)
    plain = bytearray(size)
    plain[ted.TEAM_NAME_OFFSET:ted.TEAM_NAME_OFFSET + len(team)] = team
    plain[ted.MANAGER_OFFSET:ted.MANAGER_OFFSET + len(manager)] = manager
    order = order or list(range(1, ted.SQUAD_SIZE + 1))
    plain[ted.SQUAD_ORDER_OFFSET:ted.SQUAD_ORDER_OFFSET + len(order)] = bytes(order)
    for i, name in enumerate(players):
        at = ted.PLAYER_TABLE_OFFSET + i * ted.PLAYER_RECORD_SIZE + 60
        plain[at:at + len(name)] = name
    payload = bytes(b ^ key[i % ted.KEY_LENGTH] for i, b in enumerate(plain))
    header = bytearray(ted.HEADER_SIZE)
    header[0:2] = (24).to_bytes(2, "little")
    header[2:4] = (1).to_bytes(2, "little")
    return bytes(header) + payload, key


class RecoverKeyTest(unittest.TestCase):
    def test_recovers_the_key_from_zero_padding(self):
        blob, key = build_ted()
        self.assertEqual(ted.recover_key(blob[ted.HEADER_SIZE:]), key)

    def test_recovers_a_different_key_just_as_well(self):
        # Keys are per file, so nothing may be hardcoded.
        odd = bytes((i * 7 + 3) & 0xFF for i in range(ted.KEY_LENGTH))
        blob, key = build_ted(key=odd)
        self.assertEqual(ted.recover_key(blob[ted.HEADER_SIZE:]), odd)
        self.assertEqual(key, odd)


class DecryptTest(unittest.TestCase):
    def test_round_trips_to_mostly_padding(self):
        blob, _ = build_ted()
        plain, _ = ted.decrypt(blob)
        self.assertGreater(plain.count(0), len(plain) // 2)

    def test_rejects_something_far_too_short(self):
        with self.assertRaises(ValueError):
            ted.decrypt(b"\x00" * 8)

    def test_rejects_a_payload_that_does_not_decrypt(self):
        # Random noise has no dominant byte per position, so the recovered key
        # is meaningless and the result is not padding. Better to fail loudly
        # than to hand back plausible nonsense.
        noise = bytes((i * 131 + 17) & 0xFF for i in range(4000))
        with self.assertRaises(ValueError):
            ted.decrypt(b"\x00" * ted.HEADER_SIZE + noise)


class ReadExportTest(unittest.TestCase):
    def test_reads_the_team_and_manager(self):
        blob, _ = build_ted(team=b"/xyz/", manager=b"THE BOSS")
        plain, _ = ted.decrypt(blob)
        self.assertEqual(ted.read_string(plain, ted.TEAM_NAME_OFFSET), "/xyz/")
        self.assertEqual(ted.read_string(plain, ted.MANAGER_OFFSET), "THE BOSS")

    def test_reads_the_squad_order(self):
        wanted = [22, 14, 11, 13, 21, 15, 7, 19, 9, 10, 8]
        blob, _ = build_ted(order=wanted + list(range(23, 23 + ted.SQUAD_SIZE - 11)))
        plain, _ = ted.decrypt(blob)
        self.assertEqual(ted.read_squad_order(plain)[:11], wanted)

    def test_reads_player_names_in_record_order(self):
        blob, _ = build_ted(players=(b"FIRST GUY", b"SECOND GUY", b"THIRD GUY"))
        plain, _ = ted.decrypt(blob)
        self.assertEqual(ted.read_players(plain)[:3], ["FIRST GUY", "SECOND GUY", "THIRD GUY"])

    def test_a_record_of_only_padding_ends_the_roster(self):
        blob, _ = build_ted(players=(b"ONLY ONE",))
        plain, _ = ted.decrypt(blob)
        self.assertEqual(ted.read_players(plain), ["ONLY ONE"])


if __name__ == "__main__":
    unittest.main()

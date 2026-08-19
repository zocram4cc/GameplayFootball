"""Tests for assigning a 4cc pack's models to a squad.

A pack does not name its files after players. It names them after shirt numbers,
because that is what PES's slots are keyed by:

    Faces/XXX02 - Lobby doko          the face and hair for whoever wears 2
    Boots/k2411 - Helldiver           the body in the boots slot for shirt 11

Nothing in the pack says which engine player wears which shirt. That mapping is
in the team's tactical export and nowhere else - `ted.read_squad` returns
{'id': 80311, 'number': 11} - which is why a squad model import requires the
.ted rather than merely benefiting from one. Importing /hdg/ without it bound
models to the three players whose shirt number happened to have its own boots
folder and left the other twenty on the stock body.

A pack also shares bodies. /hdg/ ships three for twenty-three players, and which
one a player takes follows from what his own face folder contains: PES draws the
boots model and the face model together, so a player whose folder carries a face
mesh takes the headless body and a player with only hair takes the complete one.
That is why the pack ships "Helldiver" and "Helldiver Headless" as a pair.

Run: python3 -m unittest test_squad_models -v
"""

import unittest

import squad_models


SQUAD = [{"id": 80301, "number": 1}, {"id": 80302, "number": 2},
         {"id": 80305, "number": 5}, {"id": 80311, "number": 11},
         {"id": 80321, "number": 21}]

# What the /hdg/ pack ships, in the shape the reader returns.
FACES = {1: {"dir": "XXX01 - Bullet Sponge", "face": False, "hair": True},
         2: {"dir": "XXX02 - Lobby doko", "face": True, "hair": False},
         5: {"dir": "XXX05 - SEAF-chan", "face": True, "hair": False},
         11: {"dir": "XXX11 - John Helldiver", "face": False, "hair": True}}
BOOTS = {2: "k2402 - Helldiver Headless", 11: "k2411 - Helldiver",
         21: "k2421 - Alexus"}


class ReadingAShirtNumberOffAFilename(unittest.TestCase):
    def test_a_face_folder_carries_it_after_the_xxx(self):
        self.assertEqual(squad_models.shirt_of("XXX02 - Lobby doko"), 2)
        self.assertEqual(squad_models.shirt_of("XXX23 - Mechwarrior"), 23)

    def test_a_boots_folder_carries_it_in_the_last_two_digits(self):
        # k<team block><shirt>: /hdg/ was allocated 24xx, so k2411 is shirt 11
        self.assertEqual(squad_models.shirt_of("k2411 - Helldiver"), 11)
        self.assertEqual(squad_models.shirt_of("k2402 - Helldiver Headless"), 2)

    def test_a_folder_naming_no_shirt_has_none(self):
        self.assertIsNone(squad_models.shirt_of("Common"))
        self.assertIsNone(squad_models.shirt_of("Kit Textures"))


class AssigningBodies(unittest.TestCase):
    def test_a_player_with_his_own_boots_folder_gets_it(self):
        got = squad_models.assign(SQUAD, FACES, BOOTS)
        self.assertEqual(got[80321]["body"], "k2421 - Alexus")

    def test_a_player_with_a_face_mesh_takes_the_headless_body(self):
        # PES draws boots and face together, so his own head would be a second one.
        # Shirt 5 ships a face and has no boots folder of his own, so this is the
        # shared rule rather than a folder lookup.
        got = squad_models.assign(SQUAD, FACES, BOOTS)
        self.assertEqual(got[80305]["body"], "k2402 - Helldiver Headless")

    def test_a_player_with_only_hair_takes_the_complete_body(self):
        got = squad_models.assign(SQUAD, FACES, BOOTS)
        self.assertEqual(got[80301]["body"], "k2411 - Helldiver")

    def test_every_player_in_the_squad_is_assigned_something(self):
        got = squad_models.assign(SQUAD, FACES, BOOTS)
        self.assertEqual(sorted(got), [80301, 80302, 80305, 80311, 80321])
        for entry in got.values():
            self.assertTrue(entry["body"])

    def test_a_players_own_face_and_hair_come_with_him(self):
        got = squad_models.assign(SQUAD, FACES, BOOTS)
        self.assertEqual(got[80301]["face_dir"], "XXX01 - Bullet Sponge")
        self.assertIsNone(got[80321]["face_dir"])   # Alexus ships no face folder

    def test_the_reason_is_recorded_so_an_import_can_be_read_back(self):
        got = squad_models.assign(SQUAD, FACES, BOOTS)
        self.assertIn("own", got[80321]["why"])
        self.assertIn("headless", got[80305]["why"])


class WhenThePackSharesNothing(unittest.TestCase):
    """A pack shipping a body per player needs no sharing rule at all."""

    def test_one_boots_folder_each_is_used_as_given(self):
        squad = [{"id": 1, "number": 1}, {"id": 2, "number": 2}]
        boots = {1: "k1851 - one", 2: "k1852 - two"}
        got = squad_models.assign(squad, {}, boots)
        self.assertEqual(got[1]["body"], "k1851 - one")
        self.assertEqual(got[2]["body"], "k1852 - two")

    def test_a_pack_with_no_bodies_leaves_the_body_unset(self):
        # nothing to draw over the base: the caller falls back to the stock body
        got = squad_models.assign([{"id": 1, "number": 1}], {}, {})
        self.assertIsNone(got[1]["body"])


class TheTedIsRequired(unittest.TestCase):
    def test_no_squad_is_no_assignment(self):
        # and the caller must say so rather than silently importing three of them
        with self.assertRaises(ValueError):
            squad_models.assign([], FACES, BOOTS)


if __name__ == "__main__":
    unittest.main()

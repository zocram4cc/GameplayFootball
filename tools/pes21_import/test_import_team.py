"""Tests for what an imported export is allowed to become.

A 4cc pack's per-player export is not always a body. The packs override PES's
boots, glove and face slots, and PES draws those *in addition to* its own body
wearing the team's kit texture - it does not replace it. 2HUG's kit textures are
DXT1, which carries no alpha, so the invisible-kit trick is not in play there:
the character is the kit on the standard body and the boots export is a prop.

Bound as a whole body instead, a prop is all you get. That is what put a fan of
wing blades and a headless torso on the pitch: lcg_2702 is `boots` plus `wings`,
2hug_1851 is a single `medical_c` mesh, and neither has a player in it.

So an export that does not clothe the rig is not bound at all, and the player
keeps the stock body the kit is swapped into (Team::FetchKit). body_coverage
decides it, by asking whether there is geometry near each of the rig's joints.

Run: python3 -m unittest test_import_team -v
"""

import unittest

import import_team


class WhichExportsMayBeBoundAsABody(unittest.TestCase):
    def test_a_whole_body_is_bound(self):
        self.assertTrue(import_team.may_bind_as_body("whole"))

    def test_a_needs_base_export_is_bound_anyway(self):
        # "needs base" is not a completeness signal: body_coverage counts bare
        # finger joints, and its 32-vertex head threshold rejects the engine's
        # own fullbody.ase at 29-31. Refusing on it left real characters with
        # real geometry unused - hdg_2421 imported and then went unbound.
        self.assertTrue(import_team.may_bind_as_body("needs base"))

    def test_an_export_carrying_scenery_is_not(self):
        # lcg_2718's backdrop reaches 362 m and frames the player down to a dot.
        # This is the one verdict that means what it says: while a backdrop is in
        # the file nothing about the model can be judged.
        self.assertFalse(import_team.may_bind_as_body("carries scenery"))

    def test_an_unknown_verdict_is_bound_rather_than_dropped(self):
        # the failure that matters here is a player with no model, not a player
        # with an odd one
        self.assertTrue(import_team.may_bind_as_body(""))
        self.assertTrue(import_team.may_bind_as_body("something new"))

    def test_a_composited_export_is_bound_whatever_its_own_coverage(self):
        """--base puts the stock body underneath, so the result does clothe the rig."""
        self.assertTrue(import_team.may_bind_as_body("needs base", composited=True))
        self.assertTrue(import_team.may_bind_as_body("carries scenery", composited=True))


if __name__ == "__main__":
    unittest.main()


class PortraitBindings(unittest.TestCase):
    """Binding a pack's portraits to the players who already have models.

    A 4cc export names its portraits by team slot - "XXX07 - Rodya.png" - because the
    team's real slot number is not known until it is assigned. The models are keyed by
    the same slot ("lcg_2707"), and the model bindings already resolve a slot to a
    database ID, so the portraits ride along on that rather than guessing.
    """

    def test_a_slot_is_read_out_of_a_portrait_name(self):
        self.assertEqual(import_team.portrait_slot("XXX07 - Rodya.png"), 7)
        self.assertEqual(import_team.portrait_slot("XXX23 - te.png"), 23)
        self.assertEqual(import_team.portrait_slot("player_78301.png"), 1)

    def test_a_name_with_no_slot_in_it_is_refused(self):
        self.assertIsNone(import_team.portrait_slot("logo.png"))
        self.assertIsNone(import_team.portrait_slot("XXX - nameless.png"))

    def test_an_export_number_is_read_out_of_a_model_directory(self):
        self.assertEqual(import_team.model_number("media/players/custom/lcg_2707"), 2707)
        self.assertEqual(import_team.model_number("media/players/custom/2hug_1851"), 1851)
        self.assertIsNone(import_team.model_number("media/players/custom/plain"))

    def test_a_pack_numbering_from_its_own_base_still_lines_up(self):
        # 2hug counts its players from 51 and its portraits from 01
        models = {"393": "media/players/custom/2hug_1851",
                  "153": "media/players/custom/2hug_1853"}
        bound = import_team.bind_portraits(models, ["player_78301.png", "player_78303.png"],
                                          "2hug")
        self.assertEqual(bound, {"393": "imports/2hug/portraits/player_78301.png",
                                 "153": "imports/2hug/portraits/player_78303.png"})

    def test_portraits_bind_to_the_ids_their_models_already_hold(self):
        models = {"450": "media/players/custom/lcg_2701",
                  "455": "media/players/custom/lcg_2706"}
        portraits = ["XXX01 - Clapped.png", "XXX06 - Faust.png", "XXX09 - Monzo.png"]
        bound = import_team.bind_portraits(models, portraits, "lcg")
        self.assertEqual(bound, {"450": "imports/lcg/portraits/XXX01 - Clapped.png",
                                "455": "imports/lcg/portraits/XXX06 - Faust.png"})

    def test_a_portrait_for_a_player_with_no_model_is_left_alone(self):
        # binding it would need an ID nothing in the pack supplies
        bound = import_team.bind_portraits({}, ["XXX01 - Clapped.png"], "lcg")
        self.assertEqual(bound, {})

    def test_a_name_both_sides_carry_beats_the_numbering(self):
        # LCG's portraits run one ahead of its boots from slot 8 on: XXX09 is Dante
        # and the model numbered 2708 is Dante. The numbering is not evidence; the
        # name they both carry is.
        models = {"457": "media/players/custom/lcg_2708",
                  "458": "media/players/custom/lcg_2709"}
        names = {"457": "Dante", "458": "Monzo"}
        bound = import_team.bind_portraits(
            models, ["XXX08 - Papa Don.png", "XXX09 - Dante.png", "XXX10 - Monzo.png"],
            "lcg", names)
        self.assertEqual(bound, {"457": "imports/lcg/portraits/XXX09 - Dante.png",
                                 "458": "imports/lcg/portraits/XXX10 - Monzo.png"})

    def test_an_unnamed_portrait_does_not_take_a_player_the_named_one_wants(self):
        # Papa Don has no model and sits at slot 8, which is Dante's model number;
        # binding by slot first would consume Dante and drop his own portrait.
        models = {"457": "media/players/custom/lcg_2708"}
        bound = import_team.bind_portraits(
            models, ["XXX08 - Papa Don.png", "XXX09 - Dante.png"], "lcg", {"457": "Dante"})
        self.assertEqual(bound, {"457": "imports/lcg/portraits/XXX09 - Dante.png"})

    def test_a_pack_that_only_numbers_its_portraits_binds_by_slot(self):
        # 2hug's are player_78301.png upwards and name nobody
        models = {"393": "media/players/custom/2hug_1851"}
        bound = import_team.bind_portraits(models, ["player_78301.png"], "2hug",
                                          {"393": "1CC Killer"})
        self.assertEqual(bound, {"393": "imports/2hug/portraits/player_78301.png"})

    def test_a_pack_that_names_most_players_does_not_guess_the_rest(self):
        # its numbering has already been shown wrong where a name failed to match, so
        # a leftover portrait is left unbound rather than put on somebody's face
        models = {"457": "media/players/custom/lcg_2708",
                  "458": "media/players/custom/lcg_2709"}
        bound = import_team.bind_portraits(models, ["XXX08 - Papa Don.png", "XXX09 - Dante.png"],
                                          "lcg", {"457": "Dante", "458": "Nobody"})
        self.assertEqual(bound, {"457": "imports/lcg/portraits/XXX09 - Dante.png"})

    def test_one_portrait_is_not_handed_to_two_players(self):
        models = {"1": "media/players/custom/lcg_2701", "2": "media/players/custom/lcg_2702"}
        names = {"1": "Clapped", "2": "Clapped"}
        bound = import_team.bind_portraits(models, ["XXX01 - Clapped.png"], "lcg", names)
        self.assertEqual(len(set(bound.values())), len(bound))

    def test_portraits_of_another_teams_prefix_are_not_claimed(self):
        models = {"450": "media/players/custom/ink_2401"}
        self.assertEqual(import_team.bind_portraits(models, ["XXX01 - x.png"], "lcg"), {})


class BasePartsToDrop(unittest.TestCase):
    """Which of the stock body's parts a composited import replaces.

    A face-slot import brings its own head, so the stock face, eyes and scalp have to
    go or they sit inside it and fight it for depth. A hair-only export brings no head
    at all - the 4cc packs ship plenty (fcl_hair.fmdl) - and dropping the stock face
    for one of those leaves a player with no head, which is exactly what shipped.
    """

    def test_a_face_import_replaces_the_stock_head(self):
        drop = import_team.base_parts_to_drop(["face_high", "hair_high"])
        self.assertIn("face", drop)
        self.assertIn("eyes", drop)

    def test_a_hair_only_import_keeps_the_stock_head(self):
        drop = import_team.base_parts_to_drop(["fcl_hair"])
        self.assertNotIn("face", drop)
        self.assertNotIn("eyes", drop)

    def test_an_accessory_import_keeps_everything(self):
        drop = import_team.base_parts_to_drop(["stim_bsm", "pouch"])
        self.assertNotIn("face", drop)
        self.assertNotIn("eyes", drop)

    def test_a_head_named_mesh_counts_as_a_face(self):
        self.assertIn("face", import_team.base_parts_to_drop(["head"]))
        self.assertIn("face", import_team.base_parts_to_drop(["u0XXXp0_head_bsm"]))

    def test_nothing_at_all_drops_nothing(self):
        self.assertEqual(import_team.base_parts_to_drop([]), set())

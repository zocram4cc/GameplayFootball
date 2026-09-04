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

import os
import tempfile
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


class WhereAPackKeepsItsPortraits(unittest.TestCase):
    """A pack's portraits live in `Portraits/` or beside each face - or both.

    LCG, SMBG, HDG and DBG keep `portrait.dds` inside `Faces/<XXXnn - Name>/`, which
    the import never read: four squads played with no faces on the game plan. The
    written name keeps the token that carries the shirt, which is what
    relink_portraits binds by.
    """

    def setUp(self):
        self.pack = tempfile.mkdtemp()
        self.addCleanup(lambda: __import__("shutil").rmtree(self.pack))

    def _file(self, *parts):
        path = os.path.join(self.pack, *parts)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "wb").close()
        return path

    def test_faces_folders_supply_a_portrait_named_after_the_folder(self):
        source = self._file("Faces", "XXX07 - Shiddy", "portrait.dds")
        self._file("Faces", "XXX07 - Shiddy", "face_high.fmdl")
        self.assertEqual(import_team.portrait_sources(self.pack), [("XXX07 - Shiddy.png", source)])

    def test_the_portraits_folder_keeps_its_own_names(self):
        source = self._file("Portraits", "player_78301.dds")
        self.assertEqual(import_team.portrait_sources(self.pack), [("player_78301.png", source)])

    def test_a_shirt_named_in_both_places_is_taken_once(self):
        kept = self._file("Portraits", "player_XXX21.dds")
        self._file("Faces", "XXX21 - Someone", "portrait.dds")
        other = self._file("Faces", "XXX22 - EAT", "portrait.dds")
        self.assertEqual(import_team.portrait_sources(self.pack),
                         [("player_XXX21.png", kept), ("XXX22 - EAT.png", other)])

    def test_a_face_folder_without_a_shirt_or_a_portrait_is_skipped(self):
        self._file("Faces", "Common", "portrait.dds")
        self._file("Faces", "XXX05 - Nobody", "face_high.fmdl")
        self.assertEqual(import_team.portrait_sources(self.pack), [])

    def test_every_written_name_resolves_to_its_shirt(self):
        self._file("Faces", "XXX04 - Wario Land 4", "portrait.dds")
        self._file("Portraits", "player_78316.dds")
        shirts = [import_team.portrait_shirt(name) for name, _ in import_team.portrait_sources(self.pack)]
        self.assertEqual(sorted(shirts), [4, 16])


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

"""Tests for how imported meshes are assigned materials when composited onto a
base body (--base).

The /a/ squad shipped with the team kit painted over every player's face. The
cause was here: the imported head was written with *MATERIAL_REF 0, and in a
--base composite material 0 belongs to the base body's shirt - it is
kit_template.png, the slot HumanoidBase::SetKit swaps the team kit into. So the
head was, as far as the engine could tell, a piece of jersey.

Run: python3 -m unittest test_material_plan -v
"""

import unittest

import fmdl_to_fullbody


class MaterialPlanTest(unittest.TestCase):
    def test_standalone_model_numbers_groups_from_zero(self):
        """With no base there is nothing to collide with."""
        appended, refs = fmdl_to_fullbody.base_material_plan(
            0, ["face.png", "hair.png"], "fallback.png")
        self.assertEqual(refs, [0, 1])
        self.assertEqual(appended, ["face.png", "hair.png"])

    def test_composited_model_starts_after_the_base_materials(self):
        """The whole bug: refs must clear the base's own materials."""
        appended, refs = fmdl_to_fullbody.base_material_plan(
            6, ["ateam_70202_head.png"], "fallback.png")
        self.assertEqual(refs, [6])
        self.assertNotIn(0, refs, "material 0 is the base body's kit slot")
        self.assertEqual(appended, ["ateam_70202_head.png"])

    def test_every_imported_group_gets_its_own_material(self):
        """One appended material per group, not one for the whole import -
        otherwise groups share a texture and the extra ones point at nothing."""
        appended, refs = fmdl_to_fullbody.base_material_plan(
            6, ["head.png", "hair.png", "eyes.png"], "fallback.png")
        self.assertEqual(refs, [6, 7, 8])
        self.assertEqual(appended, ["head.png", "hair.png", "eyes.png"])
        self.assertEqual(len(appended), len(refs))

    def test_group_without_a_texture_falls_back(self):
        """A group whose material carried no base texture still needs one."""
        appended, refs = fmdl_to_fullbody.base_material_plan(
            6, [None], "fallback.png")
        self.assertEqual(appended, ["fallback.png"])
        self.assertEqual(refs, [6])

    def test_import_with_no_groups_still_yields_one_material(self):
        appended, refs = fmdl_to_fullbody.base_material_plan(6, [], "fallback.png")
        self.assertEqual(appended, ["fallback.png"])
        self.assertEqual(refs, [6])


class UnresolvedGroupTextureTest(unittest.TestCase):
    """Which texture a mesh gets when the pack does not ship the one its
    material names.

    4cc models routinely point their kit mesh at the shared PES kit map
    (u0XXXp0), which no pack contains. On a whole-character import the kit is
    dropped in beside the model and used directly. On a face-slot import
    composited over a base body there is no such file, and the mesh is a piece
    of kit, so it belongs in the engine's kit slot - kit_template.png, which
    Team::FetchKit swaps the team's own kit into per match.
    """

    def test_composite_sends_unresolved_meshes_to_the_kit_slot(self):
        self.assertEqual(
            fmdl_to_fullbody.unresolved_group_texture("base.ase", "pack/body.png"),
            fmdl_to_fullbody.KIT_SLOT_TEXTURE)

    def test_standalone_import_keeps_its_own_kit_drop_in(self):
        self.assertEqual(
            fmdl_to_fullbody.unresolved_group_texture(None, "pack/body.png"),
            "pack/body.png")


if __name__ == "__main__":
    unittest.main()

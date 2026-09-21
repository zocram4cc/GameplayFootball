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

    def test_kit_switcher_mesh_goes_to_the_kit_slot(self):
        """A mesh UV-mapped onto PES's kit map wears the kit, not the body.

        The 4cc "kit switcher": a custom mesh whose UVs are laid out on the
        shared kit texture, so the team's own uniform paints it and the model
        changes with the kit preset. PES names that slot `dummy_kit` and swaps
        the team's uniform in at run time; the engine does the same through
        kit_template.png (HumanoidBase::SetKit).

        Bowser (smbg k2593) is the whole of the case: his shins and shorts are
        that mesh and nothing else, his pack ships no dummy_kit.dds, and the
        character's own atlas has no leg art at those UVs - so handing him the
        fallback painted his legs with a blank corner of the body map and he
        walked out with nothing below the belt.
        """
        for base in (None, "base.ase"):
            self.assertEqual(
                fmdl_to_fullbody.resolved_group_texture(
                    "dummy_kit", None, base, "pack/kit.png", "pack/body.png"),
                fmdl_to_fullbody.KIT_SLOT_TEXTURE)

    def test_a_missing_character_texture_still_falls_back(self):
        """Only the kit slot is the kit: the 05-09 face-painting stays fixed."""
        self.assertEqual(
            fmdl_to_fullbody.unresolved_group_texture(
                None, "pack/kit.png", "face_bsm_alp", "pack/body.png"),
            "pack/body.png")


class KitSlotGroupTest(unittest.TestCase):
    """The kit slot beats any copy of the kit the pack happens to ship.

    ink drops its own `Common/dummy_kit.dds` beside the models so the mesh
    previews dressed in the editor - and that file is its first kit: measured
    against `Kit Textures/u0XXXp1.dds` the mean absolute difference is 0.1 of
    255, DXT round-trip noise and nothing else (u0XXXp2 differs by 130).
    Baking it would freeze all 103 such meshes on kit 1, so the second and
    third strips and the keeper's would never reach them. The slot costs
    nothing on kit 1 - same image - and is what PES does.
    """

    def test_a_shipped_kit_copy_does_not_beat_the_slot(self):
        self.assertEqual(
            fmdl_to_fullbody.resolved_group_texture(
                "dummy_kit", "pack/smbg_2593_dummy_kit.png", None,
                "pack/kit.png", "pack/body.png"),
            fmdl_to_fullbody.KIT_SLOT_TEXTURE)

    def test_an_ordinary_group_still_wears_its_own_texture(self):
        self.assertEqual(
            fmdl_to_fullbody.resolved_group_texture(
                "body", "pack/body.png", None, "pack/kit.png", "pack/body.png"),
            "pack/body.png")

    def test_an_ordinary_group_without_a_file_falls_back(self):
        self.assertEqual(
            fmdl_to_fullbody.resolved_group_texture(
                "face_bsm_alp", None, None, "pack/kit.png", "pack/body.png"),
            "pack/body.png")

    def test_only_the_kit_placeholder_is_the_kit_slot(self):
        self.assertTrue(fmdl_to_fullbody.is_kit_slot_texture("dummy_kit"))
        self.assertTrue(fmdl_to_fullbody.is_kit_slot_texture("DUMMY_KIT"))
        # the normal/specular stand-ins share the prefix and are not kit
        self.assertFalse(fmdl_to_fullbody.is_kit_slot_texture("dummy_nrm"))
        self.assertFalse(fmdl_to_fullbody.is_kit_slot_texture("dummy_srm"))
        self.assertFalse(fmdl_to_fullbody.is_kit_slot_texture("body"))
        self.assertFalse(fmdl_to_fullbody.is_kit_slot_texture(None))


class NonRenderPassTest(unittest.TestCase):
    """PES ships passes this engine does not render, as extra copies of the
    body sitting a hair outside the real one.

    The antiblur copy was already skipped. The outline shell was not, so /a/
    players carried a second, slightly inflated body - and once its texture was
    routed to the kit slot it became a team-coloured shell z-fighting the real
    shirt, which is the torn patchwork on their shoulders. On one player the
    outline was the only mesh that survived the triangle budget, leaving him
    with no face at all.
    """

    def test_antiblur_material_is_skipped(self):
        self.assertTrue(fmdl_to_fullbody.is_non_render_pass("kit_antiblur", "kit"))

    def test_outline_material_is_skipped(self):
        self.assertTrue(fmdl_to_fullbody.is_non_render_pass("body_outline", "body"))

    def test_outline_base_texture_is_skipped(self):
        """The shell is named by its texture, not always by its material."""
        self.assertTrue(fmdl_to_fullbody.is_non_render_pass("", "outline"))

    def test_ordinary_meshes_are_kept(self):
        self.assertFalse(fmdl_to_fullbody.is_non_render_pass("face_mat", "face"))
        self.assertFalse(fmdl_to_fullbody.is_non_render_pass("hair", "hair_col"))
        self.assertFalse(fmdl_to_fullbody.is_non_render_pass("", ""))

    def test_does_not_match_a_word_that_merely_contains_it(self):
        """"outlined_crest" is artwork, not a shell pass."""
        self.assertFalse(fmdl_to_fullbody.is_non_render_pass("", "outlined_crest"))


if __name__ == "__main__":
    unittest.main()

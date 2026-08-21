"""Tests for carrying PES's unlit materials through the import.

The 4cc anime models are largely flat-shaded on purpose - the look people call the
"pony shader" - and PES says so in the material itself. Fox Engine names its
shaders after the BRDF they use, and the unlit one is `Constant`:

    fox3DFW_ConstantSRGB_NDR_Solid    47 meshes   unlit, constant colour
    fox3DDF_Blin_Fuzzblock            47 meshes   Blinn
    fox3DDF_GGX                       32 meshes   GGX
    fox3DDF_Blin_Translucent           2 meshes   Blinn, translucent

surveyed over the /hdg/ and 2HUG boots exports. So the importer does not have to
guess which meshes want flat shading; it has to stop throwing the answer away.

The engine already has the mechanism. aseloader.cpp reads MATERIAL_SELFILLUM into
materialparams.z, simple.frag writes it to the aux buffer's alpha, and the lighting
pass takes that as the self-illumination factor - so an unlit mesh is one whose
self-illumination is 1.

Run: python3 -m unittest test_shadeless -v
"""

import unittest

import fmdl_to_fullbody


class _Material:
    def __init__(self, shader, technique=None):
        self.shader = shader
        self.technique = technique if technique is not None else shader
        self.textures = []


class _Mesh:
    def __init__(self, shader):
        self.materialInstance = _Material(shader)


class WhichMaterialsAreUnlit(unittest.TestCase):
    def test_the_constant_shader_is_unlit(self):
        self.assertTrue(fmdl_to_fullbody.is_shadeless(_Mesh("fox3dfw_constant_srgb_ndr_solid")))

    def test_it_is_recognised_by_technique_too(self):
        mesh = _Mesh("something_unfamiliar")
        mesh.materialInstance.technique = "fox3DFW_ConstantSRGB_NDR_Solid"
        self.assertTrue(fmdl_to_fullbody.is_shadeless(mesh))

    def test_case_does_not_matter(self):
        self.assertTrue(fmdl_to_fullbody.is_shadeless(_Mesh("fox3DFW_CONSTANTSRGB_NDR_SOLID")))

    def test_a_lit_brdf_is_not(self):
        for shader in ("fox3ddf_blin_fuzzblock", "fox3ddf_ggx", "fox3ddf_translucent"):
            self.assertFalse(fmdl_to_fullbody.is_shadeless(_Mesh(shader)), shader)

    def test_a_mesh_with_no_material_is_not(self):
        class Bare:
            pass
        self.assertFalse(fmdl_to_fullbody.is_shadeless(Bare()))

    def test_a_material_naming_no_shader_is_not(self):
        mesh = _Mesh("")
        mesh.materialInstance.technique = ""
        self.assertFalse(fmdl_to_fullbody.is_shadeless(mesh))


class WhatTheMaterialBlockSays(unittest.TestCase):
    def test_an_unlit_mesh_asks_for_full_self_illumination(self):
        block = fmdl_to_fullbody.material_block("body.png", shadeless=True)
        self.assertIn("*MATERIAL_SELFILLUM 1.0", block)

    def test_a_lit_mesh_asks_for_none(self):
        block = fmdl_to_fullbody.material_block("body.png", shadeless=False)
        self.assertIn("*MATERIAL_SELFILLUM 0.0", block)

    def test_the_texture_still_lands_in_the_block(self):
        self.assertIn("body.png", fmdl_to_fullbody.material_block("body.png", shadeless=True))

    def test_nothing_else_about_the_material_changes(self):
        lit = fmdl_to_fullbody.material_block("t.png", shadeless=False)
        unlit = fmdl_to_fullbody.material_block("t.png", shadeless=True)
        self.assertEqual(lit.replace("SELFILLUM 0.0", "SELFILLUM 1.0"), unlit)


if __name__ == "__main__":
    unittest.main()

"""Tests for carrying PES's UV-animated materials through the import.

k2017's LED boots are not animated by the engine's animation system at all:
the FMDL material is a Fox `uvstep`/`uvscroll` shader whose parameters say how
the UVs move, plus a `Timing_Tex_LIN` map whose alpha decides visibility
frame by frame. Measured on the three shipped packs: R and A in timing.dds are
binary, and the tile index is provably 0 (tilesUsed=1, R in {0,1}), so what
actually animates is that alpha, sampled at a swept U - and timing V is
per-vertex (the eyes swap states between rows).

The import carries all of it in three places, no more:

  *MATERIAL_UVANIM  one parameter line per material (family + floats)
  *MAP_TIMING       the timing texture, resolved like the diffuse map
  *MESH_TVERT      the third float, which aseloader.cpp:377 already reads
                   un-negated into texcoord.z

Run: python3 -m unittest test_uvanim -v
"""

import unittest

import fmdl_to_fullbody


class _Texture:
    def __init__(self, filename):
        self.filename = filename


class _Material:
    def __init__(self, shader, technique=None, textures=(), parameters=()):
        self.shader = shader
        self.technique = technique if technique is not None else shader
        self.name = shader
        self.textures = [(role, _Texture(fn)) for role, fn in textures]
        self.parameters = [(name, tuple(values)) for name, values in parameters]


class _Mesh:
    def __init__(self, material):
        self.materialInstance = material


def _step_mesh():
    # The exact structure FmdlFile.py gives back for k2017 boots.fmdl,
    # parameter tuples included.
    material = _Material(
        "fox3dfw_constant_srgb_ndr_uvstep",
        technique="fox3DFW_ConstantSRGB_NDR_UVStep",
        textures=[("Base_Tex_SRGB", "ball.dds"),
                  ("Timing_Tex_LIN", "timing.dds")],
        parameters=[
            ("Tile_Count_U", (1.0, 0.0, 0.0, 0.0)),
            ("Tile_Count_V", (1.0, 0.0, 0.0, 0.0)),
            ("Tiles_Used", (1.0, 0.0, 0.0, 0.0)),
            ("Scale_UVs_To_Tiles", (1.0, 0.0, 0.0, 0.0)),
            ("Seconds_Per_Animation_Cycle", (1.0, 0.0, 0.0, 0.0)),
            ("Use_Timing_Texture", (1.0, 0.0, 0.0, 0.0)),
            ("Seconds_Per_Timing_U_Cycle", (0.65, 0.0, 0.0, 0.0)),
            ("Seconds_Per_Timing_V_Cycle", (0.0, 0.0, 0.0, 0.0)),
        ])
    return _Mesh(material)


class WhichMaterialsAnimate(unittest.TestCase):
    def test_uvstep_reads_every_param_and_the_timing_texture(self):
        p = fmdl_to_fullbody.uvanim_params(_step_mesh())
        # family, values in the order the ASE line carries them, and the
        # timing map's source name.
        self.assertEqual(p["family"], "uvstep")
        self.assertEqual(p["values"],
                         [1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.65, 0.0])
        self.assertEqual(p["timing"], "timing.dds")

    def test_uvscroll_reads_speed_and_offset(self):
        # k2016's aura mesh: the uvscroll family's real parameter names.
        mesh = _Mesh(_Material(
            "fox3dfw_constant_srgb_ndr_uvscroll",
            parameters=[
                ("UV0_Speed_U", (0.0, 0.0, 0.0, 0.0)),
                ("UV0_Speed_V", (0.0003, 0.0, 0.0, 0.0)),
                ("Offset", (0.5, 0.0, 0.0, 0.0)),
            ]))
        p = fmdl_to_fullbody.uvanim_params(mesh)
        self.assertEqual(p["family"], "uvscroll")
        self.assertEqual(p["values"], [0.0, 0.0003, 0.5])
        self.assertIsNone(p["timing"])

    def test_a_plain_material_does_not_animate(self):
        for shader in ("fox3DFW_ConstantSRGB_NDR_Solid",
                       "fox3DDF_GGX",
                       "fox3DDF_Blin_Fuzzblock"):
            self.assertIsNone(
                fmdl_to_fullbody.uvanim_params(_Mesh(_Material(shader))))

    def test_a_uvstep_material_is_still_shadeless(self):
        # The k2017 boots are an unlit BRDF that also animates - both facts
        # have to hold at once, so the two predicates stay independent.
        mesh = _step_mesh()
        self.assertTrue(fmdl_to_fullbody.is_shadeless(mesh))
        self.assertIsNotNone(fmdl_to_fullbody.uvanim_params(mesh))


class WhatTheAseSays(unittest.TestCase):
    def test_material_block_carries_the_uvanim_line_and_timing_map(self):
        p = fmdl_to_fullbody.uvanim_params(_step_mesh())
        p["timing"] = "textures/k2017_timing.dds.png"
        block = fmdl_to_fullbody.material_block("textures/k2017_ball.dds.png",
                                                shadeless=True, uvanim=p)
        self.assertIn(
            "*MATERIAL_UVANIM uvstep 1.000000 1.000000 1.000000 1.000000 "
            "1.000000 1.000000 0.650000 0.000000", block)
        self.assertIn("*MAP_TIMING {", block)
        self.assertIn('*BITMAP "textures/k2017_timing.dds.png"', block)
        # unlit still says unlit
        self.assertIn("*MATERIAL_SELFILLUM 1.0", block)

    def test_a_static_material_carries_no_uvanim(self):
        block = fmdl_to_fullbody.material_block("textures/body.dds.png")
        self.assertNotIn("UVANIM", block)
        self.assertNotIn("MAP_TIMING", block)


if __name__ == "__main__":
    unittest.main()

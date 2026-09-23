"""Tests for the kit-slot rule: a mesh textured for the team's kit must reach
the engine's kit slot, never the stock body and never baked art.

smbg_2577 (Mario) is the shape of the defect: his shirt, sleeves, shorts and
socks are textured dummy_kit - PES's kit slot, painted with the active kit at
run time - but the installed model carries them on the stock body
(fullbody_body, 7,212 verts) under pes_base_* materials, so the kit never
reaches them and the pack's own kit art is ignored. Bowser (smbg_2593) had the
identical defect and was repaired model-by-model; this pins the rule in the
importer so all 64 future teams get it without per-model surgery.

Run: python3 -m unittest test_kit_slot -v
"""

import os
import tempfile
import unittest

import fmdl_to_fullbody
from test_mesh_selection import Material, Vertex


def kit_mesh(name="dummy_kit"):
    """A mesh textured for the team's kit slot, like smbg's shirt."""
    mesh = type("Mesh", (), {})()
    mesh.vertices = [Vertex(0.0), Vertex(0.1), Vertex(0.2)]
    mesh.faces = [None]
    mesh.materialInstance = Material("some_shader", name)
    return mesh


class KitSlotReachesTheEngineSlot(unittest.TestCase):
    def test_a_kit_slot_mesh_is_not_a_swap_to_hide(self):
        # Whatever file a dummy_kit resolves to, the mesh is authored geometry
        # PES paints the kit onto - not a blank stand-in to drop.
        pack = tempfile.mkdtemp()
        self.addCleanup(__import__("shutil").rmtree, pack)
        mesh = kit_mesh()
        self.assertFalse(
            fmdl_to_fullbody.is_unresolved_swap_mesh(mesh, "dummy_kit",
                                                     source_dir=pack))

    def test_a_kit_slot_mesh_resolves_to_the_engine_slot(self):
        self.assertEqual(
            fmdl_to_fullbody.resolved_group_texture(
                "dummy_kit", resolved=None, base_ase=None,
                fallback_texture="fallback.png", own_texture=None),
            fmdl_to_fullbody.KIT_SLOT_TEXTURE)

    def test_a_kit_slot_mesh_resolves_to_the_engine_slot_even_when_the_pack_ships_art(self):
        # ink's Common/dummy_kit.dds is the editor preview of kit 1, not the
        # kit: baking it freezes the mesh on kit 1 forever.
        self.assertEqual(
            fmdl_to_fullbody.resolved_group_texture(
                "dummy_kit", resolved="ink/dummy_kit.png", base_ase=None,
                fallback_texture="fallback.png", own_texture=None),
            fmdl_to_fullbody.KIT_SLOT_TEXTURE)

    def test_a_blank_kit_shell_is_still_dropped(self):
        # WAHluigi k2588: a fully transparent dummy shell is the swap
        # stand-in the placeholder rule was written for. Placeholder and
        # kit-slot are different questions (hide-if-blank vs route-to-slot)
        # and both answer True for dummy_kit - the texels decide.
        self.assertTrue(fmdl_to_fullbody.is_placeholder_texture("dummy_kit"))


if __name__ == "__main__":
    unittest.main()

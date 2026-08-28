"""Tests for which of an fmdl's meshes make it into the imported model.

Two incidents, both shipped with healthy-looking inventories:

dbg_2014 rendered torn in half. Its single character is 154,799 faces over 11
meshes, and select_meshes discarded whole meshes to fit the 100,000-triangle
budget - the five 20,000-face meshes fit exactly, the legs did not. A budget
must never amputate a visible character: when the character is bigger than the
budget it ships whole, and any capacity concern belongs to the engine, not to
an importer cutting limbs off.

dbg_2009 rendered as a chimera of five characters. Its boots.fmdl carries every
form the player can take (goku, vegeta, broly, roshi, jackiechun...), and the
pack hides the unused ones the PES way: each form's meshes point at a
per-character kit-slot texture, and the forms not in play get a fully
transparent texture. Meshes whose resolved texture is such a hider must not be
imported.

Run: python3 -m unittest test_mesh_selection -v
"""

import os
import shutil
import tempfile
import unittest

from PIL import Image

import fmdl_to_fullbody


class Position:
    def __init__(self, x, y, z):
        self.x, self.y, self.z = x, y, z


class Vertex:
    def __init__(self, x):
        self.position = Position(x, 0.0, 0.0)


class Texture:
    def __init__(self, filename):
        self.filename = filename


class Material:
    def __init__(self, name, texture=None):
        self.name = name
        self.textures = [("Base_Tex_SRGB", Texture(texture + ".dds"))] if texture else []


def fake_mesh(faces, material="body", texture=None, seed=0.0):
    mesh = type("Mesh", (), {})()
    mesh.vertices = [Vertex(seed + i) for i in range(3)]
    mesh.faces = [None] * faces
    mesh.materialInstance = Material(material, texture)
    return mesh


class BudgetNeverAmputates(unittest.TestCase):
    def test_a_character_bigger_than_the_budget_ships_whole(self):
        """dbg_2014: every distinct mesh is part of the character; dropping
        any of them tears the body apart."""
        meshes = [fake_mesh(60000, seed=1.0), fake_mesh(60000, seed=2.0),
                  fake_mesh(40000, seed=3.0)]
        kept = fmdl_to_fullbody.select_meshes(meshes, 100000)
        self.assertEqual(len(kept), 3)

    def test_duplicate_copies_are_still_removed(self):
        """4cc fmdls ship every mesh twice; the copy is redundant, not fidelity."""
        meshes = [fake_mesh(100, seed=1.0), fake_mesh(100, seed=1.0)]
        self.assertEqual(len(fmdl_to_fullbody.select_meshes(meshes, 100000)), 1)

    def test_non_render_passes_are_still_removed(self):
        meshes = [fake_mesh(100, seed=1.0),
                  fake_mesh(100, material="body antiblur", seed=2.0)]
        self.assertEqual(len(fmdl_to_fullbody.select_meshes(meshes, 100000)), 1)


class KitHiddenMeshesAreNotImported(unittest.TestCase):
    def setUp(self):
        self.pack = tempfile.mkdtemp()
        self.player = os.path.join(self.pack, "Boots", "k2009 - Master Roshi")
        os.makedirs(self.player)

    def tearDown(self):
        shutil.rmtree(self.pack)

    def _write(self, name, alpha):
        Image.new("RGBA", (4, 4), (200, 100, 50, alpha)).save(
            os.path.join(self.player, name))

    def test_a_mesh_whose_texture_is_a_hider_is_dropped(self):
        """dbg_2009: jackiechun's kit-1 texture is fully transparent - PES
        does not draw that form and neither may the import."""
        self._write("roshi_u0XXXp1.png", 255)
        self._write("jackiechun_u0XXXp1.png", 0)
        meshes = [fake_mesh(100, texture="roshi_u0XXXp0", seed=1.0),
                  fake_mesh(100, texture="jackiechun_u0XXXp0", seed=2.0)]
        kept = fmdl_to_fullbody.select_meshes(meshes, 100000,
                                              source_dir=self.player)
        self.assertEqual([m.materialInstance.textures[0][1].filename
                          for m in kept], ["roshi_u0XXXp0.dds"])

    def test_a_mesh_whose_texture_is_not_shipped_is_kept(self):
        """No texture is no verdict: the kit mesh (u0XXXp0) and anything the
        pack forgot still render, as they always did."""
        meshes = [fake_mesh(100, texture="u0XXXp0", seed=1.0)]
        kept = fmdl_to_fullbody.select_meshes(meshes, 100000,
                                              source_dir=self.player)
        self.assertEqual(len(kept), 1)


if __name__ == "__main__":
    unittest.main()

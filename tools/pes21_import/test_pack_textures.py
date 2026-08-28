"""Tests for finding a mesh's texture in a 4cc pack, and for the kit-hiding
convention that decides which meshes are drawn at all.

HDG's armour rendered as the team kit map: hdg_2411 and hdg_2402 ship their
armor_bsm/cape_bsm textures in the pack's Common/u0XXXp1/ directory (per-kit
shared textures), which export_textures never searched - it only looked beside
the .fmdl. DBG's players rendered as torn chimeras: their multi-form boots.fmdl
points every mesh at a per-character kit-slot texture (<char>_u0XXXp0), the
pack ships the real art as <char>_u0XXXp1.dds, and the forms a player does NOT
use are hidden with a fully transparent texture. The importer resolved none of
this, so hidden forms were imported and everything sampled the kit map.

Run: python3 -m unittest test_pack_textures -v
"""

import os
import shutil
import tempfile
import unittest

from PIL import Image

import fmdl_to_fullbody


def write_png(path, alpha):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.new("RGBA", (4, 4), (200, 100, 50, alpha)).save(path)


class PackLayout(unittest.TestCase):
    """A little 4cc pack: <pack>/Boots/kNNNN - Name/ and <pack>/Common/."""

    def setUp(self):
        self.pack = tempfile.mkdtemp()
        self.player = os.path.join(self.pack, "Boots", "k2009 - Master Roshi")
        self.common = os.path.join(self.pack, "Common")
        os.makedirs(self.player)
        os.makedirs(self.common)

    def tearDown(self):
        shutil.rmtree(self.pack)


class FindTextureFile(PackLayout):
    def test_exact_name_beside_the_fmdl_wins(self):
        write_png(os.path.join(self.player, "kidbuu.png"), 255)
        self.assertEqual(
            fmdl_to_fullbody.find_texture_file(self.player, "kidbuu"),
            os.path.join(self.player, "kidbuu.png"))

    def test_kit_slot_suffix_p0_resolves_to_the_first_kit_p1(self):
        """dbg_2009: the mesh says roshi_u0XXXp0, the pack ships roshi_u0XXXp1."""
        write_png(os.path.join(self.player, "roshi_u0XXXp1.png"), 255)
        self.assertEqual(
            fmdl_to_fullbody.find_texture_file(self.player, "roshi_u0XXXp0"),
            os.path.join(self.player, "roshi_u0XXXp1.png"))

    def test_shared_textures_in_common_are_found(self):
        """dbg's goku body art lives in Common, not in any player's folder."""
        write_png(os.path.join(self.common, "goku_u0XXXp1.png"), 255)
        self.assertEqual(
            fmdl_to_fullbody.find_texture_file(self.player, "goku_u0XXXp0"),
            os.path.join(self.common, "goku_u0XXXp1.png"))

    def test_per_kit_shared_textures_in_common_are_found(self):
        """hdg's armor_bsm lives in Common/u0XXXp1/ (one copy per kit)."""
        write_png(os.path.join(self.common, "u0XXXp1", "armor_bsm.png"), 255)
        self.assertEqual(
            fmdl_to_fullbody.find_texture_file(self.player, "armor_bsm"),
            os.path.join(self.common, "u0XXXp1", "armor_bsm.png"))

    def test_the_players_own_folder_outranks_common(self):
        write_png(os.path.join(self.player, "roshi_u0XXXp1.png"), 255)
        write_png(os.path.join(self.common, "roshi_u0XXXp1.png"), 255)
        self.assertEqual(
            fmdl_to_fullbody.find_texture_file(self.player, "roshi_u0XXXp0"),
            os.path.join(self.player, "roshi_u0XXXp1.png"))

    def test_the_bare_kit_slot_is_never_resolved_to_a_file(self):
        """u0XXXp0 with no prefix IS the engine's kit slot: the team kit is
        swapped in at run time, so no baked file may claim it - k2010 ships a
        u0XXXp1.dds that must not be picked up."""
        write_png(os.path.join(self.player, "u0XXXp1.png"), 255)
        self.assertIsNone(
            fmdl_to_fullbody.find_texture_file(self.player, "u0XXXp0"))

    def test_a_texture_the_pack_does_not_ship_is_none(self):
        self.assertIsNone(
            fmdl_to_fullbody.find_texture_file(self.player, "nowhere"))


class TextureIsHider(PackLayout):
    def test_a_fully_transparent_texture_hides_its_mesh(self):
        path = os.path.join(self.common, "jackiechun_u0XXXp1.png")
        write_png(path, 0)
        self.assertTrue(fmdl_to_fullbody.texture_is_hider(path))

    def test_an_opaque_texture_does_not(self):
        path = os.path.join(self.player, "roshi_u0XXXp1.png")
        write_png(path, 255)
        self.assertFalse(fmdl_to_fullbody.texture_is_hider(path))

    def test_partial_alpha_is_artwork_not_a_hider(self):
        """dbg's logo texture is 59% opaque; kidbuu 57%. Art with alpha."""
        path = os.path.join(self.common, "logo_u0XXXp1.png")
        write_png(path, 120)
        self.assertFalse(fmdl_to_fullbody.texture_is_hider(path))

    def test_the_real_packs_hiders_are_not_quite_zero(self):
        """The tolerance is load-bearing, not defensive rounding. Every one of
        the 26 hider textures in the DBG pack peaks at alpha 2 rather than 0,
        so a `== 0` test would pass this suite while silently reverting every
        DBG player to the five-form chimera the hiding exists to prevent."""
        path = os.path.join(self.common, "broly_u0XXXp1.png")
        write_png(path, 2)
        self.assertTrue(fmdl_to_fullbody.texture_is_hider(path))

    def test_the_nearest_real_artwork_is_still_not_a_hider(self):
        """k2016's aura_scroll peaks at alpha 38 - the closest genuine art to
        the threshold in any of the three packs. It has to stay visible, or
        the tolerance has been opened too far."""
        path = os.path.join(self.player, "aura_scroll_u0XXXp1.png")
        write_png(path, 38)
        self.assertFalse(fmdl_to_fullbody.texture_is_hider(path))

    def test_a_mostly_transparent_texture_with_opaque_art_is_not_a_hider(self):
        """Max alpha, not mean: a decal that is bare canvas apart from the
        mark itself is artwork, and averaging would throw it away."""
        path = os.path.join(self.common, "decal_u0XXXp1.png")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        image = Image.new("RGBA", (4, 4), (200, 100, 50, 0))
        image.putpixel((1, 1), (200, 100, 50, 255))
        image.save(path)
        self.assertFalse(fmdl_to_fullbody.texture_is_hider(path))


if __name__ == "__main__":
    unittest.main()

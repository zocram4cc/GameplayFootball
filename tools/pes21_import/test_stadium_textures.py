"""Tests for what happens to a stadium texture's alpha channel on import.

PES cuts a lot of a stadium out with alpha: Planet Namek's clouds, the houses,
the ships and the Ginyu pods are flat quads whose shape lives entirely in the
alpha channel of a 2048 or 4096 square. The engine already honours that - the
geometry shader discards any fragment under 0.12 alpha (media/shaders/simple.frag)
- but the converter used to flatten every .dds to RGB on the grounds that "a
stadium shell wants plain RGB anyway", which turned every one of those cutouts
into an opaque black-cornered billboard.

Keeping the channel is not unconditional, though. A pack that ships a texture
whose alpha is uniformly zero (PES stores some that way, and so does the goal
netting) would have every one of its fragments discarded, and the mesh would
vanish altogether - which is worse than a hard-edged sprite. So the channel is
kept when it carries a cutout and dropped when it cannot.

Run: python3 -m unittest test_stadium_textures -v
"""

import unittest

import stadium_to_gf


class KeepingAlpha(unittest.TestCase):
    def test_a_real_cutout_is_kept(self):
        # namekclouds: mostly transparent, opaque where the cloud is
        self.assertTrue(stadium_to_gf.alpha_is_a_cutout(0, 255))
        self.assertTrue(stadium_to_gf.alpha_is_a_cutout(0, 200))
        self.assertTrue(stadium_to_gf.alpha_is_a_cutout(3, 252))

    def test_a_fully_opaque_channel_is_not_worth_keeping(self):
        self.assertFalse(stadium_to_gf.alpha_is_a_cutout(255, 255))

    def test_an_empty_channel_would_erase_the_mesh(self):
        # Everything under 0.12 alpha is discarded, so a uniformly zero channel
        # deletes the geometry rather than shaping it.
        self.assertFalse(stadium_to_gf.alpha_is_a_cutout(0, 0))
        self.assertFalse(stadium_to_gf.alpha_is_a_cutout(0, 20))

    def test_a_channel_that_would_leave_nothing_visible_is_dropped(self):
        # 30/255 is 0.118, just under the shader's threshold.
        self.assertFalse(stadium_to_gf.alpha_is_a_cutout(0, 30))
        self.assertTrue(stadium_to_gf.alpha_is_a_cutout(0, 32))


class ChoosingTheMode(unittest.TestCase):
    """What the PNG is written as, given the source's own channels."""

    def test_a_source_without_alpha_stays_rgb(self):
        self.assertEqual(stadium_to_gf.png_mode_for("RGB", None), "RGB")
        self.assertEqual(stadium_to_gf.png_mode_for("P", None), "RGB")

    def test_a_source_with_a_cutout_becomes_rgba(self):
        self.assertEqual(stadium_to_gf.png_mode_for("RGBA", (0, 255)), "RGBA")

    def test_a_source_whose_alpha_says_nothing_is_flattened(self):
        self.assertEqual(stadium_to_gf.png_mode_for("RGBA", (255, 255)), "RGB")
        self.assertEqual(stadium_to_gf.png_mode_for("RGBA", (0, 0)), "RGB")


if __name__ == "__main__":
    unittest.main()

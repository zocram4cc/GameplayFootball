"""Tests for rendering PES's cel-shading outline shells rather than losing them.

PES draws an outline as an inverted hull: the same mesh pushed out along its
normals a few centimetres - measured at about 4 cm on Planet Namek's houses,
27.268 m across against the 27.192 m mesh it outlines - with the front faces
culled, so only the far side of the shell survives and it shows exactly where it
pokes out past the silhouette.

Our engine's geometry passes cull *back* faces, so a shell written with the
source winding covers the mesh it was supposed to outline. Every imported stadium
looked flat and grey for that reason. Reversing the shell's winding makes the
engine cull the same faces PES culls, and the outline appears.

Run: python3 -m unittest test_stadium_outline -v
"""

import unittest

import stadium_to_gf


class IsOutlinePassTest(unittest.TestCase):
    def test_the_outline_texture_marks_a_shell(self):
        self.assertTrue(stadium_to_gf.is_outline_pass("outline.png"))
        self.assertTrue(stadium_to_gf.is_outline_pass("outline"))

    def test_a_qualified_outline_name_still_counts(self):
        # PES names these per part as often as not.
        self.assertTrue(stadium_to_gf.is_outline_pass("houses_outline.png"))
        self.assertTrue(stadium_to_gf.is_outline_pass("outline_02.ftex"))

    def test_ordinary_textures_are_left_alone(self):
        for name in ("namek.png", "capsulecorpship.png", "houses.png",
                     "namekclouds.png", "bill_01_bsm.png"):
            self.assertFalse(stadium_to_gf.is_outline_pass(name), name)

    def test_a_name_that_merely_contains_the_word_is_not_a_shell(self):
        # "outlines" is a different word, and a texture called "outlined_turf"
        # is artwork, not a pass. Matching loosely would silently invert real
        # geometry, which is worse than missing an outline.
        self.assertFalse(stadium_to_gf.is_outline_pass("outlined_turf.png"))
        self.assertFalse(stadium_to_gf.is_outline_pass("myoutline.png"))

    def test_nothing_is_not_a_shell(self):
        self.assertFalse(stadium_to_gf.is_outline_pass(None))
        self.assertFalse(stadium_to_gf.is_outline_pass(""))


class FaceWindingTest(unittest.TestCase):
    def test_an_ordinary_face_keeps_its_order(self):
        self.assertEqual(stadium_to_gf.face_winding(7, 8, 9, False), (7, 8, 9))

    def test_a_shell_face_is_reversed(self):
        self.assertEqual(stadium_to_gf.face_winding(7, 8, 9, True), (9, 8, 7))

    def test_reversing_twice_is_the_original(self):
        once = stadium_to_gf.face_winding(1, 2, 3, True)
        self.assertEqual(stadium_to_gf.face_winding(*once, True), (1, 2, 3))


if __name__ == "__main__":
    unittest.main()

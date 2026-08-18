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


class OutlineWidth(unittest.TestCase):
    """How far the shell is pushed out, so the rim is visible at all.

    PES pushes its outline shells about 4 cm along the normal, which is right for
    a character filmed from a few metres. Planet Namek's scenery is 50 to 600 m
    from the camera, and at 600 m four centimetres is well under a pixel - the
    shells draw (they cover a few tenths of a percent of the frame) but the anime
    line never reads. The push is scaled with the distance the mesh will be seen
    from instead, so the rim stays a few pixels wide wherever it is.
    """

    def test_close_scenery_keeps_something_like_pess_own_offset(self):
        self.assertLess(stadium_to_gf.outline_offset(20.0), 0.15)
        self.assertGreaterEqual(stadium_to_gf.outline_offset(20.0), 0.04)

    def test_distant_scenery_is_pushed_much_further(self):
        near = stadium_to_gf.outline_offset(40.0)
        far = stadium_to_gf.outline_offset(400.0)
        self.assertGreater(far, near * 5)

    def test_the_rim_stays_about_the_same_width_on_screen(self):
        # offset / distance is the angle it subtends; hold it roughly constant
        angles = [stadium_to_gf.outline_offset(d) / d for d in (60.0, 150.0, 300.0)]
        self.assertLess(max(angles) - min(angles), 0.0005)

    def test_nothing_is_pushed_out_absurdly(self):
        self.assertLessEqual(stadium_to_gf.outline_offset(5000.0), 2.5)

    def test_a_mesh_at_the_origin_still_gets_an_outline(self):
        self.assertGreater(stadium_to_gf.outline_offset(0.0), 0.0)

    def test_pushing_a_vertex_moves_it_along_its_normal(self):
        moved = stadium_to_gf.push_along_normal((1.0, 0.0, 0.0), (1.0, 0.0, 0.0), 0.5)
        self.assertAlmostEqual(moved[0], 1.5, places=5)
        self.assertAlmostEqual(moved[1], 0.0, places=5)

    def test_a_degenerate_normal_leaves_the_vertex_alone(self):
        moved = stadium_to_gf.push_along_normal((2.0, 3.0, 4.0), (0.0, 0.0, 0.0), 0.5)
        self.assertEqual(moved, (2.0, 3.0, 4.0))

    def test_a_shell_gets_bigger_not_smaller(self):
        # The shells are written with reversed winding - that is what makes their
        # visible side face the camera - so the normals derived from it point
        # inward, and pushing along them shrank the shell instead of growing it.
        # A cube wound that way has to come out larger.
        corners = [(x, y, z) for x in (-1.0, 1.0) for y in (-1.0, 1.0) for z in (-1.0, 1.0)]
        # two triangles per face, wound inward
        faces = [(0, 2, 1), (1, 2, 3), (4, 5, 6), (5, 7, 6),
                 (0, 1, 4), (1, 5, 4), (2, 6, 3), (3, 6, 7),
                 (0, 4, 2), (2, 4, 6), (1, 3, 5), (3, 7, 5)]
        widened = stadium_to_gf._widen_outline(corners, faces)
        before = max(c[0] for c in corners) - min(c[0] for c in corners)
        after = max(w[0] for w in widened) - min(w[0] for w in widened)
        self.assertGreater(after, before)


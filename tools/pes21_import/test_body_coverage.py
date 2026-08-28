"""Tests for judging whether an imported body actually clothes the rig.

A 4cc aesthetic export is not always a body. The packs override PES's slots -
boots, gloves, face - and lean on the invisible-kit trick, where the character
you see is those pieces drawn over PES's own base body with a transparent kit
hiding it. Read one of those exports as a whole body and you get what the
showcase showed: a fan of wing blades with no player attached, a headless
torso, a figure framed to nothing by a backdrop mesh 362 m across.

The test is geometric rather than by mesh name, because names are the pack
author's business: for each joint of the native rig, is there any geometry near
it? Measured over the models that do render whole - the stock body, lcg_2709,
ateam_70201 - the furthest any joint sits from the nearest vertex is 0.18 m, so
0.20 m separates them from lcg_2702, which leaves 17 of 20 joints bare.

hdg_2402 was cited here as a fourth reference and should not have been: it
ships no head at all, and proximity could not tell, because a collar ring sits
0.083 m under the head joint. A model that misses the check was calibrating
the check. head_vertex_count is what catches that case.

Run: python3 -m unittest test_body_coverage -v
"""

import unittest

import body_coverage


# A rig small enough to reason about: a head, a hand and a foot.
BIND = {"head": (0.0, 0.0, 1.6), "left_hand": (0.6, 0.0, 1.0),
        "left_ankle": (0.2, 0.0, 0.1)}


class WhichJointsAreBare(unittest.TestCase):
    def test_geometry_at_every_joint_leaves_none_bare(self):
        verts = [(0.0, 0.0, 1.6), (0.6, 0.0, 1.0), (0.2, 0.0, 0.1)]
        self.assertEqual(body_coverage.bare_joints(verts, BIND), [])

    def test_a_missing_head_is_reported(self):
        verts = [(0.6, 0.0, 1.0), (0.2, 0.0, 0.1)]
        self.assertEqual(body_coverage.bare_joints(verts, BIND), ["head"])

    def test_geometry_near_a_joint_counts_as_covering_it(self):
        # a collar is not a head, but at 0.05 m it is geometry at the neck
        verts = [(0.0, 0.05, 1.6), (0.6, 0.0, 1.0), (0.2, 0.0, 0.1)]
        self.assertEqual(body_coverage.bare_joints(verts, BIND), [])

    def test_geometry_the_far_side_of_the_pitch_covers_nothing(self):
        self.assertEqual(len(body_coverage.bare_joints([(80.0, 0.0, 0.0)], BIND)), 3)

    def test_an_export_with_no_vertices_leaves_every_joint_bare(self):
        self.assertEqual(len(body_coverage.bare_joints([], BIND)), 3)

    def test_the_radius_is_the_measured_one_by_default(self):
        # 0.18 m is the worst joint on a body that renders whole; 0.20 is the line
        self.assertEqual(body_coverage.BARE_RADIUS, 0.20)


class GeometryThatIsNotAPlayer(unittest.TestCase):
    """Exports that carry scenery along with the character.

    lcg_2718 ships a 'bg_bsm' backdrop reaching 362 m. Nothing is wrong with the
    mesh; it simply is not part of a footballer, and while it is in the file the
    model's bounds are the backdrop's, so the player frames down to a dot.
    """

    def test_a_vertex_past_the_envelope_is_a_stray(self):
        self.assertEqual(body_coverage.strays([(0.0, 0.0, 1.0), (362.0, 0.0, 0.0)]), 1)

    def test_a_body_sized_model_has_none(self):
        self.assertEqual(body_coverage.strays([(0.6, 0.0, 1.0), (-0.6, 0.0, 2.2)]), 0)

    def test_the_envelope_clears_the_tallest_whole_model(self):
        # hdg_2421 stands 2.20 m and is whole; the limit must not call it scenery
        self.assertEqual(body_coverage.strays([(0.0, 0.0, 2.20)]), 0)


class TheVerdict(unittest.TestCase):
    def test_a_whole_body_is_whole(self):
        verts = [(0.0, 0.0, 1.6), (0.6, 0.0, 1.0), (0.2, 0.0, 0.1)]
        self.assertEqual(body_coverage.verdict(verts, BIND)[0], "whole")

    def test_a_slot_override_needs_the_base_body_under_it(self):
        # what PES draws it over, and what --base composites in
        self.assertEqual(body_coverage.verdict([(0.6, 0.0, 1.0)], BIND)[0], "needs base")

    def test_scenery_is_called_out_separately(self):
        verts = [(0.0, 0.0, 1.6), (0.6, 0.0, 1.0), (0.2, 0.0, 0.1), (362.0, 0.0, 0.0)]
        self.assertEqual(body_coverage.verdict(verts, BIND)[0], "carries scenery")

    def test_the_verdict_says_which_joints_are_bare(self):
        self.assertIn("head", body_coverage.verdict([(0.6, 0.0, 1.0)], BIND)[1])


if __name__ == "__main__":
    unittest.main()


class AHeadThatIsNotThere(unittest.TestCase):
    """Proximity cannot answer this one. hdg_2402 ships no head - its pack
    folder is "k2402 - Helldiver Headless" and PES's base body supplies it -
    yet its nearest vertex to the head joint is 0.083 m, because an open
    collar ring sits right under the joint. It passed as "whole"."""

    # The head check needs the neck, since it measures along neck -> head.
    RIG = dict(BIND, neck=(0.0, 0.0, 1.5))

    def test_a_collar_under_the_joint_is_not_a_head(self):
        collar = [(0.0, 0.0, 1.6 - 0.08), (0.05, 0.0, 1.6 - 0.09)]
        self.assertEqual(body_coverage.head_vertex_count(collar, self.RIG), 0)

    def test_geometry_above_the_joint_is_a_head(self):
        head = [(0.0, 0.0, 1.6 + 0.10 + i * 0.001) for i in range(64)]
        self.assertGreaterEqual(body_coverage.head_vertex_count(head, self.RIG),
                                body_coverage.HEAD_MIN_VERTICES)

    def test_a_headless_model_needs_the_base_body(self):
        """Every joint clothed and still not a whole body."""
        at_joints = [self.RIG[j] for j in self.RIG]
        call, detail = body_coverage.verdict(at_joints, self.RIG)
        self.assertEqual(call, "needs base")
        self.assertIn("no head of its own", detail)

    def test_a_headed_model_is_whole(self):
        clothed = [self.RIG[j] for j in self.RIG]
        clothed += [(0.0, 0.0, 1.6 + 0.10 + i * 0.001) for i in range(64)]
        call, _ = body_coverage.verdict(clothed, self.RIG)
        self.assertEqual(call, "whole")

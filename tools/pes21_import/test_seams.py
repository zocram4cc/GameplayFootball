"""Where two parts of a body cover the same place, they have to move together.

PES builds a player out of separate shells that overlap rather than meet: measured on
the stock body, `shirt` and `sleeves` share no vertex position at all, but 328 of the
shirt's vertices lie within 20 mm of a sleeve vertex - the sleeve is nested inside the
shirt over the shoulder.

They are weighted per part, and over that overlap they disagree: 198 of those 328
pairs take a different set of joints. The shirt at the shoulder is chest 0.89 plus
clavicle 0.11 while the sleeve at the same place is chest 0.44 plus shoulder 0.56, so
the moment the shoulder turns, the sleeve swings and the shirt does not, and one
surface comes through the other. That is the seam between arm and body.

A skin weight is a property of a place on the body, not of the garment that covers it.
"""

import math
import unittest

import seams


class Agreement(unittest.TestCase):
    def test_two_parts_at_one_place_end_up_agreeing(self):
        # A shirt vertex and a sleeve vertex 5 mm apart, weighted differently.
        parts = [
            [((0.0, 0.0, 1.4), [(6, 0.9), (5, 0.1)])],
            [((0.0, 0.005, 1.4), [(6, 0.4), (7, 0.6)])],
        ]
        out = seams.agree(parts, radius=0.02)
        self.assertEqual(set(dict(out[0][0][1])), set(dict(out[1][0][1])))

    def test_the_blend_is_between_what_the_two_said(self):
        # Sharing `chest`, as the shoulder's two shells do, and disagreeing about the
        # rest: the result carries both of their own bones.
        parts = [
            [((0.0, 0.0, 1.4), [(6, 0.5), (11, 0.5)])],
            [((0.0, 0.005, 1.4), [(6, 0.5), (12, 0.5)])],
        ]
        out = seams.agree(parts, radius=0.02)
        weights = dict(out[0][0][1])
        self.assertGreater(weights.get(11, 0.0), 0.0)
        self.assertGreater(weights.get(12, 0.0), 0.0)

    def test_a_vertex_with_no_neighbour_in_another_part_is_untouched(self):
        parts = [
            [((0.0, 0.0, 1.4), [(6, 1.0)]), ((3.0, 0.0, 1.4), [(2, 1.0)])],
            [((0.0, 0.005, 1.4), [(7, 1.0)])],
        ]
        out = seams.agree(parts, radius=0.02)
        self.assertEqual(out[0][1][1], [(2, 1.0)])

    def test_vertices_inside_one_part_are_untouched(self):
        # Two vertices of the same mesh, however close, are the artist's business.
        parts = [[((0.0, 0.0, 1.4), [(6, 1.0)]), ((0.0, 0.001, 1.4), [(2, 1.0)])]]
        out = seams.agree(parts, radius=0.02)
        self.assertEqual(out[0][0][1], [(6, 1.0)])
        self.assertEqual(out[0][1][1], [(2, 1.0)])

    def test_weights_still_sum_to_one(self):
        parts = [
            [((0.0, 0.0, 1.4), [(6, 0.9), (5, 0.1)])],
            [((0.0, 0.004, 1.4), [(6, 0.4), (7, 0.6)])],
            [((0.0, 0.008, 1.4), [(7, 0.3), (2, 0.7)])],
        ]
        for part in seams.agree(parts, radius=0.02):
            for _, joints in part:
                self.assertAlmostEqual(sum(w for _, w in joints), 1.0, places=5)

    def test_no_more_than_three_influences_survive(self):
        # The engine packs three per vertex; a blend that produced four would be
        # silently truncated by whoever wrote it out.
        parts = [
            [((0.0, 0.0, 1.4), [(1, 0.5), (2, 0.5)])],
            [((0.0, 0.004, 1.4), [(3, 0.5), (4, 0.5)])],
        ]
        for part in seams.agree(parts, radius=0.02):
            for _, joints in part:
                self.assertLessEqual(len(joints), 3)

    def test_the_nearer_neighbour_counts_for_more(self):
        parts = [
            [((0.0, 0.0, 0.0), [(1, 1.0)])],
            [((0.0, 0.001, 0.0), [(1, 0.5), (2, 0.5)])],
            [((0.0, 0.019, 0.0), [(1, 0.5), (3, 0.5)])],
        ]
        weights = dict(seams.agree(parts, radius=0.02)[0][0][1])
        self.assertGreater(weights.get(2, 0.0), weights.get(3, 0.0))

    def test_a_far_part_is_not_dragged_in(self):
        parts = [
            [((0.0, 0.0, 0.0), [(1, 1.0)])],
            [((0.0, 0.5, 0.0), [(2, 1.0)])],
        ]
        out = seams.agree(parts, radius=0.02)
        self.assertEqual(out[0][0][1], [(1, 1.0)])
        self.assertEqual(out[1][0][1], [(2, 1.0)])

    def test_running_it_again_settles_rather_than_drifts(self):
        # Not exactly idempotent, and it cannot be: each vertex still counts its own
        # say first, so two surfaces converge on each other without ever being one
        # number. What matters is that a second pass moves less than the first did,
        # and that both sides already name the same joints after the first.
        parts = [
            [((0.0, 0.0, 1.4), [(6, 0.9), (5, 0.1)])],
            [((0.0, 0.005, 1.4), [(6, 0.4), (7, 0.6)])],
        ]
        once = seams.agree(parts, radius=0.02)
        twice = seams.agree(once, radius=0.02)
        self.assertEqual(set(dict(once[0][0][1])), set(dict(once[1][0][1])))

        def moved(a, b):
            return max(abs(dict(a[i][0][1]).get(j, 0.0) - dict(b[i][0][1]).get(j, 0.0))
                       for i in (0, 1)
                       for j in set(dict(a[i][0][1])) | set(dict(b[i][0][1])))

        self.assertLess(moved(once, twice), moved(parts, once))

    def test_an_empty_part_is_allowed(self):
        self.assertEqual(seams.agree([[], []], radius=0.02), [[], []])

    def test_the_disagreement_it_is_meant_to_remove(self):
        # The measured case, in miniature: the two surfaces of the shoulder.
        shirt = [((0.19, 0.0, 1.42), [(6, 0.89), (11, 0.11)])]
        sleeve = [((0.193, 0.0, 1.421), [(6, 0.44), (12, 0.56)])]
        # Counted per vertex, so a disagreeing pair is two: each of them is looking at
        # a neighbour that says something else.
        before = seams.disagreement([shirt, sleeve], radius=0.02)
        after = seams.disagreement(seams.agree([shirt, sleeve], radius=0.02), radius=0.02)
        self.assertEqual(before, 2)
        self.assertEqual(after, 0)


if __name__ == "__main__":
    unittest.main()


class NotTheSamePlace(unittest.TestCase):
    """An arm hanging beside the ribs is close to the shirt and is not part of it."""

    def test_surfaces_sharing_no_bone_are_left_alone(self):
        ribs = [((0.16, 0.0, 1.2), [(6, 1.0)])]          # shirt, on the chest
        arm = [((0.175, 0.0, 1.2), [(12, 1.0)])]         # the arm hanging beside it
        out = seams.agree([ribs, arm], radius=0.02)
        self.assertEqual(out[0][0][1], [(6, 1.0)])
        self.assertEqual(out[1][0][1], [(12, 1.0)])

    def test_surfaces_sharing_a_bone_still_blend(self):
        shirt = [((0.19, 0.0, 1.42), [(6, 0.89), (11, 0.11)])]
        sleeve = [((0.193, 0.0, 1.421), [(6, 0.44), (12, 0.56)])]
        out = seams.agree([shirt, sleeve], radius=0.02)
        self.assertNotEqual(out[0][0][1], shirt[0][1])
        self.assertEqual(set(dict(out[0][0][1])), set(dict(out[1][0][1])))


class ColourRoundTrip(unittest.TestCase):
    """Reconciling a seam means reading back what was written, so it has to survive."""

    def test_a_blend_survives_being_written_and_read(self):
        from fmdl_to_fullbody import encode_color, decode_color
        # Order is not preserved and should not be: encode_color puts the strongest
        # influence in channel 0, because the engine asserts on it.
        for joints in ([(6, 1.0)], [(6, 0.5), (12, 0.5)], [(6, 0.44), (12, 0.56)],
                       [(2, 0.6), (5, 0.3), (9, 0.1)]):
            back = dict(decode_color(encode_color(joints)))
            self.assertEqual(set(back), {j for j, _ in joints})
            for joint, want in joints:
                self.assertAlmostEqual(back[joint], want, places=1)

    def test_the_strongest_influence_stays_first(self):
        from fmdl_to_fullbody import encode_color, decode_color
        back = decode_color(encode_color([(9, 0.2), (3, 0.8)]))
        self.assertEqual(back[0][0], 3)


class Passes(unittest.TestCase):
    def test_more_passes_bring_them_closer(self):
        def apart(parts):
            a = dict(parts[0][0][1])
            b = dict(parts[1][0][1])
            return sum(abs(a.get(j, 0.0) - b.get(j, 0.0)) for j in set(a) | set(b))

        parts = [
            [((0.0, 0.0, 1.4), [(6, 0.9), (11, 0.1)])],
            [((0.0, 0.012, 1.4), [(6, 0.4), (12, 0.6)])],
        ]
        one = seams.agree(parts, radius=0.02)
        three = one
        for _ in range(2):
            three = seams.agree(three, radius=0.02)
        self.assertLess(apart(three), apart(one))
        self.assertLess(apart(one), apart(parts))

    def test_reconcile_reports_what_it_moved(self):
        shirt = [((0.19, 0.0, 1.42), [(6, 0.89), (11, 0.11)])]
        sleeve = [((0.193, 0.0, 1.421), [(6, 0.44), (12, 0.56)])]
        parts = [shirt, sleeve]
        out = seams.reconcile(parts)
        changed, migrated = seams.reconciled_count(parts, out)
        self.assertGreater(changed, 0)
        self.assertLessEqual(migrated, changed)

    def test_reconcile_leaves_a_lone_vertex_alone(self):
        parts = [[((0.0, 0.0, 0.0), [(6, 1.0)])],
                 [((5.0, 0.0, 0.0), [(12, 1.0)])]]
        out = seams.reconcile(parts)
        self.assertEqual(seams.reconciled_count(parts, out), (0, 0))


class WeightsNotColours(unittest.TestCase):
    """The blend runs on the influence lists, not on what a colour can hold.

    A hand vertex can be on joint 44 and the colour a glove vertex beside it
    carries cannot say so, so reconciling the colours would agree the two
    surfaces onto the fallback wrist instead of onto the finger.
    """

    def test_a_joint_no_colour_could_carry_survives(self):
        import retarget
        finger = retarget.JOINT_ID["left_index_dip"]
        self.assertGreater(finger, retarget.MAX_VERTEX_COLOUR_JOINT)
        parts = [[((0.6, 0.0, 1.0), [(finger, 0.8), (15, 0.2)])],
                 [((0.6008, 0.0, 1.0), [(finger, 0.6), (15, 0.4)])]]
        out = seams.reconcile(parts)
        for part in out:
            self.assertIn(finger, dict(part[0][1]))


class WeldsCoincidentVertices(unittest.TestCase):
    """A UV seam duplicates a vertex; both halves must skin the same way.

    Measured on lcg_2709: two vertices at the same millimetre carried
    `right_shoulder 0.29` and `right_clavicle 0.29`, and the bind-pose bake
    moved one 0.15 m and left the other - a 0.5 mm edge stretched 315x, drawn
    as a shard fanning out of the model.
    """

    def test_duplicates_in_one_part_end_up_identical(self):
        part = [((0.0, 0.0, 2.2), [(1, 0.4), (2, 0.6)]),
                ((0.0, 0.0, 2.2), [(1, 0.4), (3, 0.6)])]
        welded = seams.weld([part])[0]
        self.assertEqual(welded[0][1], welded[1][1])

    def test_the_agreed_weights_are_the_sum_of_both(self):
        part = [((0.0, 0.0, 1.0), [(7, 1.0)]),
                ((0.0, 0.0, 1.0), [(9, 1.0)])]
        welded = seams.weld([part])[0]
        self.assertEqual(dict(welded[0][1]), {7: 0.5, 9: 0.5})

    def test_a_vertex_a_millimetre_away_is_left_alone(self):
        # 3 mm apart with no bone in common: inside WELD_RADIUS, so this is the
        # guard being tested rather than the radius. It used to be 50 mm apart,
        # ten times the radius, and passed without exercising anything.
        part = [((0.0, 0.0, 1.0), [(7, 1.0)]),
                ((0.0, 0.0, 1.003), [(9, 1.0)])]
        welded = seams.weld([part])[0]
        self.assertEqual(welded[0][1], [(7, 1.0)])
        self.assertEqual(welded[1][1], [(9, 1.0)])

    def test_a_weight_gradient_inside_one_run_survives(self):
        # Twelve centimetres of forearm at 4 mm spacing, weights handing over
        # from one joint to the next - what an artist authors, and what the
        # transitive union over the 5 mm band collapsed into a single blend
        # (measured: a 2,544-vertex hand's 1,900 blends became one).
        count = 30
        part = [((0.0, 0.0, 1.0 + i * 0.004),
                 [(7, 1.0 - i / (count - 1.0)), (9, i / (count - 1.0))])
                for i in range(count)]
        faces = [(i, i + 1, min(i + 2, count - 1)) for i in range(count - 2)]
        welded = seams.weld([part], faces=[faces])[0]
        # Blends, not token order: the pass sorts an influence list by weight.
        self.assertEqual([dict(j) for _, j in welded], [dict(j) for _, j in part])

    def test_a_seam_with_no_edge_between_the_halves_still_welds(self):
        # The same 3 mm gap, sharing a bone, but with NO edge joining the two:
        # that is a seam's two halves and they must still agree. A UV seam
        # leaves the surface in one piece, so this cannot be judged by
        # connected components - only by whether an edge joins the pair.
        part = [((0.0, 0.0, 1.0), [(7, 0.9), (9, 0.1)]),
                ((0.0, 0.1, 1.0), [(7, 1.0)]),
                ((0.0, 0.0, 1.003), [(9, 0.9), (7, 0.1)]),
                ((0.1, 0.0, 1.0), [(9, 1.0)])]
        # One connected sheet - 0-1, 1-3, 3-2 - with no 0-2 edge.
        faces = [(0, 1, 3), (1, 3, 2)]
        welded = seams.weld([part], faces=[faces])[0]
        self.assertEqual(welded[0][1], welded[2][1])

    def test_without_faces_only_exact_duplicates_agree(self):
        part = [((0.0, 0.0, 1.0), [(7, 1.0)]),
                ((0.0, 0.0, 1.003), [(7, 0.5), (9, 0.5)])]
        welded = seams.weld([part])[0]
        self.assertEqual(welded[0][1], [(7, 1.0)])

    def test_positions_and_order_survive(self):
        part = [((0.1, 0.2, 0.3), [(1, 1.0)]), ((0.4, 0.5, 0.6), [(2, 1.0)])]
        welded = seams.weld([part])[0]
        self.assertEqual([p for p, _ in welded], [p for p, _ in part])

    def test_a_pair_straddling_a_grid_boundary_still_welds(self):
        # lcg_2709's own numbers: 0.52 mm apart, either side of a 1 mm cell
        # edge. Bucketing on a rounded coordinate welded neither.
        part = [((-0.150000, 0.110000, 2.200000), [(5, 0.29)]),
                ((-0.150520, 0.110000, 2.200000), [(7, 0.29)])]
        welded = seams.weld([part])[0]
        self.assertEqual(welded[0][1], welded[1][1])

    def test_a_chain_of_duplicates_ends_up_in_one_group(self):
        part = [((0.0, 0.0, 0.0), [(1, 1.0)]),
                ((0.0006, 0.0, 0.0), [(2, 1.0)]),
                ((0.0012, 0.0, 0.0), [(3, 1.0)])]
        welded = seams.weld([part])[0]
        self.assertEqual(welded[0][1], welded[2][1])

    def test_two_surfaces_sharing_no_bone_are_left_alone(self):
        # A fingertip resting against a thigh is 2 mm from it and is not the
        # same place on the body.
        part = [((0.0, 0.0, 1.0), [(4, 1.0)]),
                ((0.0, 0.0, 1.002), [(41, 1.0)])]
        welded = seams.weld([part])[0]
        self.assertEqual(welded[0][1], [(4, 1.0)])
        self.assertEqual(welded[1][1], [(41, 1.0)])

"""Tests for writing finger skin weights out of a PES model.

A vertex colour channel is jointID*10 + weight*9 over 255, so it can name joint
25 at the very most and the body rig already uses twenty. The finger joints are
26 upwards and no colour can reach them: they go in a sidecar weight file beside
the .ase, up to four influences a vertex, which is PES's own maximum (measured
over the base package's parts, 14,175 vertices: 1, 2, 3 or 4 non-zero bone
weights and never a fifth).

The colours stay, as the fallback. A finger weight has to become something in
them, and the only honest something is the wrist - which is exactly the hand an
engine without the sidecar has always drawn.

Run: python3 -m unittest test_hand_weights -v
"""

import unittest

import fmdl_to_fullbody as f
import retarget
import seams


class ColourFallback(unittest.TestCase):
    def test_the_colour_ceiling_is_what_a_full_weight_can_encode(self):
        self.assertEqual(retarget.MAX_VERTEX_COLOUR_JOINT, 24)
        self.assertLessEqual(retarget.MAX_VERTEX_COLOUR_JOINT * 10 + 9, 255)
        self.assertGreater((retarget.MAX_VERTEX_COLOUR_JOINT + 1) * 10 + 9, 255)

    def test_a_body_joint_is_its_own_fallback(self):
        for name in retarget.GF_JOINT_ORDER[:20]:
            joint = retarget.JOINT_ID[name]
            self.assertEqual(retarget.colour_fallback_joint(joint), joint, name)

    def test_a_finger_falls_back_to_its_own_wrist(self):
        for side in ("left", "right"):
            wrist = retarget.JOINT_ID["%s_hand" % side]
            for finger in ("thumb", "index", "middle", "pinky", "ring"):
                for segment in ("mata", "mcp", "pip"):
                    joint = retarget.JOINT_ID["%s_%s_%s" % (side, finger, segment)]
                    self.assertEqual(retarget.colour_fallback_joint(joint), wrist,
                                     "%s %s %s" % (side, finger, segment))

    def test_every_joint_has_an_encodable_fallback(self):
        for name in retarget.GF_JOINT_ORDER:
            joint = retarget.colour_fallback_joint(retarget.JOINT_ID[name])
            self.assertLess(joint, len(retarget.GF_BODY_NODES), name)

    def test_a_finger_weighted_vertex_still_encodes_a_colour(self):
        finger = retarget.JOINT_ID["left_index_pip"]
        wrist = retarget.JOINT_ID["left_hand"]
        channels = f.encode_color([(finger, 0.7), (wrist, 0.3)])
        for channel in channels:
            self.assertGreaterEqual(channel, 0.0)
            self.assertLessEqual(channel, 1.0)
        # both influences collapse onto the wrist, and it comes out whole
        decoded = f.decode_color(channels)
        self.assertEqual([joint for joint, _ in decoded], [wrist])
        self.assertAlmostEqual(decoded[0][1], 1.0, places=3)

    def test_collapsing_does_not_lose_a_body_influence(self):
        finger = retarget.JOINT_ID["right_middle_dip"]
        chest = retarget.JOINT_ID["chest"]
        decoded = dict(f.decode_color(
            f.encode_color([(finger, 0.5), (chest, 0.5)])))
        self.assertIn(retarget.JOINT_ID["right_hand"], decoded)
        self.assertIn(chest, decoded)


class SidecarLines(unittest.TestCase):
    def test_a_vertex_becomes_one_line_of_position_and_influences(self):
        text = f.render_weights([((0.5, 0.25, 1.0), [(15, 0.6), (44, 0.4)])])
        lines = text.splitlines()
        self.assertEqual(lines[0], "# gfweights 1")
        self.assertEqual(lines[1], "0.500000 0.250000 1.000000 15:0.600000 44:0.400000")

    def test_positions_are_written_the_way_the_ase_writes_them(self):
        # the engine looks a weight up by exact float equality on the position,
        # so the same decimal text has to come out of both writers
        position = (0.123456789, -1.0 / 3.0, 2.0)
        text = f.render_weights([(position, [(44, 1.0)])])
        fields = text.splitlines()[1].split()
        self.assertEqual(fields[:3],
                         ["%.6f" % c for c in position])

    def test_a_duplicated_position_is_written_once(self):
        text = f.render_weights([((0.0, 0.0, 0.0), [(44, 1.0)]),
                                 ((0.0, 0.0, 0.0), [(45, 1.0)]),
                                 ((1.0, 0.0, 0.0), [(46, 1.0)])])
        self.assertEqual(len(text.splitlines()), 3)     # header plus two

    def test_influences_come_out_strongest_first(self):
        text = f.render_weights([((0.0, 0.0, 0.0),
                                  [(35, 0.2), (36, 0.5), (37, 0.3)])])
        fields = text.splitlines()[1].split()[3:]
        self.assertEqual([field.split(":")[0] for field in fields],
                         ["36", "37", "35"])

    def test_at_most_four_influences_survive(self):
        text = f.render_weights([((0.0, 0.0, 0.0),
                                  [(1, 0.3), (2, 0.3), (3, 0.2), (4, 0.1),
                                   (5, 0.1)])])
        fields = text.splitlines()[1].split()[3:]
        self.assertEqual(len(fields), 4)
        total = sum(float(field.split(":")[1]) for field in fields)
        self.assertAlmostEqual(total, 1.0, places=4)

    def test_nothing_to_write_is_no_file(self):
        self.assertIsNone(f.render_weights([]))

    def test_a_body_only_model_writes_no_sidecar(self):
        # a model that names no joint the colours cannot reach gains nothing
        # from a sidecar, and shipping one would only be another thing to keep
        # in step with the ase
        body = [((0.0, 0.0, 0.0), [(9, 1.0)]),
                ((1.0, 0.0, 0.0), [(15, 0.5), (14, 0.5)])]
        self.assertIsNone(f.render_weights(body))


class SeamsWithFourInfluences(unittest.TestCase):
    def test_the_blend_keeps_as_many_as_pes_weights_a_vertex_to(self):
        self.assertEqual(seams.MAX_INFLUENCES, 4)

    def test_overlapping_parts_agree_on_a_finger_joint(self):
        finger = retarget.JOINT_ID["left_index_pip"]
        wrist = retarget.JOINT_ID["left_hand"]
        # a glove vertex mostly on the wrist and a hand vertex mostly on the
        # finger, in the same place: they have to end up driving it together.
        # (Two surfaces naming no joint in common are left alone by design -
        # arms hang beside ribs in the bind pose - so each names both.)
        parts = [[((0.6, 0.0, 1.0), [(wrist, 0.9), (finger, 0.1)])],
                 [((0.6005, 0.0, 1.0), [(finger, 0.9), (wrist, 0.1)])]]
        agreed = seams.reconcile_skins(parts)
        for part in agreed:
            joints = dict(part[0][1])
            self.assertIn(finger, joints)
            self.assertIn(wrist, joints)
            # and the finger survived at full precision rather than collapsing
            self.assertGreater(joints[finger], 0.2)

    def test_four_influences_survive_a_blend(self):
        joints = [(1, 0.4), (2, 0.3), (3, 0.2), (4, 0.1)]
        parts = [[((0.0, 0.0, 0.0), list(joints))],
                 [((0.001, 0.0, 0.0), list(joints))]]
        agreed = seams.reconcile_skins(parts)
        self.assertEqual(len(agreed[0][0][1]), 4)
        self.assertAlmostEqual(sum(w for _, w in agreed[0][0][1]), 1.0, places=5)

    def test_a_vertex_with_no_neighbour_is_untouched(self):
        parts = [[((0.0, 0.0, 0.0), [(1, 0.6), (2, 0.4)])],
                 [((5.0, 0.0, 0.0), [(3, 1.0)])]]
        agreed = seams.reconcile_skins(parts)
        self.assertEqual(agreed[0][0][1], [(1, 0.6), (2, 0.4)])
        self.assertEqual(agreed[1][0][1], [(3, 1.0)])


if __name__ == "__main__":
    unittest.main()

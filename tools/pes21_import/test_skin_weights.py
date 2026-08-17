"""Tests for packing skin weights into ASE vertex colours.

The engine decodes each colour channel as jointID*10 + weight*9 and then
renormalises the three weights so they sum to 1 (humanoidbase.cpp), skipping
any channel whose decoded weight is <= 0.01 and asserting the first is not
zero. The encoder used to satisfy that assert by clamping *every* influence up
to 0.12 - which turned a vertex that was 99% on the spine with two traces of
arm into 79% spine and 10.5% on each arm bone once the engine renormalised. On
the base body's shirt that is the shoulder seam, and it showed.

Negligible influences are dropped instead, and what is left is renormalised, so
the proportions PES authored survive the round trip.

Run: python3 -m unittest test_skin_weights -v
"""

import unittest

import fmdl_to_fullbody as f


def decode(channels):
    """Mirror of the engine's decode, including its skip and renormalise."""
    bones = []
    for c in channels:
        raw = c * 255.0
        joint = int(raw // 10)
        weight = (raw - joint * 10.0) / 9.0
        bones.append((joint, weight))
    total = sum(w for _, w in bones)
    return [(j, w / total) for j, w in bones if w > 0.01]


class EncodeColorTest(unittest.TestCase):
    def test_dominant_influence_is_not_dragged_toward_traces(self):
        """The bug: two negligible influences became 10% each."""
        got = dict(decode(f.encode_color([(9, 0.99), (16, 0.005), (17, 0.005)])))
        self.assertAlmostEqual(got[9], 1.0, places=2)
        self.assertNotIn(16, got)
        self.assertNotIn(17, got)

    def test_a_real_blend_keeps_its_proportions(self):
        got = dict(decode(f.encode_color([(3, 0.5), (4, 0.3), (5, 0.2)])))
        self.assertAlmostEqual(got[3], 0.5, places=1)
        self.assertAlmostEqual(got[4], 0.3, places=1)
        self.assertAlmostEqual(got[5], 0.2, places=1)

    def test_weights_sum_to_one(self):
        for joints in ([(9, 0.99), (16, 0.005), (17, 0.005)],
                       [(3, 0.5), (4, 0.3), (5, 0.2)],
                       [(1, 0.7), (2, 0.3)],
                       [(11, 1.0)]):
            got = decode(f.encode_color(joints))
            self.assertAlmostEqual(sum(w for _, w in got), 1.0, places=2, msg=str(joints))

    def test_first_channel_is_never_zero(self):
        """humanoidbase.cpp asserts on it."""
        for joints in ([(0, 1.0)], [(9, 0.99), (16, 0.001)], [(5, 0.4), (6, 0.4), (7, 0.2)]):
            channels = f.encode_color(joints)
            self.assertGreater(channels[0], 0.0, msg=str(joints))

    def test_unweighted_vertex_still_encodes_something(self):
        """A vertex with no influences would trip the assert; it has to ride
        some joint, and the root is the only defensible choice."""
        channels = f.encode_color([])
        self.assertGreater(channels[0], 0.0)
        self.assertAlmostEqual(sum(w for _, w in decode(channels)), 1.0, places=2)

    def test_input_order_does_not_matter(self):
        """The strongest influence has to reach channel 0 whatever order the
        caller supplies, because that is the channel the engine asserts on."""
        channels = f.encode_color([(16, 0.05), (9, 0.95)])
        joint0 = int((channels[0] * 255.0) // 10)
        self.assertEqual(joint0, 9)

    def test_more_than_three_influences_keeps_the_strongest(self):
        got = dict(decode(f.encode_color(
            [(1, 0.4), (2, 0.3), (3, 0.2), (4, 0.1)])))
        self.assertIn(1, got)
        self.assertIn(2, got)
        self.assertNotIn(4, got)

    def test_joint_id_survives_the_round_trip(self):
        for joint in range(0, 20):
            channels = f.encode_color([(joint, 1.0)])
            self.assertEqual(int((channels[0] * 255.0) // 10), joint)


if __name__ == "__main__":
    unittest.main()

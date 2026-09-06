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
import retarget


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


class RebindStray(unittest.TestCase):
    """4cc packs a whole character into the boots slot, so the export's bone
    mapping is whatever that slot allows: k2587's only leg bones are the feet,
    which left 785 vertices a side on the ankle and none on the knee - a leg
    that cannot bend. A mapping that puts a vertex nowhere near itself is a slot
    artifact, and position is the only truth left.
    """

    JOINTS = {"left_knee": (0.1, 0.0, 0.5), "left_ankle": (0.1, 0.0, 0.1)}

    def test_a_vertex_on_its_own_joint_keeps_its_mapping(self):
        knee = f.JOINT_ID["left_knee"]
        self.assertFalse(f.rebind_stray((0.1, 0.0, 0.52), [knee], self.JOINTS))

    def test_a_shin_mapped_to_the_foot_is_rebound(self):
        ankle = f.JOINT_ID["left_ankle"]
        self.assertTrue(f.rebind_stray((0.1, 0.0, 0.48), [ankle], self.JOINTS))

    def test_geometry_far_from_every_joint_is_left_alone(self):
        # a cape tip: far from its mapped joint, but no joint is closer either
        ankle = f.JOINT_ID["left_ankle"]
        self.assertFalse(f.rebind_stray((0.1, 0.0, 0.1), [ankle], self.JOINTS))

    def test_a_big_belly_keeps_its_hip_when_a_fingertip_is_merely_as_close(self):
        # Wario: hip vertex 0.58 m from the hip, 0.57 m from a pinky tip
        joints = {"hip": (0.0, 0.0, 1.0), "left_pinky_dip": (0.68, 0.0, 1.0),
                  "left_hand": (0.6, 0.0, 1.05)}
        hip = f.JOINT_ID["hip"]
        self.assertFalse(f.rebind_stray((0.36, 0.44, 0.85), [hip], joints))

    def test_a_stray_never_lands_on_a_finger(self):
        # a knee vertex mapped to the foot, with a fingertip hanging right beside it
        joints = dict(self.JOINTS, left_pinky_dip=(0.1, 0.0, 0.49))
        ankle = f.JOINT_ID["left_ankle"]
        self.assertTrue(f.rebind_stray((0.1, 0.0, 0.48), [ankle], joints))
        self.assertNotIn("left_pinky_dip", f.body_joint_positions(joints))
        self.assertIn("left_knee", f.body_joint_positions(joints))


if __name__ == "__main__":
    unittest.main()


class NearestBoneNeverMixesLimbs(unittest.TestCase):
    """The guess for geometry a pack ships with no bone mapping.

    Measured on the installed models: guessing from the nearest joint POINT put
    a torso vertex at the waist on `right_hand 0.44 / right_elbow 0.31 /
    right_thigh 0.25` - the hand hangs beside the hip - while the vertex a
    millimetre away came out `middle 0.58 / chest 0.42`, and the bake pulled the
    pair 0.4 m apart (hdg_XXX23, 373x). A bone span cannot do that: at the waist
    the arm bone is a forearm away and the spine is touching.
    """

    def setUp(self):
        self.bind = retarget.gf_world_bind()
        self.names = {i: n for n, i in retarget.JOINT_ID.items()}

    def joints(self, position, model_scale=1.0):
        return {self.names[j] for j, _ in
                f.nearest_bone(position, self.bind, model_scale=model_scale)}

    def test_a_waist_vertex_beside_the_hand_stays_on_the_body(self):
        named = self.joints((-0.539, 0.186, 0.923))
        self.assertFalse(named & {"right_hand", "right_elbow", "left_hand"},
                         "the arm took a torso vertex: %s" % named)

    def test_a_shin_vertex_stays_on_the_leg(self):
        # Off lcg_2709, a 2.56 m character: its own coordinates, and the scale
        # the engine will draw it at (MEASURED_BODY_HEIGHT / its height).
        named = self.joints((0.47, 0.121, 0.382), model_scale=1.81 / 2.56)
        self.assertTrue(named <= {"left_knee", "left_ankle", "left_thigh"}, named)

    def test_a_cape_hanging_clear_of_the_body_is_carried_by_the_trunk(self):
        # Anywhere else and the bake swings it: the arms rotate 45 degrees
        # between the authoring pose and the rig, which is what spread
        # lcg_2704's cape into a pair of wings.
        named = self.joints((0.95, -0.55, 1.35))
        self.assertTrue(named & set(f.TRUNK_BONES), named)

    def test_a_thick_torso_beats_a_thin_arm_at_equal_distance(self):
        # The radii are what make this work: a surface a third of a metre from
        # both the spine and the upper arm belongs to the spine, which clothes
        # 0.31 m of skin, not to the arm, which clothes 0.27 m at a quarter of
        # the mass.
        named = self.joints((0.30, -0.30, 1.20))
        self.assertFalse(named & {"left_hand", "right_hand"}, named)

    def test_a_fingertip_still_lands_on_its_finger(self):
        named = self.joints(tuple(self.bind["left_index_dip"]))
        self.assertIn("left_index_dip", named)

    def test_a_vertex_is_never_shared_across_two_limbs(self):
        # This used to demand that a vertex name a parent and its child and
        # nothing else. That is what tore: one bone per vertex draws a hard
        # line wherever the winner changes, and the authoring->bind bake turns
        # the arm 45 degrees while leaving the trunk alone, so a line between
        # the chest and the shoulder moved its two sides half a metre apart
        # (smbg_2582: 795 edges torn at the bind, worst 23.7x). The weight is
        # shared along the skeleton now; what must still never happen is a
        # blend between two LIMBS, which is what put a hip on a fingertip.
        for position in ((0.0, 0.0, 1.4), (0.3, 0.0, 1.2), (-0.2, 0.1, 0.5),
                         (0.6, 0.0, 1.0), (0.0, 0.3, 2.0)):
            named = sorted(self.joints(position))
            self.assertLessEqual(len(named), f.MAX_INFLUENCES, named)
            # One of them is within BLEND_HOPS of all the others: the share
            # reaches that far from the bone that won and no further, so the
            # joints named are one run of the skeleton. A hand blended with a
            # hip has no such joint - that is the shape of the defect this
            # guards, and the reach along the spine (body..head, four hops end
            # to end) is not it.
            hub = [a for a in named
                   if all(f._bone_hops(a, b) <= f.BLEND_HOPS for b in named)]
            self.assertTrue(hub, "no joint within %d hops of all of %s"
                            % (f.BLEND_HOPS, named))

"""Tests for the PES hand rig: the skh_* bones as GF joints of their own.

Run: python3 -m unittest test_hand_rig -v
Set PES_HAND_SKL_L / PES_HAND_SKL_R to the real hand_[lr].skl to also check
the bind table against Konami's own (proprietary, stays outside the repo).
"""

import math
import os
import unittest

import pes_skl
import retarget


FINGERS = ("thumb", "index", "middle", "pinky", "ring")


class FingerJoints(unittest.TestCase):
    def test_every_pes_hand_bone_has_its_own_joint(self):
        # PES's hand rig is 19 bones a hand (thumb 3, the other four 4 each);
        # both hands is 38, and each one is a joint of its own now.
        bones = [bone for _, bone, _ in retarget.GF_NODES if bone.startswith("skh_")]
        self.assertEqual(len(bones), 38)
        self.assertEqual(len(set(bones)), 38)

    def test_body_joints_keep_their_ids(self):
        # Models converted before the fingers existed address joints by number.
        legacy = ["body", "hip", "left_thigh", "left_knee", "left_ankle",
                  "right_thigh", "right_knee", "right_ankle", "middle",
                  "chest", "neck", "head", "left_clavicle", "left_shoulder",
                  "left_elbow", "left_hand", "right_clavicle",
                  "right_shoulder", "right_elbow", "right_hand"]
        self.assertEqual(retarget.GF_JOINT_ORDER[:20], legacy)
        for i, name in enumerate(legacy):
            self.assertEqual(retarget.JOINT_ID[name], i, name)

    def test_finger_joints_follow_the_body(self):
        self.assertEqual(len(retarget.GF_JOINT_ORDER), 58)
        for name in retarget.GF_JOINT_ORDER[20:]:
            self.assertTrue(any(f in name for f in FINGERS), name)

    def test_finger_ids_follow_the_pes_track_order(self):
        # unit order of pes_human_hand_141203.frig: thumb, index, middle,
        # pinky, ring - keeping it means a pose's channels arrive in order
        expect = []
        for side in ("left", "right"):
            for finger in FINGERS:
                segs = ("mata", "mcp", "pip") if finger == "thumb" else \
                       ("mata", "mcp", "pip", "dip")
                for seg in segs:
                    expect.append("%s_%s_%s" % (side, finger, seg))
        self.assertEqual(retarget.GF_JOINT_ORDER[20:], expect)

    def test_finger_chains_hang_off_the_hands(self):
        for side in ("left", "right"):
            for finger in FINGERS:
                self.assertEqual(retarget.GF_PARENT["%s_%s_mata" % (side, finger)],
                                 "%s_hand" % side)
                self.assertEqual(retarget.GF_PARENT["%s_%s_mcp" % (side, finger)],
                                 "%s_%s_mata" % (side, finger))

    def test_parents_precede_children(self):
        for name in retarget.GF_JOINT_ORDER:
            parent = retarget.GF_PARENT[name]
            if parent is not None:
                self.assertLess(retarget.JOINT_ID[parent], retarget.JOINT_ID[name])

    def test_no_finger_joint_sits_on_the_wrist(self):
        # a mata bone offset of zero would mean the table never got the skl
        world = retarget.gf_world_bind()
        for side in ("left", "right"):
            wrist = world["%s_hand" % side]
            for finger in FINGERS:
                d = math.dist(world["%s_%s_mata" % (side, finger)], wrist)
                self.assertGreater(d, 0.005, "%s %s" % (side, finger))
                self.assertLess(d, 0.09, "%s %s" % (side, finger))

    def test_fingertips_reach_past_the_knuckles(self):
        world = retarget.gf_world_bind()
        for side in ("left", "right"):
            wrist = world["%s_hand" % side]
            for finger in FINGERS:
                tip = "pip" if finger == "thumb" else "dip"
                self.assertGreater(
                    math.dist(world["%s_%s_%s" % (side, finger, tip)], wrist),
                    math.dist(world["%s_%s_mata" % (side, finger)], wrist),
                    "%s %s" % (side, finger))

    def test_hands_are_mirror_images(self):
        world = retarget.gf_world_bind()
        for name in retarget.GF_JOINT_ORDER[20:]:
            if not name.startswith("left_"):
                continue
            left = world[name]
            right = world["right_" + name[len("left_"):]]
            self.assertAlmostEqual(left[0], -right[0], places=3, msg=name)
            self.assertAlmostEqual(left[1], right[1], places=3, msg=name)
            self.assertAlmostEqual(left[2], right[2], places=3, msg=name)


class BoneResolution(unittest.TestCase):
    def test_finger_bones_resolve_to_their_own_joint(self):
        for name, bone, _ in retarget.GF_NODES:
            self.assertEqual(retarget.resolve_bone(bone), name, bone)

    def test_face_bones_still_collapse_onto_the_head(self):
        for bone in ("skf_jaw", "skf_eyelid_up_l", "skf_cheek_r"):
            self.assertEqual(retarget.resolve_bone(bone), "head", bone)

    def test_unknown_finger_bone_still_lands_on_the_hand(self):
        # PES ships glove and cloth variants under the same prefix; anything
        # the table does not name must still ride something sensible
        self.assertEqual(retarget.resolve_bone("skh_nonesuch_l"), "left_hand")
        self.assertEqual(retarget.resolve_bone("skh_nonesuch_r"), "right_hand")

    def test_wrist_helpers_unchanged(self):
        self.assertEqual(retarget.resolve_bone("dsk_wrist_l"), "left_hand")
        self.assertEqual(retarget.resolve_bone("dsk_forearm_r"), "right_elbow")


class BindAgainstPes(unittest.TestCase):
    """The bind table is transcribed from hand_[lr].skl; hold it to the file."""

    TOLERANCE = 1e-4        # the table is written to four decimals

    def _check(self, env, suffix):
        path = os.environ.get(env)
        if not path or not os.path.exists(path):
            self.skipTest("%s not set" % env)
        positions = {b.name: b.position for b in pes_skl.parse_file(path)}
        worst = 0.0
        checked = 0
        for bone, (pos, _) in retarget.PES_BIND.items():
            if not bone.startswith("skh_") or not bone.endswith(suffix):
                continue
            self.assertIn(bone, positions, bone)
            worst = max(worst, math.dist(positions[bone], pos))
            checked += 1
        self.assertEqual(checked, 19)
        self.assertLess(worst, self.TOLERANCE,
                        "worst finger bind error %.6f m" % worst)

    def test_left_hand_matches_the_skl(self):
        self._check("PES_HAND_SKL_L", "_l")

    def test_right_hand_matches_the_skl(self):
        self._check("PES_HAND_SKL_R", "_r")


if __name__ == "__main__":
    unittest.main()

"""Tests for the PES hand-pose import.

PES's body animation carries no finger channels at all - all 4,389 body ganis
are fifteen units / twenty-seven segments, the twenty bones of body_skel.frig,
and body.skl has no skh_* bone in it. The fingers are driven from a separate
one-frame pose library instead: pes_human_hand_141203.frig ("HumanHand", 21
units / 23 tracks, 20 bones = a root plus the nineteen skh_*_l) and 162 ganis
under common/anime/FoxAnim/Hand/Animations, every one of them a single frame.

Run: python3 -m unittest test_hand_poses -v
Set PES_HAND_FRIG and PES_HAND_GANIS to the real files to also check them.
"""

import math
import os
import struct
import unittest

import gani
import hand_poses
import retarget
import strcode


def synth_frig(bone_names, unit_count=None):
    """A .frig whose bone table hashes `bone_names`, in that order."""
    count = len(bone_names)
    units = unit_count if unit_count is not None else count + 1
    header_size = 0x20 + 4 * units
    bone_table = header_size
    blob = bytearray()
    blob += struct.pack("<7I", strcode.strcode32("HumanHand"), 0, 0x66,
                        units, count, 0, bone_table)
    blob += b"\0" * (0x20 - len(blob))
    # unit offsets: unused by rig_bones, but the parser reads seg counts there
    for _ in range(units):
        blob += struct.pack("<I", bone_table)
    blob += struct.pack("<I", count)
    for name in bone_names:
        blob += struct.pack("<II", 0, strcode.strcode32(name))
    # frig.parse reads a 16-byte index row before the bone table
    blob += b"\0" * 16
    return bytes(blob)


def q_axis(axis, degrees):
    half = math.radians(degrees) / 2.0
    length = math.sqrt(sum(c * c for c in axis))
    s = math.sin(half) / length
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


class RigBones(unittest.TestCase):
    NAMES = ["skh_thumb_mata_l", "skh_thumb_mcp_l", "skh_thumb_pip_l",
             "skh_index_mata_l", "skh_index_mcp_l", "skh_index_pip_l",
             "skh_index_dip_l", "skh_middle_mata_l", "skh_middle_mcp_l",
             "skh_middle_pip_l", "skh_middle_dip_l", "skh_pinky_mata_l",
             "skh_pinky_mcp_l", "skh_pinky_pip_l", "skh_pinky_dip_l",
             "skh_ring_mata_l", "skh_ring_mcp_l", "skh_ring_pip_l",
             "skh_ring_dip_l"]

    def test_reads_the_bone_order_out_of_the_rig(self):
        blob = synth_frig(["RIG_ROOT"] + self.NAMES)
        self.assertEqual(hand_poses.rig_bones(blob), self.NAMES)

    def test_rejects_a_rig_that_is_not_a_hand(self):
        blob = synth_frig(["RIG_ROOT", "sk_hand_l", "sk_forearm_l"])
        with self.assertRaises(ValueError):
            hand_poses.rig_bones(blob)

    def test_the_order_is_not_the_skl_order(self):
        # hand_l.skl lists ring before pinky and the rig lists pinky before
        # ring: reading the wrong one puts the ring finger's curl on the pinky
        bones = hand_poses.rig_bones(synth_frig(["RIG_ROOT"] + self.NAMES))
        self.assertLess(bones.index("skh_pinky_mata_l"),
                        bones.index("skh_ring_mata_l"))


class PoseMapping(unittest.TestCase):
    def test_fox_quaternions_map_the_way_the_body_clips_do(self):
        # gani_to_anim.map_quat is the body's change of basis; the fingers
        # have to use the same one or a hand twists against its arm
        import gani_to_anim
        q = (0.1, 0.2, 0.3, math.sqrt(1 - 0.14))
        self.assertEqual(hand_poses.map_quat(q), gani_to_anim.map_quat(q))

    def test_a_pose_names_every_joint_of_both_hands(self):
        fox = {bone: (0.0, 0.0, 0.0, 1.0)
               for bone in RigBones.NAMES}
        pose = hand_poses.to_gf_pose(fox)
        self.assertEqual(set(pose), set(retarget.GF_JOINT_ORDER[20:]))
        self.assertEqual(len(pose), 38)

    def test_the_right_hand_is_the_left_hand_mirrored(self):
        # a curl about the knuckle axis has to close both hands, not open one
        fox = {bone: (0.0, 0.0, 0.0, 1.0) for bone in RigBones.NAMES}
        fox["skh_index_mcp_l"] = q_axis((0.0, 0.0, 1.0), 40.0)
        pose = hand_poses.to_gf_pose(fox)
        left = pose["left_index_mcp"]
        right = pose["right_index_mcp"]
        self.assertAlmostEqual(right[0], left[0], places=6)
        self.assertAlmostEqual(right[1], -left[1], places=6)
        self.assertAlmostEqual(right[2], -left[2], places=6)
        self.assertAlmostEqual(right[3], left[3], places=6)

    def test_mirroring_moves_both_fingertips_the_same_way_inwards(self):
        # the real check on the mirror: forward-kinematic both hands through
        # the bind and compare where the tips end up. The tolerance is PES's
        # own: hand_r.skl is not a bit-exact mirror of hand_l.skl, and the
        # bind table is written to four decimals, so the two hands' fingertips
        # sit up to a tenth of a millimetre apart before any pose is applied.
        fox = {bone: (0.0, 0.0, 0.0, 1.0) for bone in RigBones.NAMES}
        for seg in ("mcp", "pip", "dip"):
            fox["skh_index_%s_l" % seg] = hand_poses.map_quat_inverse(
                q_axis((0.0, 0.0, 1.0), 35.0))
        pose = hand_poses.to_gf_pose(fox)
        left = hand_poses.fingertip("left", "index", pose)
        right = hand_poses.fingertip("right", "index", pose)
        self.assertAlmostEqual(left[0], -right[0], delta=2e-4)
        self.assertAlmostEqual(left[1], right[1], delta=2e-4)
        self.assertAlmostEqual(left[2], right[2], delta=2e-4)

    def test_an_identity_pose_leaves_the_fingers_in_bind(self):
        fox = {bone: (0.0, 0.0, 0.0, 1.0) for bone in RigBones.NAMES}
        pose = hand_poses.to_gf_pose(fox)
        world = retarget.gf_world_bind()
        for side in ("left", "right"):
            tip = hand_poses.fingertip(side, "index", pose)
            bind = world["%s_index_dip" % side]
            self.assertAlmostEqual(math.dist(tip, bind), 0.0, places=6)


class FileFormat(unittest.TestCase):
    def test_round_trips_through_the_text_form(self):
        fox = {bone: (0.0, 0.0, 0.0, 1.0) for bone in RigBones.NAMES}
        fox["skh_thumb_mcp_l"] = q_axis((1.0, 0.0, 0.0), 25.0)
        text = hand_poses.render({"relax": hand_poses.to_gf_pose(fox)})
        self.assertTrue(text.startswith("# gfhandposes 1\n"))
        self.assertIn("pose relax\n", text)
        parsed = hand_poses.parse(text)
        self.assertEqual(list(parsed), ["relax"])
        self.assertEqual(len(parsed["relax"]), 38)
        for node, q in hand_poses.to_gf_pose(fox).items():
            for a, b in zip(parsed["relax"][node], q):
                self.assertAlmostEqual(a, b, places=5, msg=node)

    def test_poses_come_out_in_a_stable_order(self):
        fox = {bone: (0.0, 0.0, 0.0, 1.0) for bone in RigBones.NAMES}
        poses = {"zulu": hand_poses.to_gf_pose(fox),
                 "alpha": hand_poses.to_gf_pose(fox)}
        text = hand_poses.render(poses)
        self.assertLess(text.index("pose alpha"), text.index("pose zulu"))


class RealPesData(unittest.TestCase):
    """Against Konami's own files, when they are on hand."""

    def _load(self):
        frig_path = os.environ.get("PES_HAND_FRIG")
        gani_dir = os.environ.get("PES_HAND_GANIS")
        if not frig_path or not os.path.exists(frig_path):
            self.skipTest("PES_HAND_FRIG not set")
        if not gani_dir or not os.path.isdir(gani_dir):
            self.skipTest("PES_HAND_GANIS not set")
        return frig_path, gani_dir

    def test_the_rig_is_nineteen_bones(self):
        frig_path, _ = self._load()
        bones = hand_poses.rig_bones(open(frig_path, "rb").read())
        self.assertEqual(len(bones), 19)
        for bone in bones:
            self.assertIn(bone, retarget.PES_BIND, bone)

    def test_every_hand_gani_is_a_single_frame_pose(self):
        frig_path, gani_dir = self._load()
        bones = hand_poses.rig_bones(open(frig_path, "rb").read())
        count = 0
        for name in sorted(os.listdir(gani_dir)):
            if not name.endswith(".gani"):
                continue
            g = gani.parse(open(os.path.join(gani_dir, name), "rb").read())
            self.assertEqual(g.frame_count, 1, name)
            self.assertEqual(len(g.units), len(bones) + 2, name)
            count += 1
        self.assertGreater(count, 100)

    def test_the_grip_pose_closes_the_hand_and_the_open_one_does_not(self):
        frig_path, gani_dir = self._load()
        bones = hand_poses.rig_bones(open(frig_path, "rb").read())
        world = retarget.gf_world_bind()
        bind = math.dist(world["left_index_dip"], world["left_hand"])
        reach = {}
        for name in ("open_full", "normal", "relax", "nigiri"):
            path = os.path.join(gani_dir, name + ".gani")
            if not os.path.exists(path):
                self.skipTest("%s.gani missing" % name)
            pose = hand_poses.to_gf_pose(
                hand_poses.pose_quats(open(path, "rb").read(), bones))
            reach[name] = math.dist(hand_poses.fingertip("left", "index", pose),
                                    world["left_hand"])
        # open_full is PES's bind-ish spread; the curl deepens through relax to
        # the fist, and every one of them brings the tip nearer than the bind
        self.assertGreater(reach["open_full"], reach["normal"])
        self.assertGreater(reach["normal"], reach["relax"])
        self.assertGreater(reach["relax"], reach["nigiri"])
        self.assertLess(reach["nigiri"], bind * 0.95)

    def test_pointing_leaves_the_index_finger_straight(self):
        frig_path, gani_dir = self._load()
        path = os.path.join(gani_dir, "pointing.gani")
        if not os.path.exists(path):
            self.skipTest("pointing.gani missing")
        bones = hand_poses.rig_bones(open(frig_path, "rb").read())
        fox = hand_poses.pose_quats(open(path, "rb").read(), bones)

        def bend(bone):
            w = min(1.0, abs(fox[bone][3]))
            return math.degrees(2.0 * math.acos(w))

        # the index finger stays out while the others fold
        self.assertLess(bend("skh_index_pip_l"), 15.0)
        self.assertGreater(bend("skh_middle_pip_l"), 60.0)
        self.assertGreater(bend("skh_ring_pip_l"), 60.0)




class EngineCoverage(unittest.TestCase):
    """The exporter warns when a pose the engine picks is not in the export.

    ChooseHandPose (handrig.cpp) returns seven names; an export made with --poses
    that omits one silently degrades that state to bind. The names are pinned here
    so renaming one in the engine without the exporter noticing fails a test.
    """

    def test_all_seven_present_is_no_warning(self):
        poses = {name: {} for name in hand_poses.ENGINE_POSES}
        self.assertEqual(hand_poses.missing_engine_poses(poses), [])

    def test_a_missing_selected_pose_is_named(self):
        poses = {name: {} for name in hand_poses.ENGINE_POSES if name != "kp_hold"}
        self.assertEqual(hand_poses.missing_engine_poses(poses), ["kp_hold"])

    def test_the_seven_are_the_engines_seven(self):
        # From handrig.cpp ChooseHandPose: one per e_HandPose state.
        self.assertEqual(sorted(hand_poses.ENGINE_POSES),
                         sorted(["normal", "relax", "move_nigiri", "clap", "taore",
                                 "open_full_ball", "kp_hold"]))


if __name__ == "__main__":
    unittest.main()

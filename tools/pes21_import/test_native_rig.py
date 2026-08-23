"""Tests for the native PES rig adoption: pes_skl parsing, the 20-node
skeleton tables in retarget.py, and helper-bone collapse resolution.

Run: python3 -m unittest test_native_rig -v
Set PES_BODY_SKL=/path/to/body.skl to also validate against the real
skeleton file (proprietary, stays outside the repo).
"""

import math
import os
import struct
import unittest

import pes_skl
import retarget


def synth_skl(bones):
    """[(name, parent, pos)] -> a valid .skl blob (identity rotations)."""
    records = b""
    names = b""
    name_offsets = []
    base = 12 + 56 * len(bones)
    for name, _, _ in bones:
        name_offsets.append(base + len(names))
        names += name.encode() + b"\0"
    for (name, parent, pos), name_off in zip(bones, name_offsets):
        matrix = [1.0, 0.0, 0.0, pos[0],
                  0.0, 1.0, 0.0, pos[1],
                  0.0, 0.0, 1.0, pos[2]]
        records += struct.pack("<Ii12f", name_off, parent, *matrix)
    return struct.pack("<3I", 12, len(bones), 56) + records + names


class SklParser(unittest.TestCase):
    def test_roundtrip(self):
        blob = synth_skl([("sk_belly", -1, (0.0, 1.0961, 0.0)),
                          ("sk_chest", 0, (0.0, 1.2628, 0.0138))])
        bones = pes_skl.parse(blob)
        self.assertEqual([b.name for b in bones], ["sk_belly", "sk_chest"])
        self.assertEqual(bones[0].parent, -1)
        self.assertEqual(bones[1].parent, 0)
        self.assertAlmostEqual(bones[1].position[1], 1.2628, places=4)

    def test_rejects_unknown_record_size(self):
        blob = struct.pack("<3I", 12, 0, 40)
        with self.assertRaises(ValueError):
            pes_skl.parse(blob)


class SkeletonTables(unittest.TestCase):
    def test_body_joints_are_the_first_twenty(self):
        # the fingers came later and were appended; the body's IDs may not move
        self.assertEqual(len(retarget.GF_BODY_NODES), 20)
        self.assertEqual(retarget.GF_JOINT_ORDER[:20],
                         [name for name, _, _ in retarget.GF_BODY_NODES])

    def test_body_joints_stay_inside_the_vertex_colour_range(self):
        # a vertex colour channel is jointID*10 + weight*9 and must fit a
        # byte, so that encoding reaches joint 25 and no further. That ceiling
        # is why the fingers need the sidecar weight file (skinweights.cpp).
        self.assertLessEqual(retarget.JOINT_ID["right_hand"] * 10 + 9, 255)
        self.assertGreater(max(retarget.JOINT_ID.values()) * 10 + 9, 255)

    def test_legacy_names_all_present(self):
        legacy = ["body", "middle", "neck", "head",
                  "left_shoulder", "left_elbow", "left_hand",
                  "right_shoulder", "right_elbow", "right_hand",
                  "left_thigh", "left_knee", "left_ankle",
                  "right_thigh", "right_knee", "right_ankle"]
        for name in legacy:
            self.assertIn(name, retarget.JOINT_ID, name)

    def test_parents_precede_children(self):
        # joint IDs are DFS order, so a parent's ID is always lower
        for name in retarget.GF_JOINT_ORDER:
            parent = retarget.GF_PARENT[name]
            if parent is not None:
                self.assertLess(retarget.JOINT_ID[parent],
                                retarget.JOINT_ID[name])

    def test_world_bind_matches_pes(self):
        world = retarget.gf_world_bind()
        for name, bone, _ in retarget.GF_NODES:
            expect = retarget.fox_to_gf(retarget.PES_BIND[bone][0])
            for a, b in zip(world[name], expect):
                self.assertAlmostEqual(a, b, places=5,
                                       msg="%s vs %s" % (name, bone))

    def test_body_height(self):
        self.assertAlmostEqual(retarget.GF_BODY_HEIGHT, 0.9921, places=4)

    def test_limb_offsets_point_down(self):
        # sanity: knee and elbow offsets descend (negative Z in GF coords)
        for node in ("left_knee", "right_knee", "left_ankle", "right_ankle",
                     "left_elbow", "right_elbow", "left_hand", "right_hand"):
            offset, _ = retarget.GF_BIND[node]
            self.assertLess(offset[2], 0.0, node)


class HelperCollapse(unittest.TestCase):
    def test_animated_bones_resolve_to_themselves(self):
        for name, bone, _ in retarget.GF_NODES:
            self.assertEqual(retarget.resolve_bone(bone), name)

    def test_curated_helpers(self):
        cases = {
            "dsk_clavicle_l": "left_clavicle",
            "dsk_collar_r": "chest",
            "dsk_scm": "neck",
            "dsk_wrist_l": "left_hand",
            "dsk_toe_r": "right_ankle",
            "dsk_leg_l": "left_knee",
            "dsk_upperarm_skin_t_r": "right_shoulder",
            "dsk_hem_ba_fake_l": "left_thigh",
            "tip_dsk_sleeve_03_r": "right_shoulder",
            "pos_arm_target_05_l": "left_shoulder",
            "skh_index_dip_l": "left_index_dip",
            "skh_thumb_mcp_r": "right_thumb_mcp",
            "skf_jaw": "head",
            "dsk_earlobe_r": "head",
        }
        for bone, expect in cases.items():
            self.assertEqual(retarget.resolve_bone(bone), expect, bone)

    def test_position_fallback(self):
        # unknown bone near the left foot binds to the ankle
        got = retarget.resolve_bone("prop_shinpad_l",
                                    {"prop_shinpad_l": (0.19, 0.2, 0.05)})
        self.assertEqual(got, "left_ankle")

    def test_real_body_skl_fully_resolves(self):
        path = os.environ.get("PES_BODY_SKL")
        if not path or not os.path.exists(path):
            self.skipTest("PES_BODY_SKL not set")
        bones = pes_skl.parse_file(path)
        positions = {b.name: b.position for b in bones}
        unresolved = [b.name for b in bones
                      if retarget.resolve_bone(b.name, positions) is None]
        self.assertEqual(unresolved, [])

    def test_real_body_skl_agrees_with_pes_bind(self):
        path = os.environ.get("PES_BODY_SKL")
        if not path or not os.path.exists(path):
            self.skipTest("PES_BODY_SKL not set")
        positions = {b.name: b.position for b in pes_skl.parse_file(path)}
        for bone, (pos, _) in retarget.PES_BIND.items():
            if bone == "motion":        # rig-only node, not in the skl
                continue
            d = math.dist(positions[bone], pos)
            self.assertLess(d, 0.002, "%s drifted %.4f" % (bone, d))


class StockAnimConversion(unittest.TestCase):
    """convert_stock_anims' conjugation must preserve world limb directions."""

    SEGMENTS = [("left_shoulder", "left_elbow"), ("left_elbow", "left_hand"),
                ("right_shoulder", "right_elbow"), ("right_elbow", "right_hand"),
                ("left_thigh", "left_knee"), ("left_knee", "left_ankle"),
                ("right_thigh", "right_knee"), ("right_knee", "right_ankle")]
    STOCK_NODES = ["body", "middle", "neck",
                   "left_shoulder", "left_elbow", "right_shoulder",
                   "right_elbow", "left_thigh", "left_knee", "left_ankle",
                   "right_thigh", "right_knee", "right_ankle"]

    @staticmethod
    def _fk(bind, order, locals_):
        from migrate_to_native_rig import q_mul, q_rot
        pos, rot = {}, {}
        for name in order:
            off, parent = bind[name]
            q = locals_.get(name, (0.0, 0.0, 0.0, 1.0))
            if parent is None:
                pos[name], rot[name] = off, q
            else:
                pos[name] = tuple(p + o for p, o in
                                  zip(pos[parent], q_rot(rot[parent], off)))
                rot[name] = q_mul(rot[parent], q)
        return pos

    def test_conjugation_preserves_world_limb_directions(self):
        import random
        import convert_stock_anims as CSA
        from migrate_to_native_rig import OLD_BIND
        conv = CSA.conversions()
        random.seed(7)

        def random_quat():
            ax = [random.uniform(-1, 1) for _ in range(3)]
            l = math.sqrt(sum(c * c for c in ax))
            angle = random.uniform(-1.2, 1.2)
            s = math.sin(angle / 2)
            return tuple(c / l * s for c in ax) + (math.cos(angle / 2),)

        worst = 0.0
        for _ in range(50):
            old_locals = {n: random_quat() for n in self.STOCK_NODES}
            new_locals = {}
            for n, q in old_locals.items():
                pre, post = conv.get(n, (None, None))
                new_locals[n] = CSA._convert_quat(q, pre, post) if n in conv else q
            for n, (pre, post) in conv.items():
                if n not in new_locals:
                    new_locals[n] = CSA._convert_quat((0, 0, 0, 1), pre, post)
            p_old = self._fk(OLD_BIND, list(OLD_BIND.keys()), old_locals)
            p_new = self._fk(retarget.GF_BIND, retarget.GF_JOINT_ORDER, new_locals)
            for a, b in self.SEGMENTS:
                d_old = tuple(x - y for x, y in zip(p_old[b], p_old[a]))
                d_new = tuple(x - y for x, y in zip(p_new[b], p_new[a]))
                lo = math.sqrt(sum(c * c for c in d_old))
                ln = math.sqrt(sum(c * c for c in d_new))
                dot = sum(x * y for x, y in zip(d_old, d_new)) / (lo * ln)
                worst = max(worst, math.degrees(math.acos(max(-1.0, min(1.0, dot)))))
        self.assertLess(worst, 0.01)

    def test_convert_text_inserts_hand_lines(self):
        import convert_stock_anims as CSA
        text = ("player,0,0,0,0,10,0,0,0\n"
                "body,0,0,0,0,1,10,0,0,0,1\n"
                "left_elbow,0,0,0,0,1,10,0,0,0,1\n"
                "<type>\n\tmovement\n</type>\n")
        out = CSA.convert_text(text)
        self.assertIn("left_hand,", out)
        # metadata tail untouched
        self.assertIn("<type>\n\tmovement\n</type>", out)
        # player root untouched
        self.assertIn("player,0,0,0,0,10,0,0,0", out)


if __name__ == "__main__":
    unittest.main()

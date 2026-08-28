"""Tests for installer-side anim re-bucketing: root velocity is not enough
to classify a 'sprinting' receiver; foot plant cadence is the right signal.

A defensive trap from a sprinting forward shows ~0 root motion in the
player track (the body stays roughly where it was), but a foot plant every
~8-12 frames (leg stepping in place) tells the selector the receiver is
moving. Without that, all 340 PES trap clips land in idle/ and the cheat
check rejects every one for a sprint receiver.
"""
import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import anim_metrics as am


class _StubAnim:
    """Minimal stand-in for am.parse_anim() output to drive a quantizer.

    Both forms of root_path are accepted: (frame, x, y) and
    (frame, x, y, z). Each entry is normalised to the 4-tuple shape that
    am._edge_velocity requires.
    """
    def __init__(self, root_path, foot_step_frames=None, last_frame=24):
        norm = []
        for k in root_path:
            if len(k) == 4:
                norm.append(k)
            else:
                f, x, y = k
                norm.append((f, x, y, 0.0))
        # Mirror the real parser: append a final keyframe at last_frame if
        # none exists, repeating the last position (so _edge_velocity finds
        # a non-zero frame gap to compute speed over).
        if norm[-1][0] != last_frame:
            norm.append((last_frame, norm[-1][1], norm[-1][2], norm[-1][3]))
        self.player = norm
        self.last_frame = last_frame
        self.foot_plants = foot_step_frames or []
        self.nodes = {}


class Rebucket(unittest.TestCase):
    POOL = os.path.join("..", "..", "data", "imports", "pes21", "animations")

    def test_idle_receiver_still_buckets_idle(self):
        # The helper takes a foot_source callable so the test can pass an
        # explicit empty list, matching the "no foot plant" contract.
        anim = _StubAnim([(0, 0, 0), (24, 0, 0)], foot_step_frames=[])
        self.assertEqual(am.receiver_velocity_bucket(anim, foot_source=lambda: []), 0.0)

    def test_moving_receiver_via_root_motion_still_buckets_walk(self):
        # A 1.6 m/s jog over 24 frames (0.6s) is plainly walk; root motion
        # alone already classifies correctly.
        anim = _StubAnim([(0, 0, 0), (24, 0, 1.2)])  # 1.2m/0.24s = 5.0 m/s -> walk bucket (5.0)
        self.assertEqual(am.receiver_velocity_bucket(anim), 5.0)

    def test_moving_receiver_via_foot_plants_bumps_out_of_idle(self):
        # The fix: a clip with ~0 root motion but a leg stepping every
        # ~6-12 frames is a moving receiver and should NOT land in idle.
        # Root motion is 0; foot cadence 0,8,16,24 -> roughly stepping in
        # place. Without the fix the bucket is 0.0; with the fix it
        # crosses the walk threshold.
        # 80 frames = 0.8s; 4 plants spaced 0.2s apart = 3.75 plants/sec.
        # With 0.7 m stride: 2.6 m/s -> walk bucket (5.0). Without the
        # fix the helper returns idle (root motion is zero).
        anim = _StubAnim(
            [(0, 0, 0), (80, 0, 0)],
            foot_step_frames=[0, 20, 40, 60, 80],
            last_frame=80,
        )
        self.assertGreater(am.receiver_velocity_bucket(anim, foot_source=lambda: anim.foot_plants), 0.0)

    def test_real_pool_trap_with_foot_cadence_no_longer_all_idle(self):
        if not os.path.isdir(self.POOL):
            self.skipTest("import pool not present")
        # The pool holds no clips at its root - PES ships them under
        # body_anime_fileN/ - so this has to descend. It used to break out of
        # the walk after the first directory, which meant it examined a
        # directory of directories, found no .anim at all and failed on the
        # empty search rather than on any measurement.
        examined = 0
        for root, _, files in os.walk(self.POOL):
            for f in files:
                if "trap" in f and f.endswith(".anim"):
                    anim = am.parse_anim(os.path.join(root, f))
                    # Use the real foot_plants() function as the cadence source.
                    bucket = am.receiver_velocity_bucket(anim, foot_source=am.foot_plants)
                    examined += 1
                    if bucket > 0.0:
                        return
        if not examined:
            self.skipTest("import pool holds no trap clips")
        self.fail("all %d pool trap clips bucketed as idle" % examined)


if __name__ == "__main__":
    unittest.main()

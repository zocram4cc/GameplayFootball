"""A receiver clip is bucketed by its root motion and nothing else.

GF's root track is the locomotion. A foot-plant cadence used to bump
still-root clips into walk/dribble; the engine then handed them to a moving
receiver, who stopped dead and strode in place (FILTHY MONKEYS, kickoff,
02-09-26 showcase). Fails if the cadence bump is reintroduced.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import anim_metrics as am


class _StubAnim:
    def __init__(self, keys, last_frame=80):
        self.player = keys
        self.last_frame = last_frame
        self.nodes = {}


class Rebucket(unittest.TestCase):
    def test_still_root_is_idle_whatever_the_legs_do(self):
        # a foot-plant every 8 frames, root never moves: standing receiver
        stepping = _StubAnim([(0, 0, 0, 0), (80, 0, 0, 0)])
        stepping.foot_plants = list(range(0, 81, 8))
        self.assertEqual(am.receiver_velocity_bucket(stepping), 0.0)

    def test_root_travel_buckets_walk(self):
        # 1.2 m over 0.24 s = 5 m/s -> walk
        self.assertEqual(am.receiver_velocity_bucket(
            _StubAnim([(0, 0, 0, 0), (24, 0, 1.2, 0)])), 5.0)


if __name__ == "__main__":
    unittest.main()

"""Tests for keeping the travel in an imported entrance walkout.

PES walks both squads out of the tunnel and onto the pitch; the VGL26 reference
(docs/VGL26_REFERENCE.md, s036-s048) spends about ten seconds on it. Our imported
entrances had everybody marching on the spot, and the measurement says why: in
ent_009's common pack every walk slot's root track advances smoothly - a median
step of 7 mm - for about 2.4 m and then jumps back to the start in a single 2.4 m
step, netting 3 mm of travel over the whole cycle.

That is `bake_track` sampling the clip's root at `t % frame_count`. The clip does
carry forward motion; wrapping the time throws it away and leaves a closed loop.
A looping walk has to accumulate what each cycle covered, so cycle two starts
where cycle one finished.

Run: python3 -m unittest test_entrance_walk -v
"""

import unittest

import entrance_pl


def forward_walk(frame_count, metres_per_cycle):
    """A sampler that walks straight down +x, plus a little sway."""
    def sample(t):
        # a small sway so the track is not a perfectly straight line
        sway = 0.02 * ((t % 10) - 5) / 5.0
        return (metres_per_cycle * (t / frame_count), sway, 0.0)
    return sample


def on_the_spot(frame_count):
    """A sampler that returns to where it started: a genuine in-place clip."""
    import math

    def sample(t):
        angle = 2.0 * math.pi * (t / frame_count)
        return (0.1 * math.sin(angle), 0.1 * (math.cos(angle) - 1.0), 0.0)
    return sample


class UnwrappedRootTest(unittest.TestCase):
    def test_inside_the_first_cycle_nothing_changes(self):
        sample = forward_walk(100, 2.4)
        for t in (0.0, 10.0, 55.5, 99.0):
            got = entrance_pl.unwrapped_root(sample, 100, t)
            self.assertAlmostEqual(got[0], sample(t)[0], places=5)
            self.assertAlmostEqual(got[1], sample(t)[1], places=5)

    def test_the_second_cycle_starts_where_the_first_finished(self):
        sample = forward_walk(100, 2.4)
        start = entrance_pl.unwrapped_root(sample, 100, 0.0)
        after = entrance_pl.unwrapped_root(sample, 100, 100.0)
        self.assertAlmostEqual(after[0] - start[0], 2.4, places=1)

    def test_travel_keeps_accumulating_over_many_cycles(self):
        sample = forward_walk(100, 2.4)
        previous = -1e9
        for cycle in range(6):
            x = entrance_pl.unwrapped_root(sample, 100, cycle * 100.0)[0]
            self.assertGreater(x, previous, "cycle %d went backwards" % cycle)
            previous = x
        # six cycles of a 2.4 m stride is about fourteen metres, which is the
        # difference between standing in the tunnel and reaching the pitch
        self.assertGreater(entrance_pl.unwrapped_root(sample, 100, 600.0)[0], 13.0)

    def test_there_is_no_jump_where_the_cycles_meet(self):
        # The bug was visible as one huge step in an otherwise smooth track.
        sample = forward_walk(100, 2.4)
        before = entrance_pl.unwrapped_root(sample, 100, 99.9)
        after = entrance_pl.unwrapped_root(sample, 100, 100.1)
        self.assertLess(abs(after[0] - before[0]), 0.2)

    def test_a_clip_that_really_stays_put_does_not_drift(self):
        # Only clips that move should move; a standing actor must not slide.
        sample = on_the_spot(100)
        start = entrance_pl.unwrapped_root(sample, 100, 0.0)
        later = entrance_pl.unwrapped_root(sample, 100, 500.0)
        self.assertAlmostEqual(later[0], start[0], places=2)
        self.assertAlmostEqual(later[1], start[1], places=2)

    def test_yaw_comes_straight_from_the_clip(self):
        def turning(t):
            return (0.0, 0.0, 0.5 * (t / 100.0))
        self.assertAlmostEqual(entrance_pl.unwrapped_root(turning, 100, 50.0)[2], 0.25, places=5)

    def test_a_degenerate_clip_length_does_not_divide_by_zero(self):
        sample = forward_walk(100, 2.4)
        entrance_pl.unwrapped_root(sample, 0, 10.0)
        entrance_pl.unwrapped_root(sample, 1, 10.0)


if __name__ == "__main__":
    unittest.main()


class ATurningClipKeepsTurningWhenItLoops(unittest.TestCase):
    """ent_009's "idle_walk_turn_right" actor walked 38 m in a straight line out
    through the back of the tunnel, because only the cycle's translation was
    carried over, in the clip's starting frame. The carry-over is the whole
    rigid motion."""

    def walk_and_turn(self, frame_count, distance, turn):
        import math

        def sample(t):
            # straight along +x while the yaw sweeps through `turn`
            f = t / frame_count
            return (distance * f, 0.0, turn * f)
        return sample

    def test_four_right_angles_close_a_square(self):
        import math
        sample = self.walk_and_turn(100, 1.0, math.pi / 2.0)
        x, z, yaw = entrance_pl.unwrapped_root(sample, 100, 400.0)
        self.assertAlmostEqual(x, 0.0, places=5)
        self.assertAlmostEqual(z, 0.0, places=5)
        self.assertAlmostEqual(yaw, 2.0 * math.pi, places=5)

    def test_a_straight_walk_is_unchanged_by_the_generalisation(self):
        sample = forward_walk(100, 2.4)
        got = entrance_pl.unwrapped_root(sample, 100, 300.0)
        self.assertAlmostEqual(got[0], forward_walk(100, 2.4)(0.0)[0] + 3 * 2.4, places=1)
        self.assertAlmostEqual(got[2], 0.0, places=6)


class AClipLoopsOnlyIfItEndsFacingWhereItBegan(unittest.TestCase):
    """The owner saw actors freeze mid-stride six seconds into a thirty-second
    walk-on, and Mario snap through a right angle every twelve: walks were being
    played once and turn clips looped. The body's heading at the wrap decides."""

    def fake_gani(self, turn_deg):
        import math
        import gani_to_anim

        class Sampler:
            def __init__(self, q):
                self._q = q
            def quat(self, t):
                return self._q(t)
            def vec(self, t):
                return (0.0, 0.0, 0.0)

        class G:
            frame_count = 100
        def mot(t):
            return gani_to_anim.q_axis_angle((0.0, 1.0, 0.0), math.radians(turn_deg) * t / 99.0)
        identity = Sampler(lambda t: (0.0, 0.0, 0.0, 1.0))
        return G(), identity, Sampler(mot)

    def test_a_walk_cycle_loops(self):
        import entrance_pl, gani_to_anim
        g, identity, mot = self.fake_gani(5.0)
        real = gani_to_anim.build_samplers
        gani_to_anim.build_samplers = lambda _g: ({}, identity, identity, mot, identity)
        try:
            self.assertTrue(entrance_pl.clip_is_cycle(g))
        finally:
            gani_to_anim.build_samplers = real

    def test_a_turn_plays_once(self):
        import entrance_pl, gani_to_anim
        g, identity, mot = self.fake_gani(70.0)
        real = gani_to_anim.build_samplers
        gani_to_anim.build_samplers = lambda _g: ({}, identity, identity, mot, identity)
        try:
            self.assertFalse(entrance_pl.clip_is_cycle(g))
        finally:
            gani_to_anim.build_samplers = real

"""Tests for seating PES's own crowd in an imported stadium.

The stands are in each pack's audi/audiarea.bin - the same file crowd_gen.py reads
for its flat outlines - as sloped quads with the row spacing the game itself uses
(1.9 m in st060, 2.1 in st002, 0.7-0.9 in st011). PES fills them with one
spectator model placed at every seat: au_Low.fmdl is 672 vertices, au00..au16_parts
the variants, each with a mouthOpen version for when the crowd is singing.

Counted from those files: st041 has about 13,800 seats, st011 11,900, st060 5,100.
Merged into static geometry that is over a million vertices in a text ASE, so the
seats are written as a placement list instead and the engine draws one mesh many
times (utils/instancelist.hpp).

Run: python3 -m unittest test_stadium_crowd -v
"""

import math
import unittest

import stadium_crowd


# a stand 20 m wide and 10 m of slope, its front edge along y = -40, rising away
# from the pitch. In Fox coordinates, which is what audiarea.bin holds: x across,
# y up, z the axis the engine calls -y.
STAND = [(-10.0, 0.0, 40.0), (-10.0, 6.0, 50.0), (10.0, 6.0, 50.0), (10.0, 0.0, 40.0)]


class Seats(unittest.TestCase):
    def setUp(self):
        self.seats = stadium_crowd.seats_for_stand(STAND, row_step=2.0, seat_step=0.5)

    def test_a_stand_of_that_size_seats_about_that_many(self):
        # 20 m of row at half a metre each is 40 seats, and 11.7 m of slope in 2 m
        # rows is six rows of them
        self.assertEqual(len(self.seats), 240)

    def test_every_seat_is_in_the_stand_not_on_the_pitch(self):
        for x, y, _z, _yaw in self.seats:
            self.assertLessEqual(y, -39.0)
            self.assertLessEqual(abs(x), 10.5)

    def test_the_back_rows_are_higher_than_the_front(self):
        front = min(self.seats, key=lambda s: abs(s[1] + 40.0))
        back = max(self.seats, key=lambda s: abs(s[1] + 40.0))
        self.assertGreater(back[2], front[2])

    def test_they_all_face_the_pitch(self):
        for x, y, _z, yaw in self.seats:
            facing = (math.sin(yaw), -math.cos(yaw))
            towards = (-x, -y)
            length = math.hypot(*towards) or 1.0
            dot = facing[0] * towards[0] / length + facing[1] * towards[1] / length
            self.assertGreater(dot, 0.9, "a seat at %.1f, %.1f faces away" % (x, y))

    def test_an_empty_stand_seats_nobody(self):
        flat = [(0.0, 0.0, 0.0)] * 4
        self.assertEqual(stadium_crowd.seats_for_stand(flat), [])

    def test_the_layout_is_the_same_every_run(self):
        again = stadium_crowd.seats_for_stand(STAND, row_step=2.0, seat_step=0.5)
        self.assertEqual(self.seats, again)


class SharingOutTheVariants(unittest.TestCase):
    """PES ships seventeen spectators; a stand should not be seventeen clones."""

    def test_seats_are_dealt_between_the_models(self):
        seats = [(float(i), 0.0, 0.0, 0.0) for i in range(10)]
        shares = stadium_crowd.share_out(seats, 3)
        self.assertEqual(len(shares), 3)
        self.assertEqual(sum(len(s) for s in shares), 10)

    def test_nobody_is_dealt_twice(self):
        seats = [(float(i), 0.0, 0.0, 0.0) for i in range(10)]
        shares = stadium_crowd.share_out(seats, 3)
        flat = [s for share in shares for s in share]
        self.assertEqual(len(set(flat)), 10)

    def test_neighbours_are_not_the_same_model(self):
        # dealt round robin, so a row alternates rather than running in blocks
        seats = [(float(i), 0.0, 0.0, 0.0) for i in range(6)]
        shares = stadium_crowd.share_out(seats, 2)
        self.assertEqual(shares[0][0][0], 0.0)
        self.assertEqual(shares[1][0][0], 1.0)

    def test_one_model_takes_them_all(self):
        seats = [(float(i), 0.0, 0.0, 0.0) for i in range(5)]
        self.assertEqual(len(stadium_crowd.share_out(seats, 1)[0]), 5)

    def test_no_models_seats_nobody(self):
        self.assertEqual(stadium_crowd.share_out([(0.0, 0.0, 0.0, 0.0)], 0), [])


class TheCapOnHowManyAreDrawn(unittest.TestCase):
    """A stadium with 14,000 seats is thinned rather than dropped or drawn whole."""

    def test_under_the_cap_everyone_is_seated(self):
        seats = [(float(i), 0.0, 0.0, 0.0) for i in range(100)]
        self.assertEqual(len(stadium_crowd.thin_to(seats, 200)), 100)

    def test_over_it_the_crowd_is_thinned_evenly(self):
        seats = [(float(i), 0.0, 0.0, 0.0) for i in range(1000)]
        kept = stadium_crowd.thin_to(seats, 250)
        self.assertEqual(len(kept), 250)
        # spread over the whole stand rather than a quarter of it left full
        self.assertLess(kept[0][0], 10.0)
        self.assertGreater(kept[-1][0], 990.0)

    def test_no_cap_keeps_everyone(self):
        seats = [(float(i), 0.0, 0.0, 0.0) for i in range(1000)]
        self.assertEqual(len(stadium_crowd.thin_to(seats, 0)), 1000)


if __name__ == "__main__":
    unittest.main()

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


class DressingThemFromPesPalette(unittest.TestCase):
    """PES's spectators carry no texture of their own; they index a palette.

    The models declare no material texture at all - the game binds one at runtime -
    and what sits beside them in Asset/model/bg/common/audi/sourceimages is a
    colour palette: au_h_col_bsm_rgba32 and au_l_col_bsm_rgba32, each 32 x 128 of
    small swatches, plus au_h_parts_bsm with the scarves and banners. So the
    spectator's own UVs pick a colour out of the palette, and binding it is how a
    crowd comes out in clothes rather than white.
    """

    def test_the_low_detail_crowd_reads_the_low_detail_palette(self):
        self.assertEqual(stadium_crowd.palette_for("au_Low.fmdl"), "au_l_col_bsm_rgba32")
        self.assertEqual(stadium_crowd.palette_for("/x/au_Low_parts.fmdl"), "au_l_col_bsm_rgba32")

    def test_the_others_read_the_high_detail_one(self):
        self.assertEqual(stadium_crowd.palette_for("au00_mouthOpen_parts.fmdl"),
                         "au_h_col_bsm_rgba32")
        self.assertEqual(stadium_crowd.palette_for("au14_parts.fmdl"), "au_h_col_bsm_rgba32")

    def test_each_variant_reads_a_different_band_of_it(self):
        # otherwise every copy of one model is dressed identically
        first = stadium_crowd.palette_offset(0, 4)
        second = stadium_crowd.palette_offset(1, 4)
        self.assertNotEqual(first, second)

    def test_the_offsets_stay_inside_the_palette(self):
        for i in range(6):
            _du, dv = stadium_crowd.palette_offset(i, 6)
            self.assertGreaterEqual(dv, 0.0)
            self.assertLess(dv, 1.0)

    def test_one_variant_is_not_shifted_at_all(self):
        self.assertEqual(stadium_crowd.palette_offset(0, 1), (0.0, 0.0))


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


class FlagsInTheCrowd(unittest.TestCase):
    """PES's stand flags are props held up among the spectators.

    mob_prop_teamflag_home01..05 and away01 are 313-vertex flags, 2.8 m tall, and
    dt19_x64.cpk animates them (common/mob/prop/standsFlag). Static, they belong
    scattered through the seats - one every so often, not one per seat.
    """

    def test_a_flag_every_so_often(self):
        seats = [(float(i), 0.0, 0.0, 0.0) for i in range(300)]
        places = stadium_crowd.flag_places(seats, every=60)
        self.assertEqual(len(places), 5)

    def test_they_stand_where_a_spectator_does(self):
        seats = [(1.0, 2.0, 3.0, 0.5), (9.0, 9.0, 9.0, 9.0)]
        places = stadium_crowd.flag_places(seats, every=1)
        self.assertEqual(places[0], seats[0])

    def test_a_small_crowd_still_gets_one(self):
        seats = [(0.0, 0.0, 0.0, 0.0)] * 10
        self.assertEqual(len(stadium_crowd.flag_places(seats, every=100)), 1)

    def test_no_seats_no_flags(self):
        self.assertEqual(stadium_crowd.flag_places([], every=10), [])

    def test_they_are_spread_through_the_crowd_not_bunched(self):
        seats = [(float(i), 0.0, 0.0, 0.0) for i in range(300)]
        places = stadium_crowd.flag_places(seats, every=60)
        gaps = [places[i + 1][0] - places[i][0] for i in range(len(places) - 1)]
        self.assertTrue(all(g == 60.0 for g in gaps), gaps)


class TheSeatsThemselves(unittest.TestCase):
    """PES ships the seat as well as the spectator.

    audi_seat_model.fpk holds chair.fmdl - 566 vertices, half a metre square and
    0.84 m tall - and a deck of them is what makes a stand read as a stand rather
    than a slope with dots on it. One under every spectator.
    """

    def test_a_seat_goes_under_every_spectator(self):
        seats = [(1.0, 2.0, 3.0, 0.5), (4.0, 5.0, 6.0, 1.5)]
        self.assertEqual(stadium_crowd.seat_places(seats), seats)

    def test_no_spectators_no_seats(self):
        self.assertEqual(stadium_crowd.seat_places([]), [])


if __name__ == "__main__":
    unittest.main()


class TheStandFlagsBadge(unittest.TestCase):
    """Whose badge the crowd's flags fly.

    PES's stand flags (mob_prop_teamflag_home01..05, away01) carry a texture called
    sys_zero_bsm, which is not a zero at all: it is a per-model placeholder PES
    swaps at run time, and each model ships a different picture under that one
    filename - the flag bearers' is the flag of the United States, the tunnel arch's
    and the stand flags' are both the FC Barcelona crest. Copied verbatim, every
    crowd in every ground flew Barcelona; and because the importer keys textures by
    bare filename, one sys_zero_bsm.png per stadium was shared between all four
    models, so whichever was converted last overwrote the rest.

    So a placeholder is not imported. The material is pointed at the engine's own
    neutral flag instead, named for the side it belongs to, and the engine paints
    the playing team's badge over it at kick-off.
    """

    def test_pes_placeholders_are_recognised(self):
        for name in ("sys_zero_bsm", "sys_zero_bsm.ftex", "SYS_ZERO_BSM_tmp", "dummy_bsm"):
            self.assertTrue(stadium_crowd.is_placeholder_texture(name), name)

    def test_real_artwork_is_not(self):
        for name in ("au_h_col_bsm_rgba32", "mob_teamflag_nrm_nomip", "acl_circlef_prop000"):
            self.assertFalse(stadium_crowd.is_placeholder_texture(name), name)

    def test_nothing_is_not_a_placeholder(self):
        self.assertFalse(stadium_crowd.is_placeholder_texture(None))
        self.assertFalse(stadium_crowd.is_placeholder_texture(""))

    def test_a_flag_knows_which_side_it_is(self):
        self.assertEqual(stadium_crowd.flag_side("mob_prop_teamflag_home01.fmdl"), "home")
        self.assertEqual(stadium_crowd.flag_side("mob_prop_teamflag_away01.fmdl"), "away")

    def test_a_flag_that_says_neither_is_the_home_ends_flag(self):
        # PES only ships an away01; the rest of the ground is the home crowd
        self.assertEqual(stadium_crowd.flag_side("mob_prop_teamflag01.fmdl"), "home")

    def test_a_placeholder_becomes_the_engines_own_flag_for_that_side(self):
        self.assertEqual(stadium_crowd.flag_bitmap("sys_zero_bsm", "mob_prop_teamflag_home01"),
                         "media/textures/stadium/teamflag_home.png")
        self.assertEqual(stadium_crowd.flag_bitmap("sys_zero_bsm", "mob_prop_teamflag_away01"),
                         "media/textures/stadium/teamflag_away.png")

    def test_a_flags_real_artwork_is_left_to_the_converter(self):
        # the cloth's own normal map and the like: None means "import it as usual"
        self.assertIsNone(stadium_crowd.flag_bitmap("mob_teamflag_nrm_nomip",
                                                    "mob_prop_teamflag_home01"))

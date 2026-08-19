"""Tests for the cloth in a pack: nets, flags, banners, pennants.

Every piece of cloth here was handled ad hoc and each broke differently.

  Goal netting     comes out solid and right. The one that works.
  Corner flag      its cloth was authored at z 1.227..1.570 off a 1.554 m pole and the
                   per-mesh grounding dropped it to the floor. Fixed by measuring a
                   prop's placement once (stadium_props.prop_placement).
  banner / pennant PES authors them flat at z 0.016 and 0.017 and positions them from
                   demo choreography we do not apply, so they lie on the grass.

What remains is a two-sided cloth's mapping. A corner flag is one quad built as two
sheets so it is visible from either side, and measured on props_09 the sheets sample
different halves of the texture:

  16 faces at v < 0.63  the red/yellow checker   area 0.0697 m2, largest face 0.0065
  16 faces at v >= 0.63 the pole strip           area 0.0689 m2, largest face 0.0065

Identical areas and identical largest face, so they are the front and back of the same
cloth rather than cloth and fittings - and the back shows the pole strip, which is the
grey band and dark disc that appear from the wrong side. A real corner flag is printed
both sides, so the back sheet takes the front's art: each back corner is matched to the
front vertex at the same position and copies its UV, which needs no guess about what
offset PES intended.

Run: python3 -m unittest test_cloth -v
"""

import unittest

import cloth


# A quad as two sheets: front wound one way at v 0.30, back the other at v 0.80.
FRONT = [((0.0, 0.0, 1.3), (0.30, 0.30)), ((0.3, 0.0, 1.3), (0.60, 0.30)),
         ((0.3, 0.0, 1.6), (0.60, 0.55))]
BACK = [((0.0, 0.0, 1.3), (0.30, 0.80)), ((0.3, 0.0, 1.6), (0.60, 0.95)),
        ((0.3, 0.0, 1.3), (0.60, 0.80))]


class MatchingTheSheetsOfATwoSidedCloth(unittest.TestCase):
    def test_the_back_takes_the_front_s_uvs(self):
        fixed = cloth.match_two_sided_uvs([FRONT, BACK])
        back = dict(fixed[1])
        self.assertEqual(back[(0.0, 0.0, 1.3)], (0.30, 0.30))
        self.assertEqual(back[(0.3, 0.0, 1.6)], (0.60, 0.55))

    def test_the_front_is_not_touched(self):
        fixed = cloth.match_two_sided_uvs([FRONT, BACK])
        self.assertEqual(fixed[0], FRONT)

    def test_the_majority_region_is_the_one_that_wins(self):
        # three sheets on the checker and one on the strip: the strip yields
        sheets = [FRONT, FRONT, FRONT, BACK]
        fixed = cloth.match_two_sided_uvs(sheets)
        self.assertEqual(dict(fixed[3])[(0.0, 0.0, 1.3)], (0.30, 0.30))

    def test_a_corner_with_no_partner_keeps_what_it_had(self):
        # never invent a UV for a vertex the other sheet does not have
        odd = [((9.0, 9.0, 9.0), (0.10, 0.90))]
        fixed = cloth.match_two_sided_uvs([FRONT, odd])
        self.assertEqual(fixed[1], odd)

    def test_a_cloth_already_agreeing_is_left_exactly_as_it_is(self):
        fixed = cloth.match_two_sided_uvs([FRONT, FRONT])
        self.assertEqual(fixed, [FRONT, FRONT])

    def test_one_sheet_alone_is_not_two_sided(self):
        self.assertEqual(cloth.match_two_sided_uvs([FRONT]), [FRONT])

    def test_nothing_at_all_is_nothing(self):
        self.assertEqual(cloth.match_two_sided_uvs([]), [])


class DecidingWhetherACothIsTwoSided(unittest.TestCase):
    """Two sheets, same area, same shape, opposite windings, different UV regions.

    The area check is what keeps fittings out of it: the corner flag's two sheets
    measure 0.0697 and 0.0689 square metres, within 2%, while a cloth and a bracket
    would not be close.
    """

    def test_two_matched_sheets_are_two_sided(self):
        self.assertTrue(cloth.looks_two_sided(0.0697, 0.0689))

    def test_a_cloth_and_a_fitting_are_not(self):
        self.assertFalse(cloth.looks_two_sided(0.0697, 0.0031))

    def test_nothing_measurable_is_not(self):
        self.assertFalse(cloth.looks_two_sided(0.0, 0.0))


class FixingAWholeMesh(unittest.TestCase):
    """Faces in, faces out: group them into sheets by texture region, then match.

    The corner flag's cloth is 32 faces over 27 texture vertices, so the fix has to
    work on a face list with a shared UV pool, not on tidy separate sheets.
    """

    # a quad's front (v around 0.3) and back (v around 0.8), as face corner lists
    MESH = [
        [((0.0, 0.0, 1.3), (0.30, 0.30)), ((0.3, 0.0, 1.3), (0.60, 0.30)),
         ((0.3, 0.0, 1.6), (0.60, 0.55))],
        [((0.0, 0.0, 1.3), (0.30, 0.30)), ((0.3, 0.0, 1.6), (0.60, 0.55)),
         ((0.0, 0.0, 1.6), (0.30, 0.55))],
        [((0.0, 0.0, 1.3), (0.30, 0.80)), ((0.3, 0.0, 1.6), (0.60, 0.95)),
         ((0.3, 0.0, 1.3), (0.60, 0.80))],
        [((0.0, 0.0, 1.3), (0.30, 0.80)), ((0.0, 0.0, 1.6), (0.30, 0.95)),
         ((0.3, 0.0, 1.6), (0.60, 0.95))],
    ]

    def test_the_back_faces_end_up_on_the_front_s_art(self):
        fixed, moved = cloth.match_mesh_uvs(self.MESH)
        self.assertEqual(moved, 2)
        for face in fixed:
            for _, uv in face:
                self.assertLess(uv[1], 0.63)

    def test_the_front_faces_are_untouched(self):
        fixed, _ = cloth.match_mesh_uvs(self.MESH)
        self.assertEqual(fixed[0], self.MESH[0])
        self.assertEqual(fixed[1], self.MESH[1])

    def test_a_mesh_all_in_one_region_is_left_alone(self):
        one = self.MESH[:2]
        fixed, moved = cloth.match_mesh_uvs(one)
        self.assertEqual(moved, 0)
        self.assertEqual(fixed, one)

    def test_an_empty_mesh_is_no_work(self):
        self.assertEqual(cloth.match_mesh_uvs([]), ([], 0))


class GatingItOnTwoSidedness(unittest.TestCase):
    """The gate that makes match_mesh_uvs safe to wire into an importer.

    Ungated it repaints the pole. The corner flag's pole legitimately uses the same
    bottom strip of cf_common_bsm that the flag's back sheet wrongly samples - its grey
    band lives there - so running the majority rule over the pole's 274 faces moves 176
    of them. A mesh only gets the treatment when its two texture regions look like the
    front and back of one cloth: comparable area, and each covering a similar count.
    """

    def test_a_flag_cloth_qualifies(self):
        # 16 faces on each region, 0.0697 and 0.0689 square metres
        self.assertTrue(cloth.regions_are_two_sided(16, 0.0697, 16, 0.0689))

    def test_the_pole_does_not(self):
        # 114 faces on one region and 160 on the other, and nothing like equal area
        self.assertFalse(cloth.regions_are_two_sided(114, 0.0421, 160, 0.2637))

    def test_a_lopsided_face_count_disqualifies_it(self):
        self.assertFalse(cloth.regions_are_two_sided(16, 0.0697, 3, 0.0689))

    def test_a_lopsided_area_disqualifies_it(self):
        self.assertFalse(cloth.regions_are_two_sided(16, 0.0697, 16, 0.0031))

    def test_nothing_measurable_does_not(self):
        self.assertFalse(cloth.regions_are_two_sided(0, 0.0, 0, 0.0))

    def test_the_mesh_pass_leaves_a_pole_alone(self):
        # two regions, unequal in both count and area: no change at all
        pole = ([[((0.0, 0.0, z / 10.0), (0.55, 0.30)) for z in range(3)]] * 40
                + [[((1.0, 0.0, z / 10.0), (0.55, 0.80)) for z in range(3)]] * 5)
        fixed, moved = cloth.match_mesh_uvs(pole, gated=True)
        self.assertEqual(moved, 0)
        self.assertEqual(fixed, pole)


class SplittingAMeshIntoItsTwoRegions(unittest.TestCase):
    """Bucketing mean V was the wrong model and the real mesh said so.

    The corner flag's 32 faces fall into five buckets at a 0.2 grid, because their V
    values spread across 0.266..0.991 - so a fixed grid invents regions that are not
    there. What is actually present is two clusters with a gap between them, which is
    what a hand split at 0.63 found. So the boundary is learned: sort the faces by mean
    V and cut at the largest gap, and only call it two regions when that gap is wide
    enough to be one.
    """

    def test_two_clusters_are_found_wherever_they_sit(self):
        means = [0.30, 0.31, 0.33, 0.29, 0.80, 0.82, 0.79, 0.81]
        low, high = cloth.split_regions(means)
        self.assertEqual(sorted(low), [0, 1, 2, 3])
        self.assertEqual(sorted(high), [4, 5, 6, 7])

    def test_the_flags_own_spread_splits_in_two(self):
        # measured: the cloth's faces sit at about 0.45 and 0.86
        means = [0.42, 0.45, 0.47, 0.44, 0.50, 0.84, 0.86, 0.88, 0.85, 0.90]
        low, high = cloth.split_regions(means)
        self.assertEqual(len(low), 5)
        self.assertEqual(len(high), 5)

    def test_one_spread_out_region_is_not_split(self):
        # an even spread has no gap worth cutting at
        means = [0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50, 0.55]
        low, high = cloth.split_regions(means)
        self.assertEqual(high, [])

    def test_nothing_is_no_regions(self):
        self.assertEqual(cloth.split_regions([]), ([], []))

    def test_a_degenerate_face_does_not_break_the_area(self):
        # the real file has faces with a repeated corner
        self.assertEqual(cloth._face_area([((0, 0, 0), (0.1, 0.1))]), 0.0)

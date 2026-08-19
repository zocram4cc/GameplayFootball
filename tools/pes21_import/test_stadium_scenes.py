"""Tests for taking a whole stadium rather than one of its models.

A PES stadium is not one mesh file. Its own scene graph names fifteen -
back1/2/3, center1/2/3, front1/2/3, left1/2/3, right1/2/3 (Planet Namek's
st017.fox2.xml lists exactly those) - and the converter took center1 and nothing
else. The 4cc authors mostly bake their ground into center1, which is why that
went unnoticed, but not always:

    st002   center1.fmdl  front3.fmdl
    st060   center1.fmdl  plane.fmdl  plane_f4u.fmdl

so st002 was missing a front section and st060 two aeroplanes. The same pack also
holds each model twice, once in <pack>_fpk/ and once in <pack>_fpk_extracted/, so
a naive sweep imports everything double.

Run: python3 -m unittest test_stadium_scenes -v
"""

import unittest

import stadium_to_gf


class ChooseSceneModels(unittest.TestCase):
    def test_the_centre_scene_leads_and_its_siblings_follow(self):
        centre, extras = stadium_to_gf.choose_scene_models([
            "/p/#Win/st002_fpk_extracted/scenes/center1.fmdl",
            "/p/#Win/st002_fpk_extracted/scenes/front3.fmdl",
        ])
        self.assertTrue(centre.endswith("center1.fmdl"))
        self.assertEqual([e.split("/")[-1] for e in extras], ["front3.fmdl"])

    def test_the_same_model_twice_is_imported_once(self):
        # every pack holds its scenes in both st002_fpk/ and st002_fpk_extracted/
        centre, extras = stadium_to_gf.choose_scene_models([
            "/p/#Win/st002_fpk/scenes/center1.fmdl",
            "/p/#Win/st002_fpk/scenes/front3.fmdl",
            "/p/#Win/st002_fpk_extracted/scenes/center1.fmdl",
            "/p/#Win/st002_fpk_extracted/scenes/front3.fmdl",
        ])
        self.assertTrue(centre.endswith("center1.fmdl"))
        self.assertEqual(len(extras), 1)

    def test_everything_else_the_pack_ships_comes_too(self):
        # st060's two aeroplanes are part of that ground, not decoration to drop
        _centre, extras = stadium_to_gf.choose_scene_models([
            "/p/#Win/st060_fpk_extracted/center1.fmdl",
            "/p/#Win/st060_fpk_extracted/plane.fmdl",
            "/p/#Win/st060_fpk_extracted/plane_f4u.fmdl",
        ])
        self.assertEqual(sorted(e.split("/")[-1] for e in extras),
                         ["plane.fmdl", "plane_f4u.fmdl"])

    def test_a_full_pes_scene_keeps_all_fifteen(self):
        names = ["%s%d.fmdl" % (side, tier)
                 for side in ("back", "center", "front", "left", "right")
                 for tier in (1, 2, 3)]
        centre, extras = stadium_to_gf.choose_scene_models(["/p/#Win/x/" + n for n in names])
        self.assertTrue(centre.endswith("center1.fmdl"))
        self.assertEqual(len(extras), 14)

    def test_the_order_is_settled_rather_than_whatever_the_disk_says(self):
        first = stadium_to_gf.choose_scene_models(
            ["/p/b.fmdl", "/p/center1.fmdl", "/p/a.fmdl"])[1]
        second = stadium_to_gf.choose_scene_models(
            ["/p/a.fmdl", "/p/b.fmdl", "/p/center1.fmdl"])[1]
        self.assertEqual(first, second)

    def test_a_pack_without_a_centre_scene_still_converts(self):
        centre, extras = stadium_to_gf.choose_scene_models(["/p/left1.fmdl", "/p/right1.fmdl"])
        self.assertTrue(centre.endswith("left1.fmdl"))
        self.assertEqual(len(extras), 1)

    def test_no_models_is_no_scene(self):
        self.assertEqual(stadium_to_gf.choose_scene_models([]), (None, []))


if __name__ == "__main__":
    unittest.main()


class PesPitchToOurs(unittest.TestCase):
    """PES authors on a smaller pitch than this engine plays on.

    PES's pitch is 105 x 68 m (half 52.5 x 34); gametypes.hpp has this one at
    110 x 72 (half 55 x 36). Geometry authored around PES's pitch therefore lands
    two and a half metres too far in at each goal and two metres too far in at each
    touchline.

    It showed up on the advertising ring. Its boards stand 4.17 m behind PES's goal
    line and 0.28 m outside its touchline - a real setback - but dropped on a longer
    pitch that becomes 1.67 m behind the goal line, and the engine's own goal net is
    2.55 m deep (goals.ase reaches x 57.55). So the hoardings ran straight through
    the netting.

    Scaling by the ratio of the two pitches keeps PES's placement relative to the
    pitch, which is what its author meant, instead of nudging boards by hand.
    """

    def test_the_scale_is_the_ratio_of_the_two_pitches(self):
        scale = stadium_to_gf.pitch_scale()
        self.assertAlmostEqual(scale[0], 55.0 / 52.5, places=6)
        self.assertAlmostEqual(scale[1], 36.0 / 34.0, places=6)

    def test_height_is_never_scaled(self):
        # a hoarding is a metre tall in either game; only the plan changes
        self.assertEqual(stadium_to_gf.pitch_scale()[2], 1.0)

    def test_a_board_behind_pes_goal_line_ends_up_behind_ours(self):
        scaled = stadium_to_gf.scale_positions([(-56.67, 0.0, 1.06)],
                                               stadium_to_gf.pitch_scale())
        # clear of the engine's netting, which reaches 57.55
        self.assertLess(scaled[0][0], -57.55)

    def test_the_setback_is_preserved_in_proportion(self):
        # 4.17 m behind a 52.5 m half-pitch is 4.37 m behind a 55 m one
        scaled = stadium_to_gf.scale_positions([(-56.67, -34.28, 0.0)],
                                               stadium_to_gf.pitch_scale())
        self.assertAlmostEqual(abs(scaled[0][0]) - 55.0, 4.17 * (55.0 / 52.5), places=2)
        self.assertAlmostEqual(abs(scaled[0][1]) - 36.0, 0.28 * (36.0 / 34.0), places=2)

    def test_the_centre_spot_does_not_move(self):
        self.assertEqual(stadium_to_gf.scale_positions([(0.0, 0.0, 0.0)],
                                                       stadium_to_gf.pitch_scale()),
                         [(0.0, 0.0, 0.0)])

    def test_no_scale_leaves_everything_alone(self):
        points = [(1.0, -2.0, 3.0)]
        self.assertEqual(stadium_to_gf.scale_positions(points, None), points)


class HoardingsLookAtThePitch(unittest.TestCase):
    """A hoarding that faces the stands is no hoarding at all.

    The advertising ring came in with 84 of its 102 board faces wound so their
    normals point away from the pitch centre: invisible from the broadcast camera,
    which stands inside the ring, and visible only from above and outside. Same root
    cause as the centre-circle banner - PES's winding convention is the opposite of
    this engine's, and the engine both culls back faces and derives lighting from the
    winding that was written.

    Judged in plan and by area, and only for a package whose every face is meant to
    look at the pitch: a stadium's walls and roofs are a different question and are
    left alone.
    """

    def _quad(self, centre_y, facing):
        # a vertical board a few metres from the centre spot, facing the pitch (-y)
        # or the stands (+y)
        y = centre_y
        v = [(-2.0, y, 0.0), (2.0, y, 0.0), (2.0, y, 1.0), (-2.0, y, 1.0)]
        f = [(0, 1, 2), (0, 2, 3)]
        if facing == "stands":
            f = [(2, 1, 0), (3, 2, 0)]
        return v, f

    def test_a_board_facing_the_stands_is_turned_round(self):
        v, f = self._quad(40.0, "stands")
        self.assertTrue(stadium_to_gf.faces_away_from_pitch(v, f))

    def test_a_board_facing_the_pitch_is_left_alone(self):
        v, f = self._quad(40.0, "pitch")
        self.assertFalse(stadium_to_gf.faces_away_from_pitch(v, f))

    def test_the_far_side_is_judged_the_same_way(self):
        # a board on the other touchline faces +y to look at the pitch
        v, f = self._quad(-40.0, "pitch")
        self.assertTrue(stadium_to_gf.faces_away_from_pitch(v, f))
        v, f = self._quad(-40.0, "stands")
        self.assertFalse(stadium_to_gf.faces_away_from_pitch(v, f))

    def test_something_lying_flat_is_not_a_hoarding(self):
        # the ring's own base and shadow planes: their normals are vertical, and
        # which way they face is not a question about the pitch
        v = [(-2.0, 40.0, 0.0), (2.0, 40.0, 0.0), (2.0, 44.0, 0.0), (-2.0, 44.0, 0.0)]
        for f in ([(0, 1, 2), (0, 2, 3)], [(2, 1, 0), (3, 2, 0)]):
            self.assertFalse(stadium_to_gf.faces_away_from_pitch(v, f))

    def test_a_board_over_the_centre_spot_is_left_alone(self):
        # nothing to point at: no inward direction exists
        v = [(-2.0, 0.0, 0.0), (2.0, 0.0, 0.0), (2.0, 0.0, 1.0), (-2.0, 0.0, 1.0)]
        self.assertFalse(stadium_to_gf.faces_away_from_pitch(v, [(2, 1, 0), (3, 2, 0)]))

    def test_nothing_is_left_alone(self):
        self.assertFalse(stadium_to_gf.faces_away_from_pitch([], []))


class PesRenderPasses(unittest.TestCase):
    """PES splits one piece of geometry into several material passes.

    st031's centre scene holds 22 meshes over 17 distinct geometries, and every
    duplicate is the same pair: one mesh on technique fox3DFW_ConstantSRGB_NDR_Solid
    with material "Material", and a twin on fox3DDF_Blin_Fuzzblock with material
    "Material antiblur". That is PES's motion-blur mask - a copy of the geometry
    drawn to keep it out of the blur - not artwork and not an outline. Imported, both
    get written, so the mesh is drawn twice at exactly the same place: z-fighting,
    and 22,323 wasted vertices on st031 alone (st060 29,704, st056 and st043 and
    st011 their own share).

    This is not the cel-shade outline, which is a different thing: an outline carries
    a texture named outline.png and sits pushed out along its normals
    (is_outline_pass, and stadium_staff.wants_winding_flipped for the winding).
    """

    def test_the_blur_mask_is_recognised_by_its_technique(self):
        self.assertTrue(stadium_to_gf.is_antiblur_pass("fox3DDF_Blin_Fuzzblock", "Material"))
        self.assertTrue(stadium_to_gf.is_antiblur_pass("FOX3DDF_BLIN_FUZZBLOCK", ""))

    def test_it_is_also_recognised_by_pes_own_material_name(self):
        self.assertTrue(stadium_to_gf.is_antiblur_pass("", "Material antiblur"))

    def test_a_normal_pass_is_not(self):
        self.assertFalse(stadium_to_gf.is_antiblur_pass("fox3DFW_ConstantSRGB_NDR_Solid",
                                                       "Material"))
        self.assertFalse(stadium_to_gf.is_antiblur_pass(None, None))

    def test_a_blur_mask_over_geometry_that_is_already_there_is_dropped(self):
        keep = stadium_to_gf.keep_visible_passes([("A", False), ("A", True)])
        self.assertEqual(keep, [0])

    def test_the_order_of_the_pair_does_not_matter(self):
        self.assertEqual(stadium_to_gf.keep_visible_passes([("A", True), ("A", False)]), [1])

    def test_a_blur_mask_with_no_twin_is_kept(self):
        # better a mesh drawn once from the wrong pass than a hole in the ground
        self.assertEqual(stadium_to_gf.keep_visible_passes([("A", True)]), [0])

    def test_distinct_geometry_is_all_kept(self):
        self.assertEqual(stadium_to_gf.keep_visible_passes([("A", False), ("B", False)]), [0, 1])

    def test_two_visible_passes_on_one_geometry_are_both_kept(self):
        # PES does layer real passes - a second, blended coat over the first - and
        # dropping one of those would lose artwork
        self.assertEqual(stadium_to_gf.keep_visible_passes([("A", False), ("A", False)]), [0, 1])

    def test_nothing_keeps_nothing(self):
        self.assertEqual(stadium_to_gf.keep_visible_passes([]), [])


class AdvertisingReadsFromThePitch(unittest.TestCase):
    """Turning a hoarding round is not enough; its text has to read the right way.

    Rendered and read off the capture, the imported ring shows a run of boards where
    some ads read normally and others are mirrored ("LESBIANS" as "SNAIBSEL").
    Measured over adboards.ase: of 89 meshes carrying board faces, 70 read correctly
    throughout and one is mirrored throughout, but 18 - the big merged runs - hold
    both, and those carry about 900 of the 916 mirrored faces. Inside one such mesh
    the same UV rectangle turns up twice with opposite handedness (nine faces at
    U 0.668..0.854 mirrored against nine at U 0.670..0.841 readable), which is what a
    modeller mirroring a duplicated segment leaves behind. PES gets away with it
    because it assigns the advertising faces at runtime through bill_anime.json; this
    engine uses the model's own UVs, so the mirrored copies show mirrored ads.

    So the handedness is normalised: for a board face whose U grows towards the
    viewer's LEFT when seen from the pitch, U is mirrored within that face's own
    extent, which for a UV rectangle is exactly the panel flipped back.
    """

    def test_a_panel_facing_the_pitch_with_u_growing_rightwards_is_left_alone(self):
        # a board on the far touchline (y = +40) facing the pitch: normal -y, so a
        # viewer at the centre sees +x as his right
        vertices = [(0.0, 40.0, 0.0), (2.0, 40.0, 0.0), (2.0, 40.0, 1.0)]
        uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0)]
        self.assertFalse(stadium_to_gf.face_reads_mirrored(vertices, uvs))

    def test_the_same_panel_with_u_growing_leftwards_reads_mirrored(self):
        vertices = [(0.0, 40.0, 0.0), (2.0, 40.0, 0.0), (2.0, 40.0, 1.0)]
        uvs = [(1.0, 0.0), (0.0, 0.0), (0.0, 1.0)]
        self.assertTrue(stadium_to_gf.face_reads_mirrored(vertices, uvs))

    def test_a_board_behind_the_goal_is_judged_in_its_own_frame(self):
        # x = +55, facing the pitch: normal -x, so the viewer's right is -y
        vertices = [(55.0, 2.0, 0.0), (55.0, 0.0, 0.0), (55.0, 0.0, 1.0)]
        uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0)]
        self.assertFalse(stadium_to_gf.face_reads_mirrored(vertices, uvs))

    def test_a_face_lying_flat_has_no_text_to_read(self):
        # the ring's own ground shadow: nothing to mirror, and no viewer frame either
        vertices = [(0.0, 0.0, 0.0), (2.0, 0.0, 0.0), (2.0, 2.0, 0.0)]
        uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0)]
        self.assertIsNone(stadium_to_gf.face_reads_mirrored(vertices, uvs))

    def test_a_degenerate_face_is_not_judged(self):
        vertices = [(0.0, 40.0, 0.0), (0.0, 40.0, 0.0), (0.0, 40.0, 0.0)]
        self.assertIsNone(stadium_to_gf.face_reads_mirrored(vertices, [(0.0, 0.0)] * 3))

    def test_mirroring_flips_u_within_the_faces_own_extent(self):
        # 0.668..0.854 stays 0.668..0.854; only which corner is which changes
        uvs = [(0.668, 0.0), (0.854, 0.0), (0.854, 1.0)]
        flipped = stadium_to_gf.mirror_face_u(uvs)
        self.assertAlmostEqual(flipped[0][0], 0.854)
        self.assertAlmostEqual(flipped[1][0], 0.668)
        self.assertAlmostEqual(flipped[2][0], 0.668)

    def test_mirroring_leaves_v_alone(self):
        uvs = [(0.1, 0.25), (0.9, 0.75), (0.9, 0.5)]
        for before, after in zip(uvs, stadium_to_gf.mirror_face_u(uvs)):
            self.assertAlmostEqual(before[1], after[1])

    def test_mirroring_twice_is_the_original(self):
        uvs = [(0.2, 0.0), (0.7, 0.0), (0.7, 1.0)]
        twice = stadium_to_gf.mirror_face_u(stadium_to_gf.mirror_face_u(uvs))
        for before, after in zip(uvs, twice):
            self.assertAlmostEqual(before[0], after[0])

    def test_a_mirrored_panel_reads_correctly_once_flipped(self):
        vertices = [(0.0, 40.0, 0.0), (2.0, 40.0, 0.0), (2.0, 40.0, 1.0)]
        uvs = [(1.0, 0.0), (0.0, 0.0), (0.0, 1.0)]
        self.assertTrue(stadium_to_gf.face_reads_mirrored(vertices, uvs))
        self.assertFalse(
            stadium_to_gf.face_reads_mirrored(vertices, stadium_to_gf.mirror_face_u(uvs)))

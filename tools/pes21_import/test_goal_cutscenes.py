"""Tests for tying each goal celebration to the camerawork PES shot it with.

PES ships a goal cutscene as a matched set. For the celebration it calls "banzai":

    goal_2018_run_30_banzai.chor              who performs it, and what they play
    goal_2018_run_30_banzai_Z_fromL.camtrack  the camera, from the left
    goal_2018_run_30_banzai_Z_fromR.camtrack  and from the right

The choreography names the clip its primary actor performs
(`slot 0 anims/dml_goal_move3_0002.anim role primary phase 0 loop 1`), so the tie
between a celebration and its camerawork is in the data rather than in a guess.
Stripping the _Z_fromL/_Z_fromR/_Flip suffix from a camera's name gives the
choreography it belongs to: measured over what we have, 38 camera tracks fall onto
19 celebrations and none is left over.

Run: python3 -m unittest test_goal_cutscenes -v
"""

import unittest

import goal_cutscenes


CHOR = """chor 1
source goal_2018_run_30_banzai.fdc
slot 0 anims/dml_goal_move3_0002.anim role primary phase 0 loop 1
k 0 0.0000 -0.0000 0.00000
k 2 -0.0010 -0.0271 0.00000
slot 1 anims/dml_goal_move3_0002_sub1.anim role secondary phase 0 loop 1
k 0 1.0000 -2.0000 0.00000
"""


class ReadingAChoreography(unittest.TestCase):
    def test_the_primary_actors_clip_is_the_celebration(self):
        self.assertEqual(goal_cutscenes.primary_clip(CHOR), "dml_goal_move3_0002.anim")

    def test_the_secondary_actors_are_not_mistaken_for_it(self):
        # a teammate joining in is not the celebration being filmed
        self.assertNotIn("sub1", goal_cutscenes.primary_clip(CHOR))

    def test_a_choreography_with_no_primary_has_no_clip(self):
        self.assertIsNone(goal_cutscenes.primary_clip("chor 1\nsource x.fdc\n"))

    def test_every_slot_is_listed_with_its_role(self):
        slots = goal_cutscenes.slots(CHOR)
        self.assertEqual(len(slots), 2)
        self.assertEqual(slots[0]["role"], "primary")
        self.assertEqual(slots[1]["role"], "secondary")
        self.assertEqual(slots[1]["clip"], "dml_goal_move3_0002_sub1.anim")


class MatchingCamerasToCelebrations(unittest.TestCase):
    def test_the_angle_suffix_is_what_separates_them(self):
        for name, base in (("goal_2018_run_30_banzai_Z_fromL", "goal_2018_run_30_banzai"),
                           ("goal_2018_run_30_banzai_Z_fromR", "goal_2018_run_30_banzai"),
                           ("goal_2018_run_30_step_Flip", "goal_2018_run_30_step"),
                           ("goal_2018_run_30", "goal_2018_run_30")):
            self.assertEqual(goal_cutscenes.celebration_base(name), base)

    def test_a_celebration_collects_its_angles(self):
        cams = ["goal_2018_run_30_banzai_Z_fromL", "goal_2018_run_30_banzai_Z_fromR",
                "goal_2018_run_40_banzai_Z_fromL"]
        chors = {"goal_2018_run_30_banzai": "dml_goal_move3_0002.anim",
                 "goal_2018_run_40_banzai": "dml_goal_move4_0002.anim"}
        built = goal_cutscenes.build(chors, cams)
        self.assertEqual(built["goal_2018_run_30_banzai"]["cameras"],
                         ["goal_2018_run_30_banzai_Z_fromL", "goal_2018_run_30_banzai_Z_fromR"])
        self.assertEqual(built["goal_2018_run_40_banzai"]["clip"], "dml_goal_move4_0002.anim")

    def test_a_celebration_nobody_filmed_is_still_listed(self):
        # PES ships 11 of these; they are usable performances with no camerawork,
        # and silently dropping them would lose the fact
        built = goal_cutscenes.build({"goal_2018_run_30_plane": "dml_goal_move3_0006.anim"}, [])
        self.assertEqual(built["goal_2018_run_30_plane"]["cameras"], [])

    def test_a_camera_with_no_choreography_is_refused(self):
        # inside this family it would be a camera aimed at nothing; better to know
        with self.assertRaises(ValueError):
            goal_cutscenes.build({}, ["goal_2018_run_30_banzai_Z_fromL"])

    def test_more_angle_suffixes_than_the_two_obvious_ones(self):
        # measured across the full library: _camNN, _appendCam, _audi and friends all
        # tie a camera back to its choreography
        for name, base in (("goal_2019_S_Golazo_01_cam00", "goal_2019_S_Golazo_01"),
                           ("x_appendCam", "x"),
                           ("x_cam_up", "x"),
                           ("x_cam_default", "x"),
                           ("x_audi", "x")):
            self.assertEqual(goal_cutscenes.celebration_base(name), base)


class SortingTheLibraryIntoFamilies(unittest.TestCase):
    """A goal camera is not always a celebration camera.

    Of PES's 731 goal tracks, 355 are the numbered celebrations, some belong to staged
    cutscenes with choreographies, and about 200 are neither: goal_st033_TV,
    goal_st061_away_G4 - a ground's own goal cameras, nothing to do with what the
    scorer does. Sweeping the lot into the celebration manifest would tie cameras to
    performances they were never shot for, so they are sorted and the leftovers named.
    """

    def test_a_numbered_celebration_goes_to_its_family(self):
        families = goal_cutscenes.sort_cameras(
            ["goal_celebrate_0092_mayaL0x"], chor_names=set())
        self.assertEqual(families["by_id"], ["goal_celebrate_0092_mayaL0x"])

    def test_a_choreographed_one_goes_to_its_own(self):
        families = goal_cutscenes.sort_cameras(
            ["goal_2018_run_30_banzai_Z_fromL"], chor_names={"goal_2018_run_30_banzai"})
        self.assertEqual(families["by_chor"], ["goal_2018_run_30_banzai_Z_fromL"])

    def test_a_grounds_own_camera_is_neither(self):
        families = goal_cutscenes.sort_cameras(["goal_st033_TV"], chor_names=set())
        self.assertEqual(families["other"], ["goal_st033_TV"])


class TheCelebrationsKeyedByNumber(unittest.TestCase):
    """PES's main celebration library is not shaped like a cutscene at all.

    The goal_2018_run_* family above is a handful of staged cutscenes with .chor
    files. The bulk is different: 355 cameras named goal_celebrate_NNNN with an angle
    suffix, and 790 performances named dml_goal_celebrate_NNNN, tied by the four-digit
    celebration number and nothing else. Every one of the 213 numbers that has a
    camera also has a performance, so the tie is complete - and it is ten times the
    19 celebrations the .chor route finds.
    """

    def test_the_number_is_read_from_either_side(self):
        self.assertEqual(goal_cutscenes.celebration_id("goal_celebrate_0092_mayaL1x"), "0092")
        self.assertEqual(goal_cutscenes.celebration_id("dml_goal_celebrate_0092.anim"), "0092")

    def test_anything_else_has_no_number(self):
        self.assertIsNone(goal_cutscenes.celebration_id("goal_2018_run_30_banzai"))
        self.assertIsNone(goal_cutscenes.celebration_id("goal_st033_TV"))

    def test_a_number_collects_its_angles(self):
        built = goal_cutscenes.build_by_id(
            ["dml_goal_celebrate_0092.anim", "dml_goal_celebrate_0093.anim"],
            ["goal_celebrate_0092_mayaL0x", "goal_celebrate_0092_mayaL1x",
             "goal_celebrate_0093"])
        self.assertEqual(sorted(built), ["celebrate_0092", "celebrate_0093"])
        self.assertEqual(built["celebrate_0092"]["cameras"],
                         ["goal_celebrate_0092_mayaL0x", "goal_celebrate_0092_mayaL1x"])
        self.assertEqual(built["celebrate_0092"]["clip"], "dml_goal_celebrate_0092.anim")

    def test_a_performance_nobody_filmed_is_left_out(self):
        # 296 numbers have a performance and only 213 a camera; the rest would be a
        # celebration with no camerawork, which is what the seeded draw must not pick
        built = goal_cutscenes.build_by_id(["dml_goal_celebrate_0500.anim"], [])
        self.assertEqual(built, {})

    def test_a_camera_with_no_performance_is_left_out(self):
        built = goal_cutscenes.build_by_id([], ["goal_celebrate_0500_mayaL0x"])
        self.assertEqual(built, {})

    def test_the_two_families_live_in_one_manifest(self):
        chor_side = {"goal_2018_run_30_banzai": {"clip": "dml_goal_move3_0002.anim",
                                                 "cameras": ["goal_2018_run_30_banzai_Z_fromL"]}}
        id_side = goal_cutscenes.build_by_id(["dml_goal_celebrate_0092.anim"],
                                             ["goal_celebrate_0092_mayaL0x"])
        merged = goal_cutscenes.merge(chor_side, id_side)
        self.assertIn("goal_2018_run_30_banzai", merged)
        self.assertIn("celebrate_0092", merged)


class TheVariableEachCelebrationIsAskedForBy(unittest.TestCase):
    """The engine picks a special animation by specialvar1/specialvar2, so tying a
    camera to a performance means both sides agreeing on a number. The manifest
    carries it, the installer stamps the clip with it, and the controller asks for it -
    one fact in one place instead of three that can drift.

    Only the filmed celebrations get one: an unfilmed performance has no camerawork to
    tie to, and handing it a number would put it in the draw.
    """

    def test_filmed_celebrations_are_numbered_from_the_base(self):
        built = {"a": {"clip": "a.anim", "cameras": ["a_Z_fromL"]},
                 "b": {"clip": "b.anim", "cameras": ["b_Z_fromL"]}}
        numbered = goal_cutscenes.assign_variables(built, base=100)
        self.assertEqual(numbered["a"]["var"], 100)
        self.assertEqual(numbered["b"]["var"], 101)

    def test_an_unfilmed_one_gets_none(self):
        built = {"a": {"clip": "a.anim", "cameras": []}}
        self.assertIsNone(goal_cutscenes.assign_variables(built, base=100)["a"]["var"])

    def test_the_numbering_is_stable_across_runs(self):
        built = {"b": {"clip": "b.anim", "cameras": ["b_Z_fromL"]},
                 "a": {"clip": "a.anim", "cameras": ["a_Z_fromL"]}}
        first = goal_cutscenes.assign_variables(built, base=100)
        second = goal_cutscenes.assign_variables(built, base=100)
        self.assertEqual(first["a"]["var"], second["a"]["var"])
        self.assertEqual(first["a"]["var"], 100)   # sorted by name, not by dict order


class TheManifest(unittest.TestCase):
    """One line per celebration, which is what the engine reads."""

    def test_a_line_carries_the_clip_and_its_cameras(self):
        built = {"goal_2018_run_30_banzai": {
            "clip": "dml_goal_move3_0002.anim", "var": 100,
            "cameras": ["goal_2018_run_30_banzai_Z_fromL", "goal_2018_run_30_banzai_Z_fromR"]}}
        text = goal_cutscenes.manifest_text(built)
        self.assertIn("celebration goal_2018_run_30_banzai", text)
        self.assertIn("var 100", text)
        self.assertIn("clip dml_goal_move3_0002.anim", text)
        self.assertIn("camera goal_2018_run_30_banzai_Z_fromL", text)
        self.assertIn("camera goal_2018_run_30_banzai_Z_fromR", text)

    def test_celebrations_come_out_in_a_settled_order(self):
        built = {"b": {"clip": "b.anim", "cameras": []},
                 "a": {"clip": "a.anim", "cameras": []}}
        text = goal_cutscenes.manifest_text(built)
        self.assertLess(text.index("celebration a"), text.index("celebration b"))

    def test_every_line_is_one_fact_or_a_comment(self):
        built = {"x": {"clip": "x.anim", "cameras": ["x_Z_fromL"]}}
        for line in goal_cutscenes.manifest_text(built).splitlines():
            if line and not line.startswith("#"):
                self.assertGreaterEqual(len(line.split()), 2)

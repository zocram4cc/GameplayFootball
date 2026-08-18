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

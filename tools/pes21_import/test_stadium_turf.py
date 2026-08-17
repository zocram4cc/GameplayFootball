"""Tests for giving a converted stadium its own turf.

The converter copies GF's stock pitch geometry - the engine's pitch carries the
line markings and the physics, and PES's pitch is a separate subsystem - but it
also copied GF's green grass, so Planet Namek played on an English lawn instead
of the teal blue it ships as st017_turf000_bsm.

The pitch itself is left alone. Its colour is generated at match start by
proceduralpitch.cpp, which overwrites the texture resources named pitch_0N.png -
so repointing the pitch materials at the pack's turf takes those names away and
the match dies as soon as the bitmaps are built. Instead the turf is dropped
beside the stadium object as turf.png, which the engine prefers over its own
seamless grass (src/onthepitch/pitchturf.hpp), and the generator keeps drawing the
line markings and the mow striping over it.

Run: python3 -m unittest test_stadium_turf -v
"""

import unittest

import stadium_to_gf

STOCK = '''*MATERIAL_LIST {
\t*MATERIAL 0 {
\t\t*MATERIAL_NAME "pitch_01"
\t\t*MAP_DIFFUSE {
\t\t\t*BITMAP "media/textures/pitch/pitch_01.png"
\t\t}
\t\t*MAP_SPECULAR {
\t\t\t*BITMAP "media/textures/pitch/pitch_specular_01.png"
\t\t}
\t\t*MAP_BUMP {
\t\t\t*BITMAP "media/textures/pitch/pitch_normal_01.png"
\t\t}
\t}
\t*MATERIAL 1 {
\t\t*MATERIAL_NAME "pitch_02"
\t\t*MAP_DIFFUSE {
\t\t\t*BITMAP "media/textures/pitch/pitch_02.png"
\t\t}
\t}
}
'''


class FindTurfTextureTest(unittest.TestCase):
    def test_picks_the_stadiums_own_turf_base_map(self):
        names = ["capsulecorpship.dds", "st017_turf000_bsm.dds", "st017_turf000_nrm.dds",
                 "st017_turf000_srm.dds", "outline.dds"]
        self.assertEqual(stadium_to_gf.find_turf_texture(names), "st017_turf000_bsm.dds")

    def test_prefers_the_base_map_over_the_normal_and_specular_maps(self):
        # _nrm and _srm are the normal and specular maps; picking one of those
        # would paint the pitch with a normal map.
        names = ["st043_turf000_nrm.dds", "st043_turf000_srm.dds", "st043_turf000_bsm.dds"]
        self.assertEqual(stadium_to_gf.find_turf_texture(names), "st043_turf000_bsm.dds")

    def test_falls_back_to_any_turf_base_map(self):
        self.assertEqual(stadium_to_gf.find_turf_texture(["turf_bsm_alp.dds"]),
                         "turf_bsm_alp.dds")

    def test_ignores_the_three_dimensional_grass_fins(self):
        # turf3d is the blades of grass standing up, not the ground colour.
        self.assertIsNone(stadium_to_gf.find_turf_texture(["grassfin_bsm_alp.dds"]))

    def test_no_turf_in_the_pack_is_not_an_error(self):
        self.assertIsNone(stadium_to_gf.find_turf_texture(["houses.dds", "outline.dds"]))
        self.assertIsNone(stadium_to_gf.find_turf_texture([]))


class TurfFilenameTest(unittest.TestCase):
    def test_the_turf_is_named_what_the_engine_looks_for(self):
        # src/onthepitch/pitchturf.cpp builds the candidate as turf.png beside
        # the stadium's .object; the two names have to agree.
        self.assertEqual(stadium_to_gf.TURF_FILENAME, "turf.png")


if __name__ == "__main__":
    unittest.main()

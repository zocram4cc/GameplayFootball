"""Tests for deciding which triangles are genuinely stretched.

The cut was absolute: fmdl_to_fullbody dropped any face with an edge over --max-edge,
0.15 m as import_team passes it. That works on a fine mesh and destroys a coarse one,
and the imported models show it. Over the 90 player models already in the tree, 44 have
their longest surviving edge sitting exactly on 0.15 - a distribution pressed flat
against a threshold is one that was cut - and nine are coarse meshes where 0.15 m is
only 1.6x to 3.6x their median edge. lcg_2715's median edge is 0.0955 m.

The shards the cut was written for were another matter entirely: up to 1.25 m on a
1.8 m body against a median of 1.9 cm, which is 65x the median. A triangle joining a
hand to a hairtip is not 1.6x the ordinary edge length, it is tens of times it.

So the threshold follows the mesh: an edge is stretched when it is far longer than that
mesh's own median edge. A fixed metre value cannot serve a mesh at 1.9 cm and a mesh at
9.6 cm at the same time.

Run: python3 -m unittest test_stretched_cut -v
"""

import unittest

import stretched_cut


def fan(edge, count):
    """`count` triangles whose edges are all about `edge` long."""
    return [edge] * (count * 3)


class WhereTheCutFalls(unittest.TestCase):
    def test_it_scales_with_the_mesh(self):
        fine = stretched_cut.threshold(fan(0.019, 100))
        coarse = stretched_cut.threshold(fan(0.0955, 100))
        self.assertGreater(coarse, fine)
        self.assertAlmostEqual(coarse / fine, 0.0955 / 0.019, places=2)

    def test_the_real_numbers_land_the_right_way_round(self):
        # lcg_2715, median 0.0955: its longest edge is 0.150 and must survive
        self.assertGreater(stretched_cut.threshold(fan(0.0955, 300)), 0.150)
        # the 2hu squad, median 0.019 with 1.25 m shards: those must not
        self.assertLess(stretched_cut.threshold(fan(0.019, 300)), 1.25)

    def test_a_meshs_own_ordinary_edges_always_survive(self):
        # the invariant. Whatever the span says, a cut below the mesh's own typical
        # edge length is removing geometry, which is the bug being fixed.
        for edge in (0.002, 0.02, 0.0955, 0.2):
            self.assertGreater(stretched_cut.threshold(fan(edge, 200), span=0.05), edge)

    def test_a_shard_is_beyond_it(self):
        # the 2hu case: a 1.25 m edge on a mesh whose median is 1.9 cm
        edges = fan(0.019, 200) + [1.25, 0.9, 1.1]
        self.assertLess(stretched_cut.threshold(edges), 1.25)

    def test_ordinary_geometry_on_a_coarse_mesh_survives(self):
        # lcg_2715: a median of 0.0955 must not be cut at 0.15
        edges = fan(0.0955, 200) + [0.15, 0.148, 0.149]
        self.assertGreater(stretched_cut.threshold(edges), 0.15)

    def test_the_default_ratio_is_the_one_measured(self):
        # 20x the median: well past a coarse mesh's own spread, well under a 65x shard
        self.assertAlmostEqual(stretched_cut.RATIO, 20.0)

    def test_a_mesh_of_one_length_has_a_threshold_above_it(self):
        self.assertGreater(stretched_cut.threshold(fan(0.05, 10)), 0.05)

    def test_no_edges_at_all_cuts_nothing(self):
        self.assertEqual(stretched_cut.threshold([]), 0.0)

    def test_a_zero_median_cuts_nothing(self):
        # a degenerate mesh must not produce a threshold of zero and drop everything
        self.assertEqual(stretched_cut.threshold([0.0, 0.0, 0.0]), 0.0)


class ChoosingFaces(unittest.TestCase):
    """A mesh with a shard in it, sized the way a real one is.

    The proportion matters. A median is robust while shards are rare, and on a real
    model they are: the 2hu squad had six of them in a body of tens of thousands of
    faces. A toy mesh that is one third shards moves its own median far enough to
    hide them, which says nothing about the models this runs on.
    """

    # forty ordinary coarse faces, then one shard reaching across the model
    COARSE = ([((0, 0, 0), (0.1, 0, 0), (0.1, 0, 0.1)),
               ((0, 0, 0), (0.1, 0, 0.1), (0, 0, 0.1))] * 20
              + [((0, 0, 0), (1.4, 0, 0), (0, 0, 1.4))])

    def test_the_shard_goes_and_the_rest_stays(self):
        kept, dropped, cut = stretched_cut.keep(self.COARSE)
        self.assertEqual(dropped, 1)
        self.assertEqual(len(kept), 40)
        self.assertGreater(cut, 0.1)

    def test_a_mesh_with_no_shard_loses_nothing(self):
        kept, dropped, _ = stretched_cut.keep(self.COARSE[:-1])
        self.assertEqual(dropped, 0)
        self.assertEqual(len(kept), 40)

    def test_an_empty_mesh_is_no_work(self):
        self.assertEqual(stretched_cut.keep([]), ([], 0, 0.0))

    def test_a_floor_stops_a_tiny_mesh_being_shredded(self):
        # a mesh of millimetre triangles must not have a 2 cm detail called a shard
        tiny = [((0, 0, 0), (0.001, 0, 0), (0.001, 0, 0.001))] * 40
        tiny.append(((0, 0, 0), (0.02, 0, 0), (0, 0, 0.02)))
        kept, dropped, cut = stretched_cut.keep(tiny)
        self.assertGreaterEqual(cut, stretched_cut.FLOOR)
        self.assertEqual(dropped, 0)

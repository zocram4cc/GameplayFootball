"""Verbatim contract: converted faces == source faces minus dedupe copies
and kit-hidden forms, nothing else. dbg_2009 pole meshes 24/25: 19,808
faces each, 160 genuine 0.79 m shaft triangles cut at a 0.085 limit."""
import unittest
import stretched_cut


class PoleSurvivesVerbatim(unittest.TestCase):
    def test_long_thin_shaft_faces_are_not_shards(self):
        thin = [(0.0, 0.0, 0.0), (0.0, 0.0, 0.79), (0.02, 0.0, 0.0)]
        dense = [(0.0, 0.0, 0.0), (0.02, 0.0, 0.0), (0.0, 0.02, 0.0)]
        kept, dropped, cut = stretched_cut.keep([thin, dense])
        self.assertEqual(dropped, 0)
        self.assertEqual(len(kept), 2)

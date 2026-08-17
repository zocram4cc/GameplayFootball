"""Tests for which UV layout the default body's kit pieces are built against.

The body was originally re-UV'd onto GameplayFootball's own kit template so
that the kit PNGs already in the repo kept working. That made PES's own kit
textures unusable: a team's u0<team>p<n> sheet is laid out for PES's uniform
mapping, so its front panel - crest, sponsor, number - fell in the gap between
the two template panels and was never sampled, while the shirt sampled the
sleeve regions instead. Every imported team hit this, and the synthesised
sleeve flaps distorted the shoulders on top of it.

PES's layout is the standard now, so kit pieces keep the UVs they were
authored with.

Run: python3 -m unittest test_kit_uv -v
"""

import unittest

import pes_base_body


class UV(object):
    """Stand-in for the fmdl library's uv record."""

    def __init__(self, u, v):
        self.u = u
        self.v = v


class SourceUVTest(unittest.TestCase):
    def test_v_is_flipped_from_pes_to_ase(self):
        """PES counts v downward, the ASE upward. Skin pieces already did
        this; kit pieces have to agree or the kit lands upside down."""
        self.assertEqual(pes_base_body.source_uv(UV(0.25, 0.10)), (0.25, 0.90))

    def test_missing_uv_is_the_origin(self):
        self.assertEqual(pes_base_body.source_uv(None), (0.0, 0.0))

    def test_round_trip_is_stable(self):
        u, v = pes_base_body.source_uv(UV(0.4, 0.7))
        self.assertAlmostEqual(u, 0.4)
        self.assertAlmostEqual(v, 0.3)


class KitUVModeTest(unittest.TestCase):
    def test_native_mode_keeps_the_authored_uv(self):
        uv = pes_base_body.kit_vertex_uv("native", UV(0.3, 0.2), lambda: (0.9, 0.9))
        self.assertEqual(uv, (0.3, 0.8))

    def test_template_mode_uses_the_synthesised_uv(self):
        uv = pes_base_body.kit_vertex_uv("template", UV(0.3, 0.2), lambda: (0.9, 0.9))
        self.assertEqual(uv, (0.9, 0.9))

    def test_native_mode_does_not_evaluate_the_synthesiser(self):
        """The template machinery is skipped entirely, not computed and
        discarded - it is the expensive part of the build."""
        def boom():
            raise AssertionError("synthesiser must not run in native mode")
        pes_base_body.kit_vertex_uv("native", UV(0.1, 0.1), boom)

    def test_native_mode_without_an_authored_uv_falls_back(self):
        uv = pes_base_body.kit_vertex_uv("native", None, lambda: (0.5, 0.5))
        self.assertEqual(uv, (0.0, 0.0))


if __name__ == "__main__":
    unittest.main()

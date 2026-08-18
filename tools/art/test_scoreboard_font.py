"""Tests for the scoreboard's own bitmap font.

The PES-styled scoreboard reads a bitmap font - a PNG atlas plus a text metric
file - and until now the only one was exported from PES 2021's own UI, which is
Konami's artwork and cannot live in this repository. This builds an equivalent
from an open typeface instead (Fira Sans Condensed ExtraBold, SIL OFL, shipped
under data/media/fonts), so a fresh checkout draws the same layout with nothing
of Konami's in it.

The format is the one src/utils/gui2/widgets/bitmaptext.cpp parses:

    atlas <png>
    line_height <px>
    ascent <px>
    glyph <codepoint> <x> <y> <w> <h> <bearingX> <bearingY> <advance>

Run: python3 -m unittest test_scoreboard_font -v
"""

import unittest

import make_scoreboard_font as fontgen


class GlyphSet(unittest.TestCase):
    def test_it_covers_what_the_scoreboard_draws(self):
        # digits, the clock's colon, added time's plus, a minus, and a space
        for codepoint in [32, 43, 45, 58] + list(range(48, 58)):
            self.assertIn(codepoint, fontgen.GLYPHS)

    def test_it_matches_the_set_the_pes_export_had(self):
        self.assertEqual(sorted(fontgen.GLYPHS), sorted([32, 43, 45] + list(range(48, 59))))


class Layout(unittest.TestCase):
    def setUp(self):
        self.boxes = {32: (0, 0), 48: (20, 40), 49: (12, 40), 58: (8, 18)}

    def test_every_glyph_gets_a_place_in_the_atlas(self):
        placed, _width, _height = fontgen.pack(self.boxes, padding=1)
        self.assertEqual(sorted(placed), sorted(self.boxes))

    def test_glyphs_do_not_overlap(self):
        placed, _w, _h = fontgen.pack(self.boxes, padding=1)
        rects = [(x, y, self.boxes[c][0], self.boxes[c][1]) for c, (x, y) in placed.items()
                 if self.boxes[c][0] and self.boxes[c][1]]
        for i, a in enumerate(rects):
            for b in rects[i + 1:]:
                apart = (a[0] + a[2] <= b[0] or b[0] + b[2] <= a[0] or
                         a[1] + a[3] <= b[1] or b[1] + b[3] <= a[1])
                self.assertTrue(apart, "%s overlaps %s" % (a, b))

    def test_the_atlas_is_big_enough_for_everything_in_it(self):
        placed, width, height = fontgen.pack(self.boxes, padding=1)
        for codepoint, (x, y) in placed.items():
            w, h = self.boxes[codepoint]
            self.assertLessEqual(x + w, width)
            self.assertLessEqual(y + h, height)

    def test_an_empty_glyph_still_gets_an_entry(self):
        # a space has no pixels but must still carry an advance
        placed, _w, _h = fontgen.pack({32: (0, 0)}, padding=1)
        self.assertIn(32, placed)


class MetricFile(unittest.TestCase):
    def test_it_writes_what_the_engine_parses(self):
        text = fontgen.metrics_text("num_mid.png", line_height=51, ascent=51,
                                    glyphs={48: (1, 1, 20, 40, 2, 40, 24)})
        self.assertIn("atlas num_mid.png", text)
        self.assertIn("line_height 51", text)
        self.assertIn("ascent 51", text)
        self.assertIn("glyph 48 1 1 20 40 2 40 24", text)

    def test_every_glyph_line_carries_all_eight_fields(self):
        text = fontgen.metrics_text("a.png", 10, 10, {48: (0, 0, 1, 1, 0, 1, 2), 32: (0, 0, 0, 0, 0, 0, 5)})
        for line in text.splitlines():
            if line.startswith("glyph"):
                self.assertEqual(len(line.split()), 9, line)


if __name__ == "__main__":
    unittest.main()

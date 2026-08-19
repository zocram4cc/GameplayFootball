"""Tests for importing the 4cc replay wipe.

The mod ships its replay transition as a movie, in 4cc_20_swipe.cpk:

    movie/fade/ACLwipe_hd.usm      1280 x 720, 145 frames at 60 fps
    movie/fade/WEPESwipe_hd.usm    the same, the default slot
    movie/fade/settings.json

They are CRI USM containers and ffmpeg reads them straight - the mod re-encoded
them, so there is no key to find - and each holds *two* video streams of equal
length: stream 0 the colour, stream 1 the matte. That matters. The picture is the
/vg/ Football League crest on black, and treated as opaque the black blacks out the
screen; the matte is what makes it a wipe. It rises from nothing over about ten
frames, holds at fully opaque while the cut happens underneath, and falls away
again, and everything past frame 92 is fully transparent.

settings.json carries the mapping and the timing:

    { "id": -1, "file": "WEPESwipe_hd.usm", "fade": 0, "fadestart": 6 }
    { "id":  7, "file": "ACLwipe_hd.usm",   "fade": 0, "fadestart": 8 }

id is the competition slot, -1 the default, and fadestart the frame on which the
picture underneath is switched.

Run: python3 -m unittest test_import_wipe -v
"""

import unittest

import import_wipe


SETTINGS = """
{
    "wipes" : [
\t\t{ "id": -1,
\t\t  "file": "WEPESwipe_hd.usm",
\t\t  "fade": 0,
\t\t  "fadestart": 6
\t\t},

\t\t{ "id": 7,
\t\t  "file": "ACLwipe_hd.usm",
\t\t  "fade": 0,
\t\t  "fadestart": 8
\t\t},

\t\t{}
\t]
}
"""


class Settings(unittest.TestCase):
    def test_every_wipe_with_a_file_is_read(self):
        wipes = import_wipe.read_settings(SETTINGS)
        self.assertEqual([w["file"] for w in wipes],
                         ["WEPESwipe_hd.usm", "ACLwipe_hd.usm"])

    def test_the_empty_entry_pes_leaves_at_the_end_is_ignored(self):
        # settings.json closes its list with a bare {}
        self.assertEqual(len(import_wipe.read_settings(SETTINGS)), 2)

    def test_the_timing_comes_through_as_numbers(self):
        wipes = import_wipe.read_settings(SETTINGS)
        self.assertEqual(wipes[1]["id"], 7)
        self.assertEqual(wipes[1]["fadestart"], 8)

    def test_rubbish_is_refused_rather_than_half_read(self):
        with self.assertRaises(ValueError):
            import_wipe.read_settings("not json at all")


class ChoosingAWipe(unittest.TestCase):
    def test_a_competition_with_its_own_wipe_gets_it(self):
        wipes = import_wipe.read_settings(SETTINGS)
        self.assertEqual(import_wipe.wipe_for(wipes, 7)["file"], "ACLwipe_hd.usm")

    def test_anything_else_gets_the_default(self):
        wipes = import_wipe.read_settings(SETTINGS)
        for competition in (0, 3, 99):
            self.assertEqual(import_wipe.wipe_for(wipes, competition)["file"],
                             "WEPESwipe_hd.usm")

    def test_with_no_default_the_first_is_used(self):
        wipes = [{"id": 7, "file": "ACLwipe_hd.usm", "fadestart": 8}]
        self.assertEqual(import_wipe.wipe_for(wipes, 3)["file"], "ACLwipe_hd.usm")

    def test_nothing_to_choose_from_is_no_wipe(self):
        self.assertIsNone(import_wipe.wipe_for([], 7))


class TrimmingTheTail(unittest.TestCase):
    """Past the last frame that carries any alpha there is nothing to draw.

    The ACL wipe runs 145 frames and goes fully transparent at 93, so a third of it
    is 1280 x 720 of nothing. Keeping those would cost 50-odd megabytes and a
    third of a second of the engine drawing a transparent quad.
    """

    def test_the_tail_is_dropped(self):
        alpha = [0.03, 0.5, 1.0, 1.0, 0.4, 0.0, 0.0, 0.0]
        self.assertEqual(import_wipe.useful_frames(alpha), 5)

    def test_a_wipe_that_never_fades_keeps_every_frame(self):
        self.assertEqual(import_wipe.useful_frames([1.0, 1.0, 1.0]), 3)

    def test_a_frame_with_a_trace_of_alpha_still_counts(self):
        # the first frame of the ACL wipe measures 0.027 mean and is the fade-in
        self.assertEqual(import_wipe.useful_frames([0.03, 0.0, 0.0]), 1)

    def test_a_wipe_of_nothing_at_all_is_no_frames(self):
        self.assertEqual(import_wipe.useful_frames([0.0, 0.0]), 0)
        self.assertEqual(import_wipe.useful_frames([]), 0)


class WhenItActuallyCovers(unittest.TestCase):
    """The frame the cut can hide behind.

    PES's own "fadestart" is not it. On both of these wipes fadestart is 6 or 8, and
    at frame 6 the matte still has pixels at zero - the crest has swung most of the
    way across but there are gaps, and a cut made there is a cut you can see through.
    The matte does not reach full cover until frame 9.

    So the covering frame is measured rather than taken on faith: the first frame
    whose *least* opaque pixel is opaque.
    """

    def test_the_first_fully_opaque_frame_is_found(self):
        # minimum alpha per frame, as measured off the decoded matte
        self.assertEqual(import_wipe.cover_frame([0.0, 0.0, 0.4, 1.0, 1.0]), 3)

    def test_partial_cover_does_not_count(self):
        # 0.996 is what the real matte reaches, and that counts; 0.9 does not
        self.assertEqual(import_wipe.cover_frame([0.0, 0.9, 0.996]), 2)

    def test_a_wipe_that_never_covers_says_so(self):
        self.assertIsNone(import_wipe.cover_frame([0.0, 0.3, 0.5]))
        self.assertIsNone(import_wipe.cover_frame([]))


class WhereTheCutGoes(unittest.TestCase):
    """Covered is not the same as safely covered.

    The matte first covers every pixel at frame 9, and cutting exactly there leaves
    nothing to spare: the wipe is 60 fps, the game may not be, and a frame either way
    puts the cut back in the gaps. So the cut is held to frame 15, well inside the
    hold, which is a quarter of a second in and still a long way from the crest
    starting to swing away.
    """

    def test_the_cut_is_held_past_the_moment_it_covers(self):
        self.assertEqual(import_wipe.cut_frame(cover=9, frames=92), 15)

    def test_a_late_cover_wins_over_the_hold(self):
        # a wipe that takes longer to close is not cut into early
        self.assertEqual(import_wipe.cut_frame(cover=40, frames=92), 40)

    def test_it_never_runs_past_the_end(self):
        self.assertEqual(import_wipe.cut_frame(cover=9, frames=10), 9)
        self.assertEqual(import_wipe.cut_frame(cover=None, frames=6), 5)

    def test_a_wipe_that_never_covers_still_cuts(self):
        self.assertEqual(import_wipe.cut_frame(cover=None, frames=92), 15)


class Sidecar(unittest.TestCase):
    """What the engine reads instead of a movie file."""

    def test_it_carries_what_playback_needs(self):
        text = import_wipe.sidecar_text(fps=60.0, frames=92, fadestart=8,
                                        source="ACLwipe_hd.usm", cover=9)
        self.assertIn("cut 15", text)
        self.assertIn("fps 60", text)
        self.assertIn("frames 92", text)
        self.assertIn("fadestart 8", text)
        self.assertIn("cover 9", text)
        self.assertIn("ACLwipe_hd.usm", text)

    def test_without_a_covering_frame_it_falls_back_to_pes_own(self):
        text = import_wipe.sidecar_text(60.0, 92, 8, "x.usm", cover=None)
        self.assertIn("cover 8", text)

    def test_every_line_is_one_fact_or_a_comment(self):
        text = import_wipe.sidecar_text(60.0, 92, 8, "x.usm", cover=9)
        for line in text.splitlines():
            if line and not line.startswith("#"):
                self.assertGreaterEqual(len(line.split()), 2)

    def test_a_fadestart_past_the_end_is_clamped_to_it(self):
        # better the cut on the last frame than a cut that never happens
        text = import_wipe.sidecar_text(60.0, 10, 40, "x.usm", cover=40)
        self.assertIn("fadestart 9", text)
        self.assertIn("cover 9", text)

    def test_a_negative_fadestart_lands_on_the_first_frame(self):
        self.assertIn("fadestart 0", import_wipe.sidecar_text(60.0, 10, -3, "x.usm", cover=-5))


if __name__ == "__main__":
    unittest.main()

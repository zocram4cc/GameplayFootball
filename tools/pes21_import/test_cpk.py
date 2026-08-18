"""Tests for the cpk extractor's command line.

The list mode is a read-only look inside an archive, and it was possible to ask
for it in a way that extracted 2.1 GB instead: the second argument is the
destination, so "cpk.py dt15.cpk --list" wrote every file in the archive into a
directory called "--list". A flag in the destination's place is a flag.

Run: python3 -m unittest test_cpk -v
"""

import unittest

import cpk


class CommandLine(unittest.TestCase):
    def test_a_cpk_and_a_destination(self):
        self.assertEqual(cpk.parse_args(["a.cpk", "out"]),
                         ("a.cpk", "out", False, None))

    def test_listing_does_not_take_the_flag_for_a_destination(self):
        # this is the one that extracted an archive into ./--list
        self.assertEqual(cpk.parse_args(["a.cpk", "--list"]),
                         ("a.cpk", None, True, None))

    def test_listing_after_a_destination_still_lists(self):
        self.assertEqual(cpk.parse_args(["a.cpk", "out", "--list"]),
                         ("a.cpk", "out", True, None))

    def test_a_filter_is_read_wherever_it_sits(self):
        # still an extraction, so it still goes to the default destination
        self.assertEqual(cpk.parse_args(["a.cpk", "--filter=st019"]),
                         ("a.cpk", "extracted", False, "st019"))
        self.assertEqual(cpk.parse_args(["a.cpk", "out", "--filter=st019"]),
                         ("a.cpk", "out", False, "st019"))

    def test_no_destination_and_no_flags_extracts_where_it_always_did(self):
        self.assertEqual(cpk.parse_args(["a.cpk"]), ("a.cpk", "extracted", False, None))

    def test_a_listing_never_needs_a_destination(self):
        # nothing is written, so nothing should be created either
        _cpk, dest, list_only, _pattern = cpk.parse_args(["a.cpk", "--list"])
        self.assertTrue(list_only)
        self.assertIsNone(dest)


if __name__ == "__main__":
    unittest.main()

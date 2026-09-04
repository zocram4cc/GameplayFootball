import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import reweld_installed


class WeldsASidecarInPlace(unittest.TestCase):
    """Two vertices at the same millimetre, weighted differently.

    This is the shard defect as it sits in an installed model: lcg_2709's
    v16033/v16034 are the same point of the neck and carried
    `right_shoulder 0.29` against `right_clavicle 0.29`, so the bake moved one
    0.15 m and left the other.
    """

    SIDECAR = ("# vertex weights\n"
               "0.100000 0.200000 2.200000 5:0.400000 3:0.600000\n"
               "0.100000 0.200000 2.200000 5:0.400000 7:0.600000\n"
               "1.000000 0.000000 0.000000 9:1.000000\n")

    def test_the_duplicates_agree_afterwards(self):
        welded, changed = reweld_installed.weld_text(self.SIDECAR)
        lines = welded.splitlines()
        self.assertEqual(lines[1].split()[3:], lines[2].split()[3:])
        self.assertEqual(changed, 2)

    def test_the_position_text_is_written_back_verbatim(self):
        # The engine finds a weight by float equality on the position
        # (SkinWeights::Find), so a reprint is a chance to change it.
        welded, _ = reweld_installed.weld_text(self.SIDECAR)
        for line in welded.splitlines()[1:]:
            self.assertEqual(len(line.split()[0].split(".")[1]), 6)
        self.assertIn("0.100000 0.200000 2.200000", welded)
        self.assertIn("1.000000 0.000000 0.000000", welded)

    def test_a_lone_vertex_keeps_its_own_weights(self):
        welded, _ = reweld_installed.weld_text(self.SIDECAR)
        self.assertEqual(welded.splitlines()[3].split()[3:], ["9:1.000000"])

    def test_running_it_twice_changes_nothing(self):
        once, first = reweld_installed.weld_text(self.SIDECAR)
        twice, second = reweld_installed.weld_text(once)
        self.assertEqual(once, twice)
        self.assertEqual(second, 0)
        self.assertGreater(first, 0)

    def test_the_header_survives(self):
        welded, _ = reweld_installed.weld_text(self.SIDECAR)
        self.assertTrue(welded.startswith("# vertex weights\n"))


if __name__ == "__main__":
    unittest.main()

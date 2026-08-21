"""Tests for what an imported export is allowed to become.

A 4cc pack's per-player export is not always a body. The packs override PES's
boots, glove and face slots, and PES draws those *in addition to* its own body
wearing the team's kit texture - it does not replace it. 2HUG's kit textures are
DXT1, which carries no alpha, so the invisible-kit trick is not in play there:
the character is the kit on the standard body and the boots export is a prop.

Bound as a whole body instead, a prop is all you get. That is what put a fan of
wing blades and a headless torso on the pitch: lcg_2702 is `boots` plus `wings`,
2hug_1851 is a single `medical_c` mesh, and neither has a player in it.

So an export that does not clothe the rig is not bound at all, and the player
keeps the stock body the kit is swapped into (Team::FetchKit). body_coverage
decides it, by asking whether there is geometry near each of the rig's joints.

Run: python3 -m unittest test_import_team -v
"""

import unittest

import import_team


class WhichExportsMayBeBoundAsABody(unittest.TestCase):
    def test_a_whole_body_is_bound(self):
        self.assertTrue(import_team.may_bind_as_body("whole"))

    def test_a_slot_override_is_not(self):
        # a prop drawn over PES's own body, not a replacement for it
        self.assertFalse(import_team.may_bind_as_body("needs base"))

    def test_an_export_carrying_scenery_is_not(self):
        # lcg_2718's backdrop reaches 362 m and frames the player down to a dot
        self.assertFalse(import_team.may_bind_as_body("carries scenery"))

    def test_an_unknown_verdict_is_refused_rather_than_guessed(self):
        self.assertFalse(import_team.may_bind_as_body(""))
        self.assertFalse(import_team.may_bind_as_body("something new"))

    def test_a_composited_export_is_bound_whatever_its_own_coverage(self):
        """--base puts the stock body underneath, so the result does clothe the rig."""
        self.assertTrue(import_team.may_bind_as_body("needs base", composited=True))
        self.assertTrue(import_team.may_bind_as_body("carries scenery", composited=True))


if __name__ == "__main__":
    unittest.main()

"""Tests for installing PES's celebration clips.

A PES celebration is two clips: an intro, and a loop the scorer holds until the
celebration is over. We import both - 66 loop clips sit in the import pack - and used
to install only the intros, so a scorer played 1.2 seconds and then fell back to
whatever his controller did next. That is why the celebration read as unfinished
however long kCelebration_ms was.

The engine picks a special animation by matching specialvar1/specialvar2, so the loop
needs its own pair: the controller asks for the intro, then asks for the loop
(GoalCelebration::Phase).

Run: python3 -m unittest test_install_anims -v
"""

import os
import unittest

import install_anims


class TellingALoopFromItsIntro(unittest.TestCase):
    def test_a_loop_is_recognised_by_its_name(self):
        self.assertTrue(install_anims.is_loop("dm_goal_archer_loop.anim"))
        self.assertTrue(install_anims.is_loop("/a/b/dm_goal_Wide_sliding01_loop.anim"))

    def test_an_intro_is_not(self):
        self.assertFalse(install_anims.is_loop("dm_goal_archer.anim"))
        self.assertFalse(install_anims.is_loop("dm_goal_looper.anim"))

    def test_the_intro_a_loop_belongs_to(self):
        self.assertEqual(install_anims.intro_of("dm_goal_archer_loop.anim"),
                         "dm_goal_archer.anim")
        self.assertIsNone(install_anims.intro_of("dm_goal_archer.anim"))


class TheClassALoopIsInstalledAs(unittest.TestCase):
    """Same folder as its intro, its own specialvars so it can be asked for."""

    def test_every_celebration_class_has_a_loop_class(self):
        for name in ("happy_normal", "happy_extreme", "sad_normal"):
            self.assertIn(name + "_loop", install_anims.CLASSES)

    def test_a_loop_lands_beside_its_intro(self):
        for name in ("happy_normal", "happy_extreme", "sad_normal"):
            self.assertEqual(install_anims.CLASSES[name + "_loop"][0],
                             install_anims.CLASSES[name][0])

    def test_a_loop_is_asked_for_by_its_own_variable(self):
        for name in ("happy_normal", "happy_extreme", "sad_normal"):
            intro_vars = install_anims.CLASSES[name][1]
            loop_vars = install_anims.CLASSES[name + "_loop"][1]
            self.assertEqual(loop_vars["specialvar1"], intro_vars["specialvar1"])
            self.assertNotEqual(loop_vars["specialvar2"], intro_vars["specialvar2"])


if __name__ == "__main__":
    unittest.main()

class TheTrapClass(unittest.TestCase):
    """Trap is the receive family: the dominant pass-failure sink (62% of
    logged failures) runs through it, and the stock set is 40 clips across
    three velocity buckets. The imported batch competes for first refusal
    like sliding/interfere do, with the stock clip falling back whenever an
    imported one cannot reach the ball."""

    POOL = os.path.join("..", "..", "data", "imports", "pes21", "animations")

    def a_pool_trap_clip(self):
        if not os.path.isdir(self.POOL):
            self.skipTest("import pool not present")
        for root, _, files in os.walk(self.POOL):
            for f in files:
                if "trap" in f and f.endswith(".anim"):
                    return os.path.join(root, f)
        self.skipTest("no trap clips in pool")

    def test_trap_is_a_match_class(self):
        self.assertIn("trap", install_anims.MATCH_CLASSES)
        self.assertIn("trap", sorted(set(install_anims.CLASSES) | set(install_anims.MATCH_CLASSES)))

    def test_an_upright_trap_installs_into_the_trap_family(self):
        subdir, stem, text = install_anims.prepare_match_anim(
            self.a_pool_trap_clip(), "trap")
        self.assertIsNotNone(subdir)
        parts = subdir.split(os.sep)
        self.assertEqual(parts[0], "trap")
        self.assertIn(parts[1], install_anims.VELOCITY_DIRS)
        self.assertEqual(parts[2], "pes")
        self.assertIn("<type>\n\ttrap\n</type>", text)
        self.assertIn("<incomingballdirection>", text)
        self.assertIn("extension,football,", text)

    def test_a_trap_that_ends_on_the_deck_is_rejected(self):
        # fk() adds a constant body height, so a prone fixture cannot be
        # built by mutating curves; patch the lie detector instead - the
        # contract under test is the rejection branch, not the detector.
        clip = self.a_pool_trap_clip()
        real_lie = install_anims.am.lie_state
        install_anims.am.lie_state = lambda anim: "lay_back"
        try:
            subdir, stem, reason = install_anims.prepare_match_anim(clip, "trap")
        finally:
            install_anims.am.lie_state = real_lie
        self.assertIsNone(subdir)
        self.assertIn("ground", reason)


class ACelebrationMustCarryItsOwnTravel(unittest.TestCase):
    """PES plays a celebration on the scorer wherever he is, so the clip's own
    root track is the only thing that can carry the run, the dive or the slide.
    entrance_pl.py strips the root from every fixdemo clip it converts, which is
    right for an entrance - a .chor holds the world track there - and wrong for
    a celebration, which has no .chor. Installed that way, 292 of the 293
    celebrations PES ships performed on the spot."""

    def test_a_stripped_player_track_is_recognised(self):
        stripped = ["player,0,0.000000,0.000000,0.000035,"
                    "2,0.000000,0.000000,-0.000859"]
        self.assertTrue(install_anims.root_is_stripped(stripped))

    def test_a_travelling_player_track_is_not(self):
        moving = ["player,0,0.000000,0.000000,0.000035,"
                  "2,-0.001200,-0.014300,-0.000859"]
        self.assertFalse(install_anims.root_is_stripped(moving))

    def test_vertical_motion_alone_is_still_stripped(self):
        # z survives --strip-root; only x and y are zeroed, so height on its own
        # must not be mistaken for travel
        vertical = ["player,0,0.000000,0.000000,0.104000,"
                    "2,0.000000,0.000000,-0.047000"]
        self.assertTrue(install_anims.root_is_stripped(vertical))

    def test_the_celebration_classes_are_the_ones_without_choreography(self):
        for name in install_anims.CELEBRATION_CLASSES:
            self.assertTrue(name.startswith(("happy_", "sad_")), name)
        self.assertNotIn("entrance_lineup", install_anims.CELEBRATION_CLASSES)
        self.assertIn("happy_extreme", install_anims.CELEBRATION_CLASSES)

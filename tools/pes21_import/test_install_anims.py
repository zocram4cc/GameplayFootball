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

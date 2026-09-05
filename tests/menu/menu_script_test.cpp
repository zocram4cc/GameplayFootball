#include "menu/menuscript.hpp"

#include <map>

#include <gtest/gtest.h>

using MenuScript::Action;
using MenuScript::Key;
using MenuScript::Parse;

TEST(MenuScriptTest, EmptySpecParsesToNoSteps) {
  EXPECT_TRUE(Parse("").empty());
  EXPECT_TRUE(Parse("   ").empty());
}

TEST(MenuScriptTest, ParsesEveryKeyWord) {
  const auto steps = Parse("0:up;10:down;20:left;30:right;40:enter;50:escape;60:x");
  ASSERT_EQ(steps.size(), 7u);
  const Key expected[] = {Key::Up, Key::Down, Key::Left, Key::Right,
                          Key::Enter, Key::Escape, Key::X};
  for (size_t i = 0; i < steps.size(); i++) {
    EXPECT_EQ(steps[i].action, Action::Tap);
    EXPECT_EQ(steps[i].key, expected[i]);
    EXPECT_EQ(steps[i].at_ms, i * 10ul);
  }
}

TEST(MenuScriptTest, ParsesShotWithName) {
  const auto steps = Parse("500:shot=plan_selected");
  ASSERT_EQ(steps.size(), 1u);
  EXPECT_EQ(steps[0].action, Action::Shot);
  EXPECT_EQ(steps[0].name, "plan_selected");
  EXPECT_EQ(steps[0].at_ms, 500ul);
}

TEST(MenuScriptTest, ParsesQuit) {
  const auto steps = Parse("999:quit");
  ASSERT_EQ(steps.size(), 1u);
  EXPECT_EQ(steps[0].action, Action::Quit);
}

TEST(MenuScriptTest, StepsComeBackSortedByTime) {
  const auto steps = Parse("300:enter;100:up;200:down");
  ASSERT_EQ(steps.size(), 3u);
  EXPECT_EQ(steps[0].at_ms, 100ul);
  EXPECT_EQ(steps[1].at_ms, 200ul);
  EXPECT_EQ(steps[2].at_ms, 300ul);
}

TEST(MenuScriptTest, TiesKeepWrittenOrder) {
  const auto steps = Parse("100:up;100:down;100:left");
  ASSERT_EQ(steps.size(), 3u);
  EXPECT_EQ(steps[0].key, Key::Up);
  EXPECT_EQ(steps[1].key, Key::Down);
  EXPECT_EQ(steps[2].key, Key::Left);
}

TEST(MenuScriptTest, DropsOnlyTheMalformedStep) {
  // "banana" is not a time, "50:nonsense" names no action, "shot=" is a shot
  // with no name - each should cost exactly its own step.
  const auto steps = Parse("banana:up;50:nonsense;60:shot=;100:enter");
  ASSERT_EQ(steps.size(), 1u);
  EXPECT_EQ(steps[0].action, Action::Tap);
  EXPECT_EQ(steps[0].key, Key::Enter);
}

TEST(MenuScriptTest, ToleratesWhitespaceAndTrailingSemicolon) {
  const auto steps = Parse(" 10 : up ; 20:down; ");
  ASSERT_EQ(steps.size(), 2u);
  EXPECT_EQ(steps[0].key, Key::Up);
  EXPECT_EQ(steps[1].key, Key::Down);
}

// --- the monkey ------------------------------------------------------------
//
// Random input is the only way a screen gets tested against sequences nobody
// would script: grab, half-drag, open a submenu mid-drag, escape out of it,
// drop on top of somebody. The owner asked for exactly this ("a monkey with a
// typewriter-type test: this should not break under any circumstances"), so
// the driver has to be reproducible: a seed and a tap index name one key.

TEST(MenuScriptMonkeyTest, ParsesSeedAndCount) {
  const auto steps = MenuScript::Parse("500:monkey=7:2500;9000:quit");
  ASSERT_EQ(steps.size(), 2u);
  EXPECT_EQ(steps.at(0).action, MenuScript::Action::Monkey);
  EXPECT_EQ(steps.at(0).seed, 7u);
  EXPECT_EQ(steps.at(0).taps, 2500u);
  EXPECT_EQ(steps.at(1).action, MenuScript::Action::Quit);
}

TEST(MenuScriptMonkeyTest, AMalformedMonkeyCostsOnlyItsOwnStep) {
  const auto steps = MenuScript::Parse("100:monkey=7;200:monkey=:5;300:monkey=1:0;400:down");
  ASSERT_EQ(steps.size(), 1u);
  EXPECT_EQ(steps.at(0).action, MenuScript::Action::Tap);
}

TEST(MenuScriptMonkeyTest, TheSameSeedGivesTheSameSequence) {
  for (unsigned long n = 0; n < 64; n++) {
    EXPECT_EQ(MenuScript::MonkeyKey(11, n), MenuScript::MonkeyKey(11, n));
  }
  bool anyDifferent = false;
  for (unsigned long n = 0; n < 64; n++) {
    if (MenuScript::MonkeyKey(11, n) != MenuScript::MonkeyKey(12, n)) anyDifferent = true;
  }
  EXPECT_TRUE(anyDifferent) << "two seeds produced identical streams";
}

TEST(MenuScriptMonkeyTest, EveryKeyIsReachedAndNothingElseIs) {
  std::map<MenuScript::Key, int> seen;
  for (unsigned long n = 0; n < 20000; n++) seen[MenuScript::MonkeyKey(3, n)]++;
  // All eight keys, none of them vanishingly rare: a monkey that never presses
  // escape never abandons a drag, and one that never presses the secondary
  // button never toggles a role on a card.
  EXPECT_EQ(seen.size(), 8u);
  for (const auto& entry : seen) EXPECT_GT(entry.second, 500) << "key starved";
}

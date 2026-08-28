#include "menu/menuscript.hpp"

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

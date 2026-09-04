// Scripted keyboard input for headless menu runs ("menu_smoke_script").
//
// The menu smoke configs could open a page and photograph it, but nothing
// could press a key. Anything that only exists after input - the game plan
// pitch's grab/move/drop, a submenu opened with 'x', a selector stepped with
// left/right - therefore had no way of being seen, and shipped unverified.
//
// A script is a timeline of key taps and screenshots, relative to the moment
// the menu task first ticks it:
//
//   "menu_smoke_script" "1200:shot=plan;1500:left;1800:enter;2400:quit"
//
// The keys are pushed into UserEventManager, which is exactly where a real
// keyboard arrives, so a scripted run exercises the same
// guitask -> windowing event -> focused widget path as a human does. Parsing
// is kept here, free of SDL and of the engine, so it can be unit-tested.

#ifndef _HPP_MENU_MENUSCRIPT
#define _HPP_MENU_MENUSCRIPT

#include <string>
#include <vector>

namespace MenuScript {

// The keys the menus actually navigate with: four directions, confirm,
// cancel, and the 'x' the game plan pitch opens its role submenu on.
enum class Key { Up, Down, Left, Right, Enter, Escape, X };

enum class Action { Tap, Shot, Quit, Monkey };

struct Step {
  unsigned long at_ms = 0;
  Action action = Action::Tap;
  Key key = Key::Up;   // Tap only
  std::string name;    // Shot only: the screenshot's suffix
  // Monkey only: a deterministic stream of random keys, which is how a screen
  // gets tested against input nobody would think to script. "monkey=7:5000"
  // presses 5000 keys from seed 7, one per driver tick, then stops - and the
  // seed makes any crash it finds replayable.
  unsigned long seed = 0;
  unsigned long taps = 0;
};

// The n'th key of a monkey run. Pure so the sequence can be reasoned about and
// tested without the engine; the same (seed, n) always gives the same key.
Key MonkeyKey(unsigned long seed, unsigned long n);

// Parses the timeline. Steps come back sorted by time, ties in the order
// written. A step whose time is not a number, or whose action is not one of
// the words above, is dropped - one typo should cost its own step, not
// silently shift every step after it.
std::vector<Step> Parse(const std::string& spec);

}  // namespace MenuScript

#endif

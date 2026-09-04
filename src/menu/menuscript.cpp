#include "menuscript.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace MenuScript {

namespace {

std::string Trim(const std::string& s) {
  size_t begin = s.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(begin, end - begin + 1);
}

bool ParseKey(const std::string& word, Key* key) {
  if (word == "up") { *key = Key::Up; return true; }
  if (word == "down") { *key = Key::Down; return true; }
  if (word == "left") { *key = Key::Left; return true; }
  if (word == "right") { *key = Key::Right; return true; }
  if (word == "enter") { *key = Key::Enter; return true; }
  if (word == "escape") { *key = Key::Escape; return true; }
  if (word == "x") { *key = Key::X; return true; }
  return false;
}

// Every character of `s` is a digit; empty is not a number.
bool IsUnsignedInteger(const std::string& s) {
  if (s.empty()) return false;
  return std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit((unsigned char)c); });
}

}  // namespace

Key MonkeyKey(unsigned long seed, unsigned long n) {
  // splitmix64 on (seed, n): no state, uniform enough for input fuzzing, and
  // identical on every machine - a reproduction is a seed and an index.
  unsigned long long x = 0x9e3779b97f4a7c15ULL * (n + 1) + seed;
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  // Weighted: mostly movement and confirm, because that is what a drag is
  // made of, with escape and 'x' often enough to open and abandon submenus
  // in the middle of one.
  static const Key table[] = {
      Key::Up,    Key::Up,    Key::Down,  Key::Down, Key::Left,  Key::Left,
      Key::Right, Key::Right, Key::Enter, Key::Enter, Key::Enter, Key::X,
      Key::Escape,
  };
  return table[x % (sizeof(table) / sizeof(table[0]))];
}

std::vector<Step> Parse(const std::string& spec) {
  std::vector<Step> steps;

  size_t pos = 0;
  while (pos <= spec.size()) {
    size_t semi = spec.find(';', pos);
    const std::string token = Trim(spec.substr(pos, semi == std::string::npos ? std::string::npos
                                                                              : semi - pos));
    pos = (semi == std::string::npos) ? spec.size() + 1 : semi + 1;

    if (token.empty()) continue;

    const size_t colon = token.find(':');
    if (colon == std::string::npos) continue;
    const std::string timePart = Trim(token.substr(0, colon));
    const std::string actionPart = Trim(token.substr(colon + 1));
    if (!IsUnsignedInteger(timePart) || actionPart.empty()) continue;

    Step step;
    step.at_ms = std::strtoul(timePart.c_str(), nullptr, 10);

    if (actionPart == "quit") {
      step.action = Action::Quit;
      steps.push_back(step);
      continue;
    }

    if (actionPart.rfind("monkey=", 0) == 0) {
      // monkey=<seed>:<taps>
      const std::string rest = actionPart.substr(7);
      const size_t inner = rest.find(':');
      if (inner == std::string::npos) continue;
      const std::string seedPart = Trim(rest.substr(0, inner));
      const std::string tapsPart = Trim(rest.substr(inner + 1));
      if (!IsUnsignedInteger(seedPart) || !IsUnsignedInteger(tapsPart)) continue;
      step.action = Action::Monkey;
      step.seed = std::strtoul(seedPart.c_str(), nullptr, 10);
      step.taps = std::strtoul(tapsPart.c_str(), nullptr, 10);
      if (step.taps == 0) continue;
      steps.push_back(step);
      continue;
    }

    if (actionPart.rfind("shot=", 0) == 0) {
      const std::string name = actionPart.substr(5);
      if (name.empty()) continue;
      step.action = Action::Shot;
      step.name = name;
      steps.push_back(step);
      continue;
    }

    Key key;
    if (ParseKey(actionPart, &key)) {
      step.action = Action::Tap;
      step.key = key;
      steps.push_back(step);
    }
  }

  std::stable_sort(steps.begin(), steps.end(),
                    [](const Step& a, const Step& b) { return a.at_ms < b.at_ms; });
  return steps;
}

}  // namespace MenuScript

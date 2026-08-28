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

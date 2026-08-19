#include "goalcelebration.hpp"

#include <cstdlib>
#include <sstream>

namespace GoalCelebration {

std::vector<Celebration> Parse(const std::string& text) {
  std::vector<Celebration> set;
  Celebration current;
  bool open = false;

  auto flush = [&set, &current, &open]() {
    // A stanza with no performance is not a celebration: it would leave the scorer
    // standing in his idle pose while a camera swept past him.
    if (open && !current.clip.empty()) set.push_back(current);
    current = Celebration();
    open = false;
  };

  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string key;
    if (!(fields >> key) || key.empty() || key[0] == '#') continue;
    std::string value;
    if (!(fields >> value)) continue;
    if (key == "celebration") {
      flush();
      current.name = value;
      open = true;
    } else if (key == "clip" && open) {
      current.clip = value;
    } else if (key == "var" && open) {
      current.var = std::atoi(value.c_str());
    } else if (key == "camera" && open) {
      current.cameras.push_back(value);
    }
  }
  flush();
  return set;
}

int Choose(const std::vector<Celebration>& set, const std::string& assigned, int seed) {
  if (set.empty()) return -1;

  if (!assigned.empty()) {
    for (size_t i = 0; i < set.size(); i++)
      if (set[i].name == assigned) return (int)i;
    // Assigned something we do not ship: fall through to the draw rather than
    // refusing to celebrate.
  }

  std::vector<int> filmed;
  for (size_t i = 0; i < set.size(); i++)
    if (!set[i].cameras.empty()) filmed.push_back((int)i);
  if (filmed.empty()) return (int)(((unsigned)seed) % set.size());
  return filmed[((unsigned)seed) % filmed.size()];
}

std::string PickCamera(const Celebration& celebration, int attackingSide, int seed) {
  if (celebration.cameras.empty()) return "";
  if (celebration.cameras.size() == 1) return celebration.cameras[0];

  // PES names them _Z_fromL and _Z_fromR; take the one on the side being attacked.
  const char* wanted = attackingSide >= 0 ? "_Z_fromR" : "_Z_fromL";
  for (const std::string& camera : celebration.cameras) {
    const size_t at = camera.rfind(wanted);
    if (at != std::string::npos && at + std::string(wanted).size() == camera.size())
      return camera;
  }
  return celebration.cameras[((unsigned)seed) % celebration.cameras.size()];
}

Phase_e Phase(unsigned long elapsed_ms, unsigned long introHold_ms) {
  return elapsed_ms >= introHold_ms ? e_Loop : e_Intro;
}

int LoopVariable(int introVariable) { return introVariable + 10; }

}  // namespace GoalCelebration

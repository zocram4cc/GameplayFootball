#include "goalcelebration.hpp"

#include <cmath>
#include "goalsequence.hpp"

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

void RunTarget(float fromX, float fromY, int attackedSide, int nearSide, float pitchHalfW,
               float pitchHalfH, float* outX, float* outY) {
  // The corner of the half he scored in, pulled inside the lines so he ends up on the
  // grass in front of the crowd rather than inside the corner flag.
  const float cornerX = (pitchHalfW - kGoalLineInset_m) * static_cast<float>(attackedSide >= 0 ? 1 : -1);
  const float cornerY = (pitchHalfH - kTouchlineInset_m) * static_cast<float>(nearSide >= 0 ? 1 : -1);
  const float dx = cornerX - fromX, dy = cornerY - fromY;
  const float distance = std::sqrt(dx * dx + dy * dy);
  if (distance <= kMaxRun_m || distance < 1e-4f) {
    // Near enough already: he runs the rest of the way rather than past it.
    *outX = cornerX;
    *outY = cornerY;
    return;
  }
  const float t = kMaxRun_m / distance;
  *outX = fromX + dx * t;
  *outY = fromY + dy * t;
}

bool HasArrived(float distanceToTarget_m, unsigned long waited_ms) {
  return distanceToTarget_m <= kArrivalRadius_m || waited_ms >= kApproachCap_ms;
}

int SeedFor(int databaseID) {
  // A cheap avalanche so neighbouring ids do not land on neighbouring celebrations:
  // squad numbers are consecutive, and consecutive draws would give a whole back four
  // the same three performances.
  unsigned int x = static_cast<unsigned int>(databaseID) * 2654435761u;
  x ^= x >> 15;
  x ^= x >> 13;
  return static_cast<int>(x & 0x7fffffffu);
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

unsigned long ClipLength_ms(int frames) {
  return frames > 0 ? static_cast<unsigned long>(frames) * kFrameLength_ms : 0;
}

unsigned long IntroHold_ms(int introFrames) {
  const unsigned long length = ClipLength_ms(introFrames);
  return length > 0 ? length : kIntroHold_ms;
}

unsigned long CelebrationTotal_ms(int introFrames, int loopFrames) {
  const unsigned long intro = IntroHold_ms(introFrames);
  const unsigned long loop = ClipLength_ms(loopFrames);
  // With no loop to measure, the sequence's own default decides how long the
  // performance runs - but never less than the intro needs.
  const unsigned long total = loop > 0 ? intro + loop
                                       : (intro > GoalSequence::kCelebration_ms
                                              ? intro
                                              : GoalSequence::kCelebration_ms);
  return total > GoalSequence::kLongestCelebration_ms ? GoalSequence::kLongestCelebration_ms
                                                      : total;
}

bool IsPerforming(unsigned long sinceGoal_ms, unsigned long celebrationLength_ms) {
  if (sinceGoal_ms < kReactionDelay_ms)
    return false;
  const unsigned long floor = kReactionDelay_ms + kMinimumPerformance_ms;
  const unsigned long until = celebrationLength_ms > floor ? celebrationLength_ms : floor;
  return sinceGoal_ms < until;
}

}  // namespace GoalCelebration

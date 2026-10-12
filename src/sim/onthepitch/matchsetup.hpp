// What a headless match is played with: who, how long, on which seed, with
// which animation library and skeleton. Replaces GF's menu-driven setup.

#ifndef _HPP_ONTHEPITCH_MATCHSETUP
#define _HPP_ONTHEPITCH_MATCHSETUP

#include <string>

namespace pesrig {
struct Rig;
}

struct MatchSetup {
  int homeTeamDbId = 0;
  int awayTeamDbId = 0;
  unsigned int seed = 0;
  int durationMinutes = 0;
  float timeScale = 1.0f;
  // Whether the CPU manager adapts each team's tactics and makes its
  // substitutions; off, the team plays its set tactics and bench all match.
  bool aiManagerRuns[2] = {true, true};
  std::string animationDir;
  // The player skeleton, built from the user's PES install. Not owned; must
  // outlive the Match.
  const pesrig::Rig* rig = nullptr;
};

#endif

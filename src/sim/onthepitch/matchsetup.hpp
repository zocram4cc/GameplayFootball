// What a headless match is played with: who, how long, on which seed, with
// which animation library and skeleton. Replaces GF's menu-driven setup.

#ifndef _HPP_ONTHEPITCH_MATCHSETUP
#define _HPP_ONTHEPITCH_MATCHSETUP

#include <string>

namespace pesrig {
struct Rig;
}

// PES 2021's match clock (docs/re/pes-re-tactics-clock.md): its constants are
// read from the install's PES2021.exe by the caller and handed to the Match.
#include "pesclock.hpp"
// PES 2021's ball flight and ground contact
// (docs/re/pes-re-ball-contact.md, docs/re/pes-re-officials-ball.md 6.3): its
// constants come from the install's constant_match.bin `ball.o` and the exe
// image, read by the caller and handed to the Match the same way.
#include "pesball.hpp"
#include "pesjudge.hpp"
#include "peskeeper.hpp"
#include "pesstoppage.hpp"
#include "pesrandom.hpp"
#include "pesstamina.hpp"

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
  // PES 2021's match clock (docs/re/pes-re-tactics-clock.md): the constants the
  // clock uses, every one read from the install's PES2021.exe by the caller.
  // Required: the sim cannot run without the user's PES install (the player
  // skeleton is built from it, and there is no fallback rig), so a match built
  // without them is a caller error and the Match construction refuses it.
  const pes::PesClockConstants* pesClockConstants = nullptr;
  // PES 2021's ball physics (docs/re/pes-re-ball-contact.md 6): the `ball.o`
  // record of the install's constant_match.bin plus the image constants, read by
  // the caller. Required for the same reason as the clock: the ball's flight,
  // bounce and rolling are PES's, and there is no GF fallback to invent.
  const pes::PesBallConstants* pesBallConstants = nullptr;
  // PES 2021's foul decision (docs/re/pes-re-duels-fouls.md 2): the Judge's
  // point weights and thresholds, read from the install's exe. Required: the
  // referee scores every contact with them.
  const pes::JudgeConstants* pesJudgeConstants = nullptr;
  // PES 2021's restart timings (docs/re/pes-re-foul-choreography.md 9): the
  // foul/offside holds and the beats between the stop and the kick. Required:
  // they are the length of every stoppage.
  const pes::StoppageConstants* pesStoppageConstants = nullptr;
  // PES 2021's generator (docs/re/pes-re-rng.md 1): every random draw of the
  // match comes from it, seeded at kick-off from `seed`. Required.
  const pes::RngConstants* pesRngConstants = nullptr;
  // PES 2021's goalkeeper deflect point and post-catch distribution target
  // (docs/re/pes-re-keeper-port.md). Required.
  const pes::KeeperConstants* pesKeeperConstants = nullptr;
  // PES 2021's stamina, fatigue and injury model
  // (docs/re/pes-re-stamina-drain.md): the tank, the drain, the breath, the
  // break bonus and the injury roll's constants, read from the install's exe.
  // Required like the others: the model's every number lives in the image.
  const pes::StaminaConstants* pesStaminaConstants = nullptr;
};

#endif

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_STRATEGY_GOALIE_DEFAULT
#define _HPP_STRATEGY_GOALIE_DEFAULT

#include <memory>

#include "../strategy.hpp"
#include "onthepitch/pesrandom.hpp"

class GoalieDefaultStrategy : public Strategy {
public:
  GoalieDefaultStrategy(ElizaController* controller);
  virtual ~GoalieDefaultStrategy();

  virtual void RequestInput(const MentalImage* mentalImage, Vector3& direction, float& velocity);
  void CalculateIfBallIsBoundForGoal(const MentalImage* mentalImage);
  bool IsBallBoundForGoal() const { return ballBoundForGoal; }

protected:
  // Where a ball on its way in is met (the bound-for-goal intercept, kept from
  // this engine's own shooter response).
  Vector3 InterceptTarget(const MentalImage* mentalImage);

  // PES 2021's positioning shuffle (docs/re/pes-re-goalkeeper.md 10.2.2) draws
  // Range0(6) on slot 1 of a per-clip generator at `anim+0x19c0` (rng-slots
  // report §13). GF has no per-clip generator, so each keeper holds one Random
  // of its own, seeded once from the match's generator constants folded with
  // its own player id, and draws slot 1 on it.
  // ponytail: this is a stand-in for the per-clip instance PES keeps; the slot
  // and the draw are PES's own, but the stream is per-keeper here, not per
  // animation clip. A keeper's shuffle is lateral noise under 0.6 m, so the
  // divergence from PES's exact stream cannot change a decision.
  std::unique_ptr<pes::Random> keeperShuffle;
  int KeeperShuffleDraw();

  bool ballBoundForGoal;
  float ballBoundForGoal_ycoord;
};

#endif

// Penalty shootout: a stat-driven resolution ("dice roll") plus the shootout
// state machine. Outcomes are decided from player attributes and caller-supplied
// random samples, so the visuals can simply play the animation that matches.
// See TECHNICAL_ROADMAP.md section 3C.

#ifndef _HPP_PENALTY_SHOOTOUT
#define _HPP_PENALTY_SHOOTOUT

#include <vector>

namespace PenaltyShootout {

enum e_Phase {
  e_Phase_Positioning = 0,
  e_Phase_Execution,
  e_Phase_Resolution,
  e_Phase_Finished,
};

enum e_Outcome {
  e_Outcome_Pending = 0,
  e_Outcome_Goal,
  e_Outcome_Save,
  e_Outcome_Miss,
};

struct Shooter {
  float vision = 0.5f;  // mental_vision
  float shot = 0.5f;    // technical_shot
};

struct Keeper {
  float reaction = 0.5f;              // physical_reaction
  float defensivePositioning = 0.5f;  // mental_defensivepositioning
};

// Point the shooter is aiming at, relative to the centre of the goal line:
// x is lateral (positive to the shooter's right), z is height.
struct Aim {
  float x = 0.0f;
  float z = 0.0f;
};

// Regulation kicks per team before sudden death.
const int regulationKicks = 5;
// Height the ball is aimed at with a neutral sample.
const float aimCentreHeight = 1.0f;
// Best case lateral spread of the accuracy cone, in metres.
const float minAimSpread = 2.0f;
// Additional spread for a shooter with no vision or technique.
const float maxExtraAimSpread = 4.0f;
// A keeper can never be certain, however good he is.
const float maxSaveChance = 0.55f;

// What the engine reports about a kick that is being played out.
struct KickObservation {
  bool goalDetected = false;
  bool keeperHasBall = false;
  bool keeperTouchedBall = false;
  bool ballLeftField = false;
  bool ballStopped = false;
};

// A kick is given up on after this long.
const unsigned long kickTimeout_ms = 5000;

// How long each stage of a kick is shown for.
const unsigned long positioningDuration_ms = 2500;
const unsigned long executionDuration_ms = 1200;
const unsigned long resolutionDuration_ms = 2200;

struct State {
  int score[2] = {0, 0};
  int taken[2] = {0, 0};
  int shootingTeam = 0;
  int round = 1;
  e_Phase phase = e_Phase_Positioning;
  e_Outcome lastOutcome = e_Outcome_Pending;
};

// `lateralSample` and `heightSample` are expected in [-1, 1].
Aim CalculateAim(const Shooter& shooter, float lateralSample, float heightSample);
bool IsOnTarget(const Aim& aim);

// Probability the keeper reaches this kick.
float GetSaveChance(const Keeper& keeper, const Aim& aim);

// `keeperSample` is expected in [0, 1).
e_Outcome ResolveKick(const Shooter& shooter, const Keeper& keeper, float lateralSample,
                      float heightSample, float keeperSample);

// Classifies a kick from what actually happened on the pitch. Returns
// e_Outcome_Pending while the ball is still live.
e_Outcome ObserveKick(const KickObservation& observation, unsigned long elapsedSinceKick_ms);

// How long each phase of a single kick is shown for.
unsigned long GetPhaseDuration_ms(e_Phase phase);

// Index of the next taker: the best rated player who has not taken a kick in
// this round yet. Returns -1 when there is nobody to pick.
int SelectTakerIndex(const std::vector<float>& shotRatings, unsigned int takenMask);

// Records a kick; the mask resets once every candidate has taken one.
unsigned int MarkTaken(unsigned int takenMask, int index, int candidateCount);

State Create(int firstShootingTeam);

// Positioning -> Execution.
void BeginKick(State& state);
// Execution -> Resolution, crediting the kick to the shooting team.
void ApplyOutcome(State& state, e_Outcome outcome);
// Resolution -> Positioning for the other team, or Finished.
void NextKick(State& state);

bool IsDecided(const State& state);
bool IsSuddenDeath(const State& state);
// Team id of the winner, or -1 while the shootout is still alive.
int GetWinner(const State& state);

}  // namespace PenaltyShootout

#endif

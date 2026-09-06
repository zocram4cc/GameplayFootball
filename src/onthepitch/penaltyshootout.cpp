#include "penaltyshootout.hpp"

#include <algorithm>
#include <cmath>

#include "../gametypes.hpp"

namespace PenaltyShootout {

namespace {

float Clamp01(float value) {
  return std::max(0.0f, std::min(value, 1.0f));
}

float ClampSample(float sample) {
  return std::max(-1.0f, std::min(sample, 1.0f));
}

// Vision and technique together decide how tight the accuracy cone is.
float GetShooterSkill(const Shooter& shooter) {
  return Clamp01((Clamp01(shooter.vision) + Clamp01(shooter.shot)) * 0.5f);
}

// Best case vertical spread, and the extra a poor striker adds.
const float minHeightSpread = 0.6f;
const float maxExtraHeightSpread = 1.8f;
// How much of the save chance a perfectly placed kick takes away.
const float placementSaveRelief = 0.85f;

int Other(int team) {
  return team == 0 ? 1 : 0;
}

int RemainingRegulationKicks(const State& state, int team) {
  return std::max(0, regulationKicks - state.taken[team]);
}

}  // namespace

Aim CalculateAim(const Shooter& shooter, float lateralSample, float heightSample) {
  const float skill = GetShooterSkill(shooter);
  const float lateralSpread = minAimSpread + maxExtraAimSpread * (1.0f - skill);
  const float heightSpread = minHeightSpread + maxExtraHeightSpread * (1.0f - skill);

  Aim aim;
  aim.x = ClampSample(lateralSample) * lateralSpread;
  aim.z = aimCentreHeight + ClampSample(heightSample) * heightSpread;
  return aim;
}

bool IsOnTarget(const Aim& aim) {
  return std::fabs(aim.x) <= goalHalfWidth && aim.z >= 0.0f && aim.z <= goalHeight;
}

float GetSaveChance(const Keeper& keeper, const Aim& aim) {
  const float ability = Clamp01((Clamp01(keeper.reflexes) + Clamp01(keeper.awareness)) * 0.5f);

  // How far into the corner the kick is placed, 0 down the middle and 1 right in
  // the angle. A well-placed penalty is unreachable however good the keeper is.
  const float lateral = Clamp01(std::fabs(aim.x) / goalHalfWidth);
  const float height = Clamp01(std::fabs(aim.z - aimCentreHeight) / goalHeight);
  const float placement = Clamp01(std::sqrt(lateral * lateral + height * height));

  return Clamp01(maxSaveChance * ability * (1.0f - placementSaveRelief * placement));
}

e_Outcome ResolveKick(const Shooter& shooter, const Keeper& keeper, float lateralSample,
                      float heightSample, float keeperSample) {
  const Aim aim = CalculateAim(shooter, lateralSample, heightSample);
  if (!IsOnTarget(aim))
    return e_Outcome_Miss;

  return keeperSample < GetSaveChance(keeper, aim) ? e_Outcome_Save : e_Outcome_Goal;
}

e_Outcome ObserveKick(const KickObservation& observation, unsigned long elapsedSinceKick_ms) {
  // The net is the last word, even if the keeper got a hand to it.
  if (observation.goalDetected)
    return e_Outcome_Goal;
  if (observation.keeperHasBall)
    return e_Outcome_Save;
  // Kept out by the keeper rather than by the woodwork or bad aim.
  if (observation.keeperTouchedBall && (observation.ballStopped || observation.ballLeftField))
    return e_Outcome_Save;
  if (observation.ballLeftField || observation.ballStopped)
    return e_Outcome_Miss;
  if (elapsedSinceKick_ms >= kickTimeout_ms)
    return e_Outcome_Miss;
  return e_Outcome_Pending;
}

unsigned long GetPhaseDuration_ms(e_Phase phase) {
  switch (phase) {
    case e_Phase_Positioning:
      return positioningDuration_ms;
    case e_Phase_Execution:
      return executionDuration_ms;
    case e_Phase_Resolution:
      return resolutionDuration_ms;
    default:
      return 0;
  }
}

int SelectTakerIndex(const std::vector<float>& shotRatings, unsigned int takenMask) {
  const int count = static_cast<int>(shotRatings.size());
  if (count == 0)
    return -1;

  // Everyone has taken one already: start a fresh round rather than nobody.
  const unsigned int allTaken = count >= 32 ? ~0u : (1u << count) - 1u;
  if ((takenMask & allTaken) == allTaken)
    takenMask = 0;

  int best = -1;
  for (int i = 0; i < count; i++) {
    if (takenMask & (1u << i))
      continue;
    if (best == -1 || shotRatings[i] > shotRatings[best])
      best = i;
  }
  return best;
}

unsigned int MarkTaken(unsigned int takenMask, int index, int candidateCount) {
  if (index < 0 || candidateCount <= 0 || index >= candidateCount)
    return takenMask;

  takenMask |= 1u << index;
  const unsigned int allTaken = candidateCount >= 32 ? ~0u : (1u << candidateCount) - 1u;
  return (takenMask & allTaken) == allTaken ? 0u : takenMask;
}

State Create(int firstShootingTeam) {
  State state;
  state.shootingTeam = firstShootingTeam == 1 ? 1 : 0;
  return state;
}

void BeginKick(State& state) {
  if (state.phase != e_Phase_Positioning)
    return;
  state.phase = e_Phase_Execution;
  state.lastOutcome = e_Outcome_Pending;
}

void ApplyOutcome(State& state, e_Outcome outcome) {
  if (state.phase != e_Phase_Execution)
    return;

  state.taken[state.shootingTeam]++;
  if (outcome == e_Outcome_Goal)
    state.score[state.shootingTeam]++;

  state.lastOutcome = outcome;
  state.phase = e_Phase_Resolution;
}

void NextKick(State& state) {
  if (state.phase != e_Phase_Resolution)
    return;

  if (IsDecided(state)) {
    state.phase = e_Phase_Finished;
    return;
  }

  // Both teams have taken this round's kick: on to the next one.
  if (state.taken[0] == state.taken[1])
    state.round++;

  state.shootingTeam = Other(state.shootingTeam);
  state.phase = e_Phase_Positioning;
}

bool IsDecided(const State& state) {
  return GetWinner(state) != -1;
}

bool IsSuddenDeath(const State& state) {
  return state.taken[0] >= regulationKicks && state.taken[1] >= regulationKicks &&
         !IsDecided(state);
}

int GetWinner(const State& state) {
  const bool regulationComplete =
      state.taken[0] >= regulationKicks && state.taken[1] >= regulationKicks;

  if (!regulationComplete) {
    // A lead bigger than what the trailing team can still salvage from its
    // remaining kicks ends the shootout early.
    for (int team = 0; team < 2; team++) {
      const int opponent = Other(team);
      if (state.score[team] > state.score[opponent] + RemainingRegulationKicks(state, opponent))
        return team;
    }
    return -1;
  }

  // Regulation over: both after the fifth kick and in sudden death, a lead only
  // counts once both teams have taken the same number of kicks.
  if (state.taken[0] == state.taken[1] && state.score[0] != state.score[1])
    return state.score[0] > state.score[1] ? 0 : 1;

  return -1;
}

}  // namespace PenaltyShootout

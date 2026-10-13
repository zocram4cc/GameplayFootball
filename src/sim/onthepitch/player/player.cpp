// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "player.hpp"

#include <cmath>
#include <cstring>

#include "simcontext.hpp"
#include "../../utils.hpp"
#include "../gameplaytuning.hpp"
#include "../match.hpp"
#include "../team.hpp"
#include "base/geometry/triangle.hpp"
#include "controller/elizacontroller.hpp"
#include "controller/strategies/strategy.hpp"
#include "data/playerskills.hpp"
#include "onthepitch/gameplaytuning.hpp"
#include "onthepitch/matchpressure.hpp"
#include "onthepitch/pitchconditions.hpp"

Player::Player(Team* team, PlayerData* playerData)
    : PlayerBase(team->GetMatch(), playerData), team(team) {
  hasPossession = false;
  hasBestPossession = false;
  hasUniquePossession = false;
  possessionDuration_ms = 0;
  timeNeededToGetToBall_ms = 1000;
  timeNeededToGetToBall_optimistic_ms = 1000;
  timeNeededToGetToBall_previous_ms = 1000;
  SetDesiredTimeToBall_ms(0);
  manMarkingID = -1;

  triggerControlledBallCollision = false;

  tacticalSituation.forwardSpaceRating = 0;
  tacticalSituation.toGoalSpaceRating = 0;
  tacticalSituation.spaceRating = 0;

  cards = 0;

  cardEffectiveTime_ms = 0;
}

Player::~Player() {}

Humanoid* Player::CastHumanoid() {
  return static_cast<Humanoid*>(humanoid);
}

ElizaController* Player::CastController() {
  return static_cast<ElizaController*>(controller);
}

int Player::GetTeamID() const {
  return team->GetID();
}

Team* Player::GetTeam() {
  return team;
}

void Player::Activate(boost::intrusive_ptr<Node> humanoidSourceNode,
                      std::shared_ptr<AnimCollection> animCollection) {
  assert(!isActive);

  humanoid = new Humanoid(this, humanoidSourceNode, animCollection);

  controller = new ElizaController(match);
  CastController()->SetPlayer(this);
  CastController()->LoadStrategies();

  // only advertise the player as active once his humanoid exists
  isActive = true;

  CastHumanoid()->ResetPosition(
      GetFormationEntry().position * 25 * Vector3(-team->GetSide(), -team->GetSide(), 0),
      Vector3(0));

  SetDynamicFormationEntry(GetFormationEntry());
}

bool Player::IsBeatenKeeper() {
  if (GetFormationEntry().role != e_PlayerRole_GK)
    return false;

  // The no-block rule only applies to a genuine shot: fast, and heading for his
  // goal. Crosses and rolling balls are his to collect as always.
  const Vector3 ballMovement = match->GetBall()->GetMovement();
  if (ballMovement.GetLength() < 14.0f)
    return false;
  const Vector3 towardsOwnGoal =
      (Vector3(pitchHalfW * team->GetSide(), 0, 0) - match->GetBall()->Predict(0)).Get2D();
  if (ballMovement.Get2D()
          .GetNormalized(Vector3(0))
          .GetDotProduct(towardsOwnGoal.GetNormalized(Vector3(0))) < 0.7f)
    return false;

  return !KeeperAttemptsSave();
}

// The two what-if keeper tweaks the census asks about, both deliberately
// slight: a longer standing frame and a quicker read. Neither is applied -
// they only count how many of the shots he was beaten by each would have
// taken, which is what decides whether either has headroom worth spending.
namespace {
constexpr float kWhatIfStandingReach_m = 1.25f;  // 1.0 m now, +25 cm
constexpr float kWhatIfReactionScale = 0.75f;    // 25% quicker read
}  // namespace

bool Player::KeeperAttemptsSave() {
  if (GetFormationEntry().role != e_PlayerRole_GK)
    return true;

  // Whether he gets there, not whether he is lucky. This used to be one
  // random(0,1) per shot against 0.53-0.66 - so a third to a half of all shots
  // went unattempted whatever the keeper's quality, which is what "they fumble
  // a lot and it looks goofy" is (owner, 15-09).
  //
  // The model is the geometry of the save, and every term is one of PES's five
  // GK attributes:
  //   - the ball's own flight gives the time available (its speed towards his
  //     line and how far out it is);
  //   - GK Reflexes spends the first part of that as his own reaction
  //     (GetKeeperReactionTime_s, 200-350 ms - a human's, not a pad's);
  //   - GK Coverage is his reach - a dive, not a step;
  //   - what is left he covers at diving speed (GetKeeperDiveSpeed_ms).
  // Beaten means the ball crosses his line further away than he can get. A
  // tame shot straight at him is therefore always saved, and a fast shot into
  // the far corner always beats him, regardless of who is in goal - which is
  // how a keeper reads on screen.
  const Vector3 ballPosition = match->GetBall()->Predict(0);
  const Vector3 ballMovement = match->GetBall()->GetMovement();
  const float lineX = pitchHalfW * team->GetSide();
  const float closingSpeed = ballMovement.coords[0] * (lineX > 0.0f ? 1.0f : -1.0f);
  if (closingSpeed <= 0.1f) return true;  // not coming at him: his to collect
  // The plane he can actually get a hand to is HIS OWN, not the goal line: a
  // keeper stands metres off his line on the ball-goal bisector
  // (goalie_default.cpp), and sampling the ball where it meets the LINE while
  // comparing that to his position charges him for every metre the ball travels
  // sideways over his own offset. Coming out to cut the angle is what the
  // offset is FOR. GameplayTuning::GetKeeperSaveChallenge owns that choice, so
  // the sweep test measures the same geometry this does.
  const GameplayTuning::KeeperSaveChallenge challenge = GameplayTuning::GetKeeperSaveChallenge(
      {ballPosition.coords[0], ballPosition.coords[1], ballPosition.coords[2]},
      {ballMovement.coords[0], ballMovement.coords[1], ballMovement.coords[2]},
      {GetPosition().coords[0], GetPosition().coords[1], GetPosition().coords[2]}, lineX);
  if (!challenge.shotAtGoal) {
    keeperBeatenRecorded = false;  // nothing coming: re-arm the census
    return true;
  }

  const float gap_m = challenge.gap_m;
  const float timeToPlane_s = challenge.timeToPlane_s;
  const float latency_s = GameplayTuning::GetKeeperReactionTime_s(GetStat("gk_reflexes"));
  // Only the reach behind the ball counts: with almost no flight even the best
  // keeper has barely started to extend, which is why point blank beats an
  // ordinary keeper and still has to beat a good one reaching for it.
  const float reach_m = GameplayTuning::KeeperEffectiveReach_m(
      GameplayTuning::GetKeeperDiveReach_m(GetStat("gk_coverage")), timeToPlane_s, latency_s);
  const bool reaches = GameplayTuning::KeeperReachesShot(
      gap_m, timeToPlane_s, latency_s, reach_m,
      GameplayTuning::GetKeeperDiveSpeed_ms(GetStat("gk_coverage")));
  // Census: was he out of reach or out of time? Once per ball approach, and
  // only for a ball actually heading INSIDE the frame - `shotAtGoal` above is
  // true for anything closing on his plane, so without this gate the sample
  // is clearances and passes upfield (measured, first match: mean "gap" 22 m
  // and 46 m, i.e. balls half a pitch wide of the post).
  const GameplayTuning::GoalLineCrossing heading = GameplayTuning::PredictGoalLineCrossing(
      {ballPosition.coords[0], ballPosition.coords[1], ballPosition.coords[2]},
      {ballMovement.coords[0], ballMovement.coords[1], ballMovement.coords[2]}, pitchHalfW);
  const bool atTheFrame =
      GameplayTuning::CrossedInsideGoalFrame(heading, goalHalfWidth, goalHeight);
  if (!atTheFrame)
    keeperBeatenRecorded = false;  // not a shot: re-arm rather than latch on it
  else if (!reaches && !keeperBeatenRecorded) {
    keeperBeatenRecorded = true;
    match->GetMatchData()->AddKeeperBeaten(team->GetID(), gap_m, timeToPlane_s,
                                           challenge.lateral_m, challenge.overhead_m);
    // Which lever, if any, would have got there: a slightly longer standing
    // frame (the short-flight case, where there is no dive to extend) or a
    // slightly quicker read (the shots with flight on the clock). Census only -
    // the decision above is untouched - and the answer arrives with the next
    // batch instead of costing one batch per guess.
    const float diveReach_m = GameplayTuning::GetKeeperDiveReach_m(GetStat("gk_coverage"));
    const float diveSpeed_ms = GameplayTuning::GetKeeperDiveSpeed_ms(GetStat("gk_coverage"));
    match->GetMatchData()->AddKeeperBeatenWhatIf(
        team->GetID(),
        GameplayTuning::KeeperReachesShotWithStandingReach(gap_m, timeToPlane_s, latency_s,
                                                           diveReach_m, diveSpeed_ms,
                                                           kWhatIfStandingReach_m),
        GameplayTuning::KeeperReachesShotWithStandingReach(
            gap_m, timeToPlane_s, latency_s, diveReach_m, diveSpeed_ms,
            GameplayTuning::kKeeperStandingReach_m, latency_s * kWhatIfReactionScale));
  }
  return reaches;
}

float Player::GetSlipVelocityMultiplier() const {
  if (lastSlipTime_ms == 0)
    return 1.0f;
  return PitchConditions::GetSlipVelocityMultiplier(match->GetActualTime_ms() - lastSlipTime_ms);
}

bool Player::IsSlipping() const {
  return lastSlipTime_ms != 0 &&
         match->GetActualTime_ms() - lastSlipTime_ms < PitchConditions::slipRecoveryTime_ms;
}

void Player::Deactivate() {
  ResetSituation(GetPosition());

  PlayerBase::Deactivate();
}

FormationEntry Player::GetFormationEntry() {
  return team->GetFormationEntry(id);
}

bool Player::HasPossession() const {
  return hasPossession;
}

bool Player::HasBestPossession() const {
  return hasBestPossession;
}

bool Player::HasUniquePossession() const {
  return hasUniquePossession;
}

bool Player::AllowLastDitch(bool includingPossessionAmount) const {
  if (includingPossessionAmount && team->GetTeamPossessionAmount() < 1.0f)
    return true;  // why team possession amount and not player's? answer: because we don't have that
                  // info here (todo: fix that)
  return (GetTimeNeededToGetToBall_optimistic_ms() * 1.7f + 800 < GetTimeNeededToGetToBall_ms());
}

float Player::GetAverageVelocity(float timePeriod_sec) {
  assert((int)timePeriod_sec > 0);
  unsigned int logSize = positionHistoryPerSecond.size();
  if (logSize == 0)
    return 0;
  Vector3 prevPos;
  float totalDistance = 0;
  unsigned int count = 0;
  while (count <= (unsigned int)timePeriod_sec) {
    Vector3 pos = positionHistoryPerSecond.at(logSize - 1 - count);
    if (count > 0)
      totalDistance += (pos - prevPos).GetLength();
    count++;
    if (logSize - count == 0)
      break;
    prevPos = pos;
  }
  return totalDistance / timePeriod_sec;  // don't divide by count, since lack of entries should not
                                          // influence average
}

void Player::UpdatePossessionStats(bool onInterval) {
  if (!onInterval)
    return;

  timeNeededToGetToBall_previous_ms =
      timeNeededToGetToBall_ms;  // todo: this will fail to function as intended when this function
                                 // is ran multiple times consecutively

  // default
  timeNeededToGetToBall_ms =
      std::max(ballPredictionSize_ms,
               (unsigned int)(round((match->GetBall()->Predict(ballPredictionSize_ms - 10).Get2D() -
                                     (GetPosition() + GetMovement() * 0.2f))
                                        .GetLength() /
                                    (GetMaxVelocity() * 0.75f) * 1000)));
  timeNeededToGetToBall_optimistic_ms = timeNeededToGetToBall_ms;

  unsigned int startTime_ms = 0;
  if ((CastHumanoid()->GetCurrentFunctionType() == e_FunctionType_ShortPass ||
       CastHumanoid()->GetCurrentFunctionType() == e_FunctionType_LongPass ||
       CastHumanoid()->GetCurrentFunctionType() == e_FunctionType_HighPass ||
       CastHumanoid()->GetCurrentFunctionType() == e_FunctionType_Shot) &&
      !TouchPending()) {
    startTime_ms = 500;
  }

  bool refine = false;
  unsigned int timeStep_ms = 10;
  unsigned int previous_ms = 0;
  bool precise = (team->GetDesignatedTeamPossessionPlayer() == this) ? true : false;
  float previousDist = 0;  // debug
  for (unsigned int ms = startTime_ms; ms < ballPredictionSize_ms; ms += timeStep_ms) {
    if (match->GetBall()->Predict(ms).coords[2] < 1.5f) {
      TimeNeeded result = AI_GetTimeNeededForDistance_ms(GetPosition(), GetMovement(),
                                                         match->GetBall()->Predict(ms).Get2D(),
                                                         GetMaxVelocity(), precise, ms, GetDebug());
      unsigned int timeNeeded = result.usual_ms;
      unsigned int timeNeeded_optimistic = result.optimistic_ms;

      // // debug
      // if (GetDebug()) {
      //   float dist = (match->GetBall()->Predict(ms).Get2D() - GetPosition()).GetLength();
      //   if (dist > previousDist) {
      //     SetGreenDebugPilon(dudVec);
      //     SetYellowDebugPilon(dudVec + (match->GetBall()->Predict(ms).Get2D() -
      //     GetPosition()).GetNormalized(0) * dud); previousDist = 1000000;
      //   } else {
      //     previousDist = dist;
      //   }
      // }

      if (timeNeeded_optimistic <= ms) {
        if (ms < timeNeededToGetToBall_optimistic_ms)
          timeNeededToGetToBall_optimistic_ms = ms;
        // foundOptimisticTime = true;
        if (GetDebug()) {
          // SetRedDebugPilon(GetPosition() + Vector3(timeNeededToGetToBall_optimistic_ms / 100.0f,
          // 0.0f, 0.05f));
        }
      }

      if (timeNeeded <= ms) {
        // refinement round!
        if (!refine) {
          ms = previous_ms;
          timeStep_ms = 10;
          refine = true;
          // if (GetDebug()) printf("refining..\n");

          // found!
        } else {
          timeNeededToGetToBall_ms = ms;
          if (GetDebug()) {
            // printf("timestep: %i, resultRadius: %f, ballDistance: %f\n", timeStep_ms, dud,
            // (match->GetBall()->Predict(0) - GetPosition()).GetLength()); printf("time needed:
            // %u\n", timeNeededToGetToBall_ms); SetGreenDebugPilon(GetPosition() +
            // Vector3(timeNeededToGetToBall_ms / 100.0f, 0.0f, 0.0f)); printf("found!\n");
          }
          if (team->GetDesignatedTeamPossessionPlayer() == this && team->GetID() == 1) {
            // SetYellowDebugPilon(GetPosition() + Vector3(timeNeededToGetToBall_ms / 100.0f, 0.0f,
            // 0.0f));
          }
          break;
        }
      }
    }

    // refine timestep (optimisation)
    if (!refine) {
      float balldist = (GetPosition() - match->GetBall()->Predict(ms).Get2D()).GetLength() +
                       0.2f;  // add a little buffer
      float maxBallVelo = 50;
      // how long does it take for the ball at max velo to travel balldist?
      unsigned int timeToGo_ms = int(std::round((balldist / maxBallVelo) * 1000.0f));
      timeStep_ms = clamp(timeToGo_ms, 10, 500);
      // round to 10s
      timeStep_ms = int(std::floor(timeStep_ms / 10.0f)) * 10;
    } else
      timeStep_ms = 10;

    previous_ms = ms;
  }

  if (TouchAnim() && TouchPending()) {
    unsigned int animTimeToBall_ms = (CastHumanoid()->GetTouchFrame() - GetCurrentFrame()) * 10;
    timeNeededToGetToBall_ms = std::min(timeNeededToGetToBall_ms, animTimeToBall_ms);
    timeNeededToGetToBall_optimistic_ms = timeNeededToGetToBall_ms;
  }

  if (timeNeededToGetToBall_ms < defaultTouchOffset_ms) {
    // apply quantum mechanics on the scale of the very small ;)
    timeNeededToGetToBall_ms =
        NormalizedClamp(((GetPosition() + GetMovement() * (defaultTouchOffset_ms * 0.001)) -
                         match->GetBall()->Predict(defaultTouchOffset_ms).Get2D())
                            .GetLength(),
                        0.0f, 0.6f) *
        defaultTouchOffset_ms;
    timeNeededToGetToBall_optimistic_ms = timeNeededToGetToBall_ms;
  }

  if ((CastHumanoid()->GetCurrentFunctionType() == e_FunctionType_ShortPass ||
       CastHumanoid()->GetCurrentFunctionType() == e_FunctionType_LongPass ||
       CastHumanoid()->GetCurrentFunctionType() == e_FunctionType_HighPass ||
       CastHumanoid()->GetCurrentFunctionType() == e_FunctionType_Shot) &&
      !TouchPending()) {
    hasPossession = false;
  } else {  // todo: shouldn't this be somewhere else?
    hasPossession = AI_HasPossession(match->GetBall(), this);
  }

  this->hasBestPossession =
      hasPossession && match->GetTeam(abs(team->GetID() - 1))->GetTimeNeededToGetToBall_ms() >
                           this->GetTimeNeededToGetToBall_ms();
  this->hasUniquePossession =
      hasPossession &&
      !match->GetTeam(abs(team->GetID() - 1))
           ->HasPossession();  // todo: this should be in a seperate function, to be called after
                               // the other team's timetoball function already ran.

  if (match->GetBallRetainer() == this) {
    timeNeededToGetToBall_ms = 1;
    timeNeededToGetToBall_optimistic_ms = 1;
    SetDesiredTimeToBall_ms(timeNeededToGetToBall_ms);
    hasPossession = true;
    hasBestPossession = true;
    hasUniquePossession = true;
  } else if (match->GetBallRetainer() != 0) {
    hasPossession = false;
    hasBestPossession = false;
    hasUniquePossession = false;
  }
}

float Player::GetClosestOpponentDistance() const {
  Player* opp = AI_GetClosestPlayer(match->GetTeam(abs(team->GetID() - 1)), GetPosition());
  return opp->GetPosition().GetDistance(GetPosition());
}

void Player::Process() {
  // if (GetDebug()) SetGreenDebugPilon(GetPosition() + GetMovement());

  if (isActive) {
    desiredTimeToBall_ms = std::max(desiredTimeToBall_ms - 10, 0);

    CastController()->Process();

    if (match->IsInPlay()) {
      if (match->GetActualTime_ms() % 1000 == 0) {
        positionHistoryPerSecond.push_back(GetPosition());
        // if (GetDebug()) printf("average velo (5): %f, (50): %f\n", GetAverageVelocity(5),
        // GetAverageVelocity(50));
      }
      if (hasPossession)
        possessionDuration_ms += 10;
      else
        possessionDuration_ms = 0;
      if ((match->GetActualTime_ms() + GetID() * 10) % 100 == 0) {
        _CalculateTacticalSituation();
      }
    }

    Vector3 posBefore = CastHumanoid()->GetPosition();

    CastHumanoid()->Process();

    Vector3 posAfter = CastHumanoid()->GetPosition();

    float distance = (posAfter - posBefore).GetLength();
    // Wet or chewed-up pitch: a sharp turn at pace can put a player on the
    // floor, which costs him his momentum for a moment (proposal 4A).
    if (distance > 0.0f) {
      const Vector3 currentDirection = GetDirectionVec();
      if (previousDirectionVec.GetLength() > 0.01f && !IsSlipping()) {
        const float sharpness =
            PitchConditions::GetTurnSharpness(currentDirection.GetDotProduct(previousDirectionVec));
        const float speedFactor =
            GetMaxVelocity() > 0.0f
                ? clamp(GetMovement().GetLength() / GetMaxVelocity(), 0.0f, 1.0f)
                : 0.0f;
        const float slipChance =
            PitchConditions::GetSlipChance(match->GetBall()->GetWeatherWetness(),
                                           PitchConditions::GetWear(match->GetMatchTime_ms()),
                                           sharpness * speedFactor, GetStat("physical_balance"));
        if (PitchConditions::ShouldSlip(slipChance, random(0.0f, 1.0f)))
          lastSlipTime_ms = match->GetActualTime_ms();
      }
      previousDirectionVec = currentDirection;
    }

    // PES 2021's per-tick stamina drain
    // (docs/re/pes-re-stamina-drain.md 3.3), which only runs while the ball is
    // in play: a dead ball freezes the tank. The GF distance-based drain it
    // replaces is gone.
    if (match->IsInPlay() && !match->IsInSetPiece()) {
      float speed_kmh = 0.0f;
      // The movement the humanoid produced this tick, in km/h.
      const float moved_m = (posAfter - posBefore).GetLength();
      if (moved_m > 0.0f) speed_kmh = moved_m * 100.0f * 3.6f;
      // The 3-D velocity PES tests against 22 km/h uses the same speed here.
      pes::UpdateBreath(match->GetPesStaminaConstants(), stamina, speed_kmh,
                        hasPossession, &stamina.breath);
      const int breathPct = stamina.breath * 100 /
                            std::max(1, pes::BreathCap(match->GetPesStaminaConstants(), stamina));
      stamina.cur = pes::DrainTick(match->GetPesStaminaConstants(), stamina,
                                   pes::SpeedClass(match->GetPesStaminaConstants(), speed_kmh),
                                   breathPct, team->HasPossession());
    }

    if (cards > 1 && cardEffectiveTime_ms <= match->GetActualTime_ms()) {
      SendOff();
    }
  }
}

void Player::SendOff() {
  Deactivate();

  if (GetFormationEntry().role == e_PlayerRole_GK) {
    FormationEntry entry = GetFormationEntry();
    std::vector<Player*> activePlayers;
    team->GetActivePlayers(activePlayers);
    assert(activePlayers.size() > 0);
    int newGoalieID = (*activePlayers.begin())->GetID();
    team->SetFormationEntry(newGoalieID, entry);
  }

  std::vector<Player*> activePlayers;
  team->GetActivePlayers(activePlayers);
  int remainingPlayers = activePlayers.size();
  if (remainingPlayers <= 6) {
    // too many red cards - forfeit
    // todo: referee should do this. also, make sure the forfeiting team will actually lose :P
    match->GameOver();
  }
}

float Player::GetStat(const char* name) const {
  // GF ran CPU teams at the configured difficulty; every team here is CPU.
  float multiplier = 0.3f + 0.7f * team->GetMatch()->GetMatchDifficulty();
  // PES's A5 fatigue factor (docs/re/pes-re-stamina-drain.md 4): 1.0 at a full
  // tank, 0.8 when it empties, on the ten stat ids the report names. It goes
  // on the base along with the per-match arrays, so the ceiling clamp below
  // still sees a rested player at 1.00 rather than past it.
  const float fatigue = GetFatigueFactor();
  // Injury: PES's 2 = knock (plays on) and 3 = injured, out of 3, so a knock
  // costs a third and an injury all of the 40% the old GF scale reserved for
  // a fully-dead player.
  multiplier *= 1.0f - 0.4f * (std::min(3, stamina.injuryLevel) / 3.0f);
  // A speed merchant loses his head at full tilt (proposal 3A).
  if (std::strcmp(name, "mental_calmness") == 0) {
    const PlayerSkills::Mask skills = playerData->GetSkills();
    if (skills != PlayerSkills::maskNone) {
      const float speedFactor =
          GetMaxVelocity() > 0.0f ? clamp(GetMovement().GetLength() / GetMaxVelocity(), 0.0f, 1.0f)
                                  : 0.0f;
      return PlayerSkills::GetCalmnessAtSpeed(skills, playerData->GetStat(name) * multiplier,
                                              speedFactor);
    }
  }

  // Clutch performers raise their technical game in a tight finish (proposal 3B).
  if (std::strncmp(name, "technical_", 10) == 0) {
    Match* playerMatch = team->GetMatch();
    const int goalDifference = playerMatch->GetMatchData()->GetGoalCount(team->GetID()) -
                               playerMatch->GetMatchData()->GetGoalCount(abs(team->GetID() - 1));
    multiplier *= MatchPressure::GetClutchTechnicalMultiplier(
        playerData->GetStat("mental_resilience"), goalDifference, playerMatch->GetMatchTime_ms());
  }
  // if (GetDebug()) printf("stat %s == %f\n", name, playerData->GetStat(name) * multiplier);

  if (playerData->GetStat(name) == 0.0f)
    printf("NULLSTAT: name: %s\n", name);
  // PES's per-match arrays go on the BASE attribute and clamp at the ceiling
  // before the situational factors above (difficulty, fatigue, injury, clutch)
  // scale it: a player already at 1.00 on a red arrow gets 1.00, never more,
  // and nothing that reads this ever sees a value past the range it was
  // written for. The clamp at the end catches a clutch multiplier above one.
  // The breath penalty (report 4) is a flat integer subtraction from the
  // four breath-sensitive ids, on PES's 0..99 stat byte. Applied here as a
  // fraction of the full stat range (100 points = the whole scale), and
  // multiplied by the A5 fatigue factor on the way out.
  const float penalty = static_cast<float>(GetBreathPenalty()) / 100.0f;
  const float base = FormState::Apply(std::max(0.0f, playerData->GetStat(name) - penalty),
                                      conditionArrow, GetDeployedFamiliarity(), name);
  return clamp(base * multiplier, 0.0f, 1.0f) * fatigue;
}

FormState::Slot Player::GetDeployedSlot() const {
  // Left and right are read off the formation position across the pitch, in
  // the team's own frame: negative y is the left flank when attacking towards
  // +x, so the sign flips with the side the team plays on.
  const FormationEntry& entry = dynamicFormationEntry;
  int side = 0;
  const float across = entry.databasePosition.coords[1] * -const_cast<Team*>(team)->GetSide();
  if (across < -0.25f) side = -1;
  if (across > 0.25f) side = +1;
  return FormState::SlotFor(entry.role, side);
}

FormState::Familiarity Player::GetDeployedFamiliarity() const {
  return FormState::Rating(playerData->GetPositionFamiliarity(), GetDeployedSlot());
}

void Player::ResetSituation(const Vector3& focusPos) {
  PlayerBase::ResetSituation(focusPos);

  hasPossession = false;
  hasBestPossession = false;
  hasUniquePossession = false;
  possessionDuration_ms = 0;
  timeNeededToGetToBall_ms = 1000;
  timeNeededToGetToBall_optimistic_ms = 1000;
  SetDesiredTimeToBall_ms(0);
  manMarkingID = -1;

  triggerControlledBallCollision = false;

  tacticalSituation.forwardSpaceRating = 0;
  tacticalSituation.toGoalSpaceRating = 0;
  tacticalSituation.spaceRating = 0;
}

void Player::_CalculateTacticalSituation() {
  const MentalImage* mentalImage =
      static_cast<PlayerController*>(GetController())->GetMentalImage();
  assert(mentalImage);
  assert(IsActive());

  // calculate how free the path forward is
  float time_sec = 0.5f;
  Vector3 checkPos = GetPosition() + Vector3(-team->GetSide(), 0, 0) * sprintVelocity * time_sec;
  tacticalSituation.forwardSpaceRating = AI_CalculateFreeSpace(
      match, mentalImage, team->GetID(), checkPos, 5.0f, time_sec, true);  // FREESPACE :D :D

  // calculate the amount of space this player has
  // tacticalSituation.spaceRating = AI_CalculatePersonalFreeSpace(match, mentalImage,
  // this, 8.0f, 8.0f, 0.2f);
  time_sec = 0.1f;
  checkPos = GetPosition() + GetMovement() * time_sec;
  tacticalSituation.spaceRating = AI_CalculateFreeSpace(match, mentalImage, team->GetID(), checkPos,
                                                        5.0f, time_sec, true);  // FREESPACE :D :D

  // distance to opponent goal 0 .. 1 == farthest .. closest
  tacticalSituation.forwardRating =
      1.0f - clamp((Vector3(pitchHalfW * -team->GetSide(), 0, 0) - GetPosition()).GetLength() /
                       (pitchHalfW * 2.0f),
                   0.0f, 1.0f);
  tacticalSituation.forwardRating = std::pow(tacticalSituation.forwardRating,
                                             1.5f);  // more important when close to goal
}

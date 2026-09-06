// Tests for the penalty shootout described in TECHNICAL_ROADMAP.md section 3C:
// a stat-driven "dice roll" resolution plus the shootout state machine.

#include <gtest/gtest.h>

#include "gametypes.hpp"
#include "onthepitch/penaltyshootout.hpp"

namespace {

PenaltyShootout::Shooter GoodShooter() {
  PenaltyShootout::Shooter shooter;
  shooter.vision = 0.9f;
  shooter.shot = 0.9f;
  return shooter;
}

PenaltyShootout::Shooter PoorShooter() {
  PenaltyShootout::Shooter shooter;
  shooter.vision = 0.1f;
  shooter.shot = 0.1f;
  return shooter;
}

// Keepers are rated on PES's GK attributes (reflexes, awareness), not the
// outfield reaction/positioning stats the struct used to mirror.
PenaltyShootout::Keeper GoodKeeper() {
  PenaltyShootout::Keeper keeper;
  keeper.reflexes = 0.9f;
  keeper.awareness = 0.9f;
  return keeper;
}

PenaltyShootout::Keeper PoorKeeper() {
  PenaltyShootout::Keeper keeper;
  keeper.reflexes = 0.1f;
  keeper.awareness = 0.1f;
  return keeper;
}

// Walks a shootout to the point where each team has taken `kicks` kicks with the
// given per-kick scoring pattern.
PenaltyShootout::State PlayOut(const std::vector<PenaltyShootout::e_Outcome>& outcomes,
                               int firstTeam = 0) {
  PenaltyShootout::State state = PenaltyShootout::Create(firstTeam);
  for (PenaltyShootout::e_Outcome outcome : outcomes) {
    if (PenaltyShootout::IsDecided(state))
      break;
    PenaltyShootout::BeginKick(state);
    PenaltyShootout::ApplyOutcome(state, outcome);
    PenaltyShootout::NextKick(state);
  }
  return state;
}

const PenaltyShootout::e_Outcome kGoal = PenaltyShootout::e_Outcome_Goal;
const PenaltyShootout::e_Outcome kSave = PenaltyShootout::e_Outcome_Save;
const PenaltyShootout::e_Outcome kMiss = PenaltyShootout::e_Outcome_Miss;

}  // namespace

// --- Aim: shooter accuracy cone ---

TEST(PenaltyAimTest, APerfectSampleAimsAtTheCentreOfTheGoalWhoeverShoots) {
  const PenaltyShootout::Aim good = PenaltyShootout::CalculateAim(GoodShooter(), 0.0f, 0.0f);
  const PenaltyShootout::Aim poor = PenaltyShootout::CalculateAim(PoorShooter(), 0.0f, 0.0f);

  EXPECT_FLOAT_EQ(good.x, 0.0f);
  EXPECT_FLOAT_EQ(poor.x, 0.0f);
  EXPECT_GT(good.z, 0.0f);  // aimed above the ground
  EXPECT_LT(good.z, goalHeight);
}

TEST(PenaltyAimTest, BetterShootersHaveATighterCone) {
  const PenaltyShootout::Aim good = PenaltyShootout::CalculateAim(GoodShooter(), 1.0f, 1.0f);
  const PenaltyShootout::Aim poor = PenaltyShootout::CalculateAim(PoorShooter(), 1.0f, 1.0f);

  EXPECT_LT(std::abs(good.x), std::abs(poor.x));
}

TEST(PenaltyAimTest, TheSampleSignPicksTheSideOfTheGoal) {
  const PenaltyShootout::Aim left = PenaltyShootout::CalculateAim(GoodShooter(), -1.0f, 0.0f);
  const PenaltyShootout::Aim right = PenaltyShootout::CalculateAim(GoodShooter(), 1.0f, 0.0f);

  EXPECT_LT(left.x, 0.0f);
  EXPECT_GT(right.x, 0.0f);
  EXPECT_NEAR(std::abs(left.x), std::abs(right.x), 1e-5f);
}

TEST(PenaltyAimTest, AGoodShooterStaysOnTargetWhereAPoorOneSprraysIt) {
  EXPECT_TRUE(
      PenaltyShootout::IsOnTarget(PenaltyShootout::CalculateAim(GoodShooter(), 1.0f, 1.0f)));
  EXPECT_FALSE(
      PenaltyShootout::IsOnTarget(PenaltyShootout::CalculateAim(PoorShooter(), 1.0f, 1.0f)));
}

TEST(PenaltyAimTest, OnTargetMeansInsideTheFrameOfTheGoal) {
  PenaltyShootout::Aim insideRightPost;
  insideRightPost.x = goalHalfWidth - 0.2f;
  insideRightPost.z = 1.0f;
  EXPECT_TRUE(PenaltyShootout::IsOnTarget(insideRightPost));

  PenaltyShootout::Aim wide;
  wide.x = goalHalfWidth + 0.2f;
  wide.z = 1.0f;
  EXPECT_FALSE(PenaltyShootout::IsOnTarget(wide));

  PenaltyShootout::Aim overTheBar;
  overTheBar.x = 0.0f;
  overTheBar.z = goalHeight + 0.2f;
  EXPECT_FALSE(PenaltyShootout::IsOnTarget(overTheBar));
}

// --- Keeper: dive success ---

TEST(PenaltyKeeperTest, BetterKeepersSaveMoreOften) {
  PenaltyShootout::Aim aim;
  aim.x = 1.0f;
  aim.z = 1.0f;
  EXPECT_GT(PenaltyShootout::GetSaveChance(GoodKeeper(), aim),
            PenaltyShootout::GetSaveChance(PoorKeeper(), aim));
}

TEST(PenaltyKeeperTest, CornersAreHarderToReachThanCentralShots) {
  PenaltyShootout::Aim central;
  central.x = 0.1f;
  central.z = 0.5f;

  PenaltyShootout::Aim corner;
  corner.x = goalHalfWidth - 0.2f;
  corner.z = goalHeight - 0.2f;

  EXPECT_GT(PenaltyShootout::GetSaveChance(GoodKeeper(), central),
            PenaltyShootout::GetSaveChance(GoodKeeper(), corner));
}

TEST(PenaltyKeeperTest, SaveChanceStaysAProbability) {
  for (float x = -goalHalfWidth; x <= goalHalfWidth; x += 0.5f) {
    PenaltyShootout::Aim aim;
    aim.x = x;
    aim.z = 1.2f;
    const float chance = PenaltyShootout::GetSaveChance(GoodKeeper(), aim);
    EXPECT_GE(chance, 0.0f);
    EXPECT_LE(chance, 1.0f);
  }
}

// --- Resolution ---

TEST(PenaltyResolveTest, AWildShotIsAMissWhateverTheKeeperDoes) {
  const PenaltyShootout::e_Outcome outcome =
      PenaltyShootout::ResolveKick(PoorShooter(), PoorKeeper(), 1.0f, 1.0f, 0.99f);
  EXPECT_EQ(outcome, PenaltyShootout::e_Outcome_Miss);
}

TEST(PenaltyResolveTest, AnOnTargetKickIsSavedWhenTheDiceFavourTheKeeper) {
  EXPECT_EQ(PenaltyShootout::ResolveKick(GoodShooter(), GoodKeeper(), 0.0f, 0.0f, 0.0f),
            PenaltyShootout::e_Outcome_Save);
}

TEST(PenaltyResolveTest, AWellPlacedKickBeatsTheKeeperWhenTheDiceFavourTheShooter) {
  EXPECT_EQ(PenaltyShootout::ResolveKick(GoodShooter(), PoorKeeper(), 0.8f, 0.8f, 0.99f),
            PenaltyShootout::e_Outcome_Goal);
}

// --- State machine ---

TEST(PenaltyStateTest, AFreshShootoutIsLevelAndUndecided) {
  const PenaltyShootout::State state = PenaltyShootout::Create(1);

  EXPECT_EQ(state.score[0], 0);
  EXPECT_EQ(state.score[1], 0);
  EXPECT_EQ(state.taken[0], 0);
  EXPECT_EQ(state.taken[1], 0);
  EXPECT_EQ(state.shootingTeam, 1);
  EXPECT_EQ(state.phase, PenaltyShootout::e_Phase_Positioning);
  EXPECT_FALSE(PenaltyShootout::IsDecided(state));
  EXPECT_EQ(PenaltyShootout::GetWinner(state), -1);
}

TEST(PenaltyStateTest, TheKickCycleWalksPositioningExecutionResolution) {
  PenaltyShootout::State state = PenaltyShootout::Create(0);

  PenaltyShootout::BeginKick(state);
  EXPECT_EQ(state.phase, PenaltyShootout::e_Phase_Execution);

  PenaltyShootout::ApplyOutcome(state, kGoal);
  EXPECT_EQ(state.phase, PenaltyShootout::e_Phase_Resolution);
  EXPECT_EQ(state.lastOutcome, kGoal);

  PenaltyShootout::NextKick(state);
  EXPECT_EQ(state.phase, PenaltyShootout::e_Phase_Positioning);
}

TEST(PenaltyStateTest, TeamsAlternateAndKicksAreCounted) {
  PenaltyShootout::State state = PlayOut({kGoal, kSave});

  EXPECT_EQ(state.taken[0], 1);
  EXPECT_EQ(state.taken[1], 1);
  EXPECT_EQ(state.score[0], 1);
  EXPECT_EQ(state.score[1], 0);
  EXPECT_EQ(state.shootingTeam, 0);  // back to the team that started
}

TEST(PenaltyStateTest, OnlyGoalsScore) {
  const PenaltyShootout::State state = PlayOut({kMiss, kSave});
  EXPECT_EQ(state.score[0], 0);
  EXPECT_EQ(state.score[1], 0);
  EXPECT_EQ(state.taken[0], 1);
  EXPECT_EQ(state.taken[1], 1);
}

TEST(PenaltyStateTest, FiveKicksEachDecidesTheWinner) {
  // 3-2 after five kicks each.
  const PenaltyShootout::State state =
      PlayOut({kGoal, kGoal, kGoal, kGoal, kSave, kSave, kGoal, kSave, kSave, kSave});

  EXPECT_EQ(state.score[0], 3);
  EXPECT_EQ(state.score[1], 2);
  EXPECT_TRUE(PenaltyShootout::IsDecided(state));
  EXPECT_EQ(PenaltyShootout::GetWinner(state), 0);
  EXPECT_EQ(state.phase, PenaltyShootout::e_Phase_Finished);
}

TEST(PenaltyStateTest, AnUnassailableLeadEndsTheShootoutEarly) {
  // Team 0 scores three, team 1 misses three: 3-0 with two kicks left each.
  const PenaltyShootout::State state = PlayOut({kGoal, kSave, kGoal, kSave, kGoal, kSave});

  EXPECT_TRUE(PenaltyShootout::IsDecided(state));
  EXPECT_EQ(PenaltyShootout::GetWinner(state), 0);
  EXPECT_EQ(state.taken[0], 3);
  EXPECT_EQ(state.taken[1], 3);
}

TEST(PenaltyStateTest, ALeadThatCanStillBeCaughtDoesNotEndTheShootout) {
  // 2-0 after two kicks each: team 1 still has three kicks to take.
  const PenaltyShootout::State state = PlayOut({kGoal, kSave, kGoal, kSave});

  EXPECT_FALSE(PenaltyShootout::IsDecided(state));
  EXPECT_EQ(PenaltyShootout::GetWinner(state), -1);
}

TEST(PenaltyStateTest, LevelAfterFiveEachGoesToSuddenDeath) {
  const PenaltyShootout::State state =
      PlayOut({kGoal, kGoal, kGoal, kGoal, kSave, kSave, kGoal, kGoal, kSave, kSave});

  EXPECT_EQ(state.score[0], 3);
  EXPECT_EQ(state.score[1], 3);
  EXPECT_FALSE(PenaltyShootout::IsDecided(state));
  EXPECT_TRUE(PenaltyShootout::IsSuddenDeath(state));
}

TEST(PenaltyStateTest, SuddenDeathIsDecidedOnlyOnCompletedRounds) {
  PenaltyShootout::State state =
      PlayOut({kGoal, kGoal, kGoal, kGoal, kSave, kSave, kGoal, kGoal, kSave, kSave});
  ASSERT_TRUE(PenaltyShootout::IsSuddenDeath(state));

  // Round six: team 0 scores, team 1 still to shoot.
  PenaltyShootout::BeginKick(state);
  PenaltyShootout::ApplyOutcome(state, kGoal);
  PenaltyShootout::NextKick(state);
  EXPECT_FALSE(PenaltyShootout::IsDecided(state));

  // Team 1 misses: shootout over.
  PenaltyShootout::BeginKick(state);
  PenaltyShootout::ApplyOutcome(state, kSave);
  PenaltyShootout::NextKick(state);
  EXPECT_TRUE(PenaltyShootout::IsDecided(state));
  EXPECT_EQ(PenaltyShootout::GetWinner(state), 0);
}

TEST(PenaltyStateTest, SuddenDeathContinuesWhileBothTeamsKeepScoring) {
  PenaltyShootout::State state =
      PlayOut({kGoal, kGoal, kGoal, kGoal, kSave, kSave, kGoal, kGoal, kSave, kSave});

  for (int round = 0; round < 3; round++) {
    PenaltyShootout::BeginKick(state);
    PenaltyShootout::ApplyOutcome(state, kGoal);
    PenaltyShootout::NextKick(state);
    PenaltyShootout::BeginKick(state);
    PenaltyShootout::ApplyOutcome(state, kGoal);
    PenaltyShootout::NextKick(state);
    EXPECT_FALSE(PenaltyShootout::IsDecided(state)) << "sudden death round " << round;
  }
  EXPECT_EQ(state.score[0], 6);
  EXPECT_EQ(state.score[1], 6);
}

TEST(PenaltyStateTest, TheSecondTeamCanWinItToo) {
  const PenaltyShootout::State state = PlayOut({kSave, kGoal, kSave, kGoal, kSave, kGoal});

  EXPECT_TRUE(PenaltyShootout::IsDecided(state));
  EXPECT_EQ(PenaltyShootout::GetWinner(state), 1);
}

TEST(PenaltyStateTest, TheTeamShootingSecondIsTrackedFromTheStart) {
  const PenaltyShootout::State state = PlayOut({kGoal}, 1);
  EXPECT_EQ(state.taken[1], 1);
  EXPECT_EQ(state.score[1], 1);
  EXPECT_EQ(state.shootingTeam, 0);
}

// --- Sequencing helpers used by the shootout controller ---

TEST(PenaltySequenceTest, EveryLivePhaseHasAVisibleDuration) {
  EXPECT_GT(PenaltyShootout::GetPhaseDuration_ms(PenaltyShootout::e_Phase_Positioning), 0UL);
  EXPECT_GT(PenaltyShootout::GetPhaseDuration_ms(PenaltyShootout::e_Phase_Execution), 0UL);
  EXPECT_GT(PenaltyShootout::GetPhaseDuration_ms(PenaltyShootout::e_Phase_Resolution), 0UL);
  EXPECT_EQ(PenaltyShootout::GetPhaseDuration_ms(PenaltyShootout::e_Phase_Finished), 0UL);
}

TEST(PenaltyTakerTest, TheBestAvailableStrikerTakesTheKick) {
  const std::vector<float> ratings = {0.4f, 0.9f, 0.7f};
  EXPECT_EQ(PenaltyShootout::SelectTakerIndex(ratings, 0), 1);
}

TEST(PenaltyTakerTest, NobodyTakesTwoKicksBeforeEveryoneHasTakenOne) {
  const std::vector<float> ratings = {0.4f, 0.9f, 0.7f};
  unsigned int taken = 0;

  const int first = PenaltyShootout::SelectTakerIndex(ratings, taken);
  taken = PenaltyShootout::MarkTaken(taken, first, ratings.size());
  const int second = PenaltyShootout::SelectTakerIndex(ratings, taken);
  taken = PenaltyShootout::MarkTaken(taken, second, ratings.size());
  const int third = PenaltyShootout::SelectTakerIndex(ratings, taken);

  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 2);
  EXPECT_EQ(third, 0);
}

TEST(PenaltyTakerTest, TheRoundResetsOnceEveryoneHasTaken) {
  const std::vector<float> ratings = {0.4f, 0.9f, 0.7f};
  unsigned int taken = 0;
  for (size_t i = 0; i < ratings.size(); i++)
    taken = PenaltyShootout::MarkTaken(taken, PenaltyShootout::SelectTakerIndex(ratings, taken),
                                       ratings.size());

  EXPECT_EQ(taken, 0u);
  // ...so the best striker is back up first in the next round.
  EXPECT_EQ(PenaltyShootout::SelectTakerIndex(ratings, taken), 1);
}

TEST(PenaltyTakerTest, AFullMaskStillYieldsATaker) {
  const std::vector<float> ratings = {0.4f, 0.9f};
  EXPECT_EQ(PenaltyShootout::SelectTakerIndex(ratings, 0x3), 1);
}

TEST(PenaltyTakerTest, AnEmptySquadHasNoTaker) {
  const std::vector<float> ratings;
  EXPECT_EQ(PenaltyShootout::SelectTakerIndex(ratings, 0), -1);
  EXPECT_EQ(PenaltyShootout::MarkTaken(0, -1, 0), 0u);
}

// --- Reading the outcome of a kick that is actually played out on the pitch ---
//
// The kick is taken with the engine's own penalty set piece (real shot and dive
// animations), so the outcome has to be observed rather than decided up front.

namespace {

PenaltyShootout::KickObservation Observation() {
  PenaltyShootout::KickObservation observation;
  return observation;
}

}  // namespace

TEST(PenaltyObserveTest, NothingHasHappenedYetWhileTheBallIsStillTravelling) {
  EXPECT_EQ(PenaltyShootout::ObserveKick(Observation(), 500), PenaltyShootout::e_Outcome_Pending);
}

TEST(PenaltyObserveTest, ABallInTheNetIsAGoal) {
  PenaltyShootout::KickObservation observation = Observation();
  observation.goalDetected = true;
  EXPECT_EQ(PenaltyShootout::ObserveKick(observation, 500), PenaltyShootout::e_Outcome_Goal);
}

TEST(PenaltyObserveTest, TheKeeperGatheringItIsASave) {
  PenaltyShootout::KickObservation observation = Observation();
  observation.keeperHasBall = true;
  EXPECT_EQ(PenaltyShootout::ObserveKick(observation, 500), PenaltyShootout::e_Outcome_Save);
}

TEST(PenaltyObserveTest, AKeeperTouchThatKeepsItOutIsASave) {
  PenaltyShootout::KickObservation observation = Observation();
  observation.keeperTouchedBall = true;
  observation.ballStopped = true;
  EXPECT_EQ(PenaltyShootout::ObserveKick(observation, 500), PenaltyShootout::e_Outcome_Save);
}

TEST(PenaltyObserveTest, AKickThatLeavesThePitchIsAMiss) {
  PenaltyShootout::KickObservation observation = Observation();
  observation.ballLeftField = true;
  EXPECT_EQ(PenaltyShootout::ObserveKick(observation, 500), PenaltyShootout::e_Outcome_Miss);
}

TEST(PenaltyObserveTest, ABallThatComesToRestWithoutAGoalIsAMiss) {
  PenaltyShootout::KickObservation observation = Observation();
  observation.ballStopped = true;
  EXPECT_EQ(PenaltyShootout::ObserveKick(observation, 500), PenaltyShootout::e_Outcome_Miss);
}

TEST(PenaltyObserveTest, TheKickIsGivenUpOnAfterTheTimeout) {
  EXPECT_EQ(PenaltyShootout::ObserveKick(Observation(), PenaltyShootout::kickTimeout_ms),
            PenaltyShootout::e_Outcome_Miss);
  EXPECT_EQ(PenaltyShootout::ObserveKick(Observation(), PenaltyShootout::kickTimeout_ms + 1000),
            PenaltyShootout::e_Outcome_Miss);
}

TEST(PenaltyObserveTest, AGoalCountsEvenIfTheKeeperGotAHandToIt) {
  PenaltyShootout::KickObservation observation = Observation();
  observation.goalDetected = true;
  observation.keeperTouchedBall = true;
  observation.ballStopped = true;
  EXPECT_EQ(PenaltyShootout::ObserveKick(observation, 5000), PenaltyShootout::e_Outcome_Goal);
}

TEST(PenaltyObserveTest, TheTimeoutIsLongEnoughForTheBallToReachTheGoal) {
  EXPECT_GE(PenaltyShootout::kickTimeout_ms, 2000UL);
  EXPECT_LE(PenaltyShootout::kickTimeout_ms, 10000UL);
}

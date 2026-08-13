// Tests for the AI manager: the CPU's own use of the new tactical features.
// Any team that is not run by a human coach gets its philosophy and its
// substitutions from here.

#include "onthepitch/aimanager.hpp"

#include <gtest/gtest.h>

namespace {

constexpr unsigned long Minutes(float minutes) {
  return static_cast<unsigned long>(minutes * 60000.0f);
}

AIManager::MatchSituation Situation(int goalDifference, unsigned long matchTime_ms) {
  AIManager::MatchSituation situation;
  situation.goalDifference = goalDifference;
  situation.matchTime_ms = matchTime_ms;
  situation.possessionShare = 0.5f;
  return situation;
}

AIManager::SubstitutionCandidate Candidate(float fatigue, float injury, bool onPitch) {
  AIManager::SubstitutionCandidate candidate;
  candidate.fatigueFactorInv = 1.0f - fatigue;
  candidate.injuryLevel = injury;
  candidate.isOnPitch = onPitch;
  candidate.averageStat = 0.6f;
  return candidate;
}

}  // namespace

// --- Philosophy selection ---

TEST(AIManagerPhilosophyTest, DefaultsToTheTeamsOwnPreferenceEarlyOn) {
  const TeamPhilosophy::e_Philosophy chosen = AIManager::ChoosePhilosophy(
      TeamPhilosophy::e_Philosophy_TikiTaka, Situation(0, Minutes(20)));
  EXPECT_EQ(chosen, TeamPhilosophy::e_Philosophy_TikiTaka);
}

TEST(AIManagerPhilosophyTest, ChasesTheGameWithHighPressingLateOn) {
  const TeamPhilosophy::e_Philosophy chosen = AIManager::ChoosePhilosophy(
      TeamPhilosophy::e_Philosophy_Balanced, Situation(-1, Minutes(80)));
  EXPECT_EQ(chosen, TeamPhilosophy::e_Philosophy_Gegenpressing);
}

TEST(AIManagerPhilosophyTest, ProtectsANarrowLeadInTheClosingMinutes) {
  const TeamPhilosophy::e_Philosophy chosen = AIManager::ChoosePhilosophy(
      TeamPhilosophy::e_Philosophy_Balanced, Situation(1, Minutes(85)));
  EXPECT_EQ(chosen, TeamPhilosophy::e_Philosophy_ParkTheBus);
}

TEST(AIManagerPhilosophyTest, KeepsTheBallWhenComfortablyAheadWithTimeToPlay) {
  const TeamPhilosophy::e_Philosophy chosen = AIManager::ChoosePhilosophy(
      TeamPhilosophy::e_Philosophy_Balanced, Situation(2, Minutes(70)));
  EXPECT_EQ(chosen, TeamPhilosophy::e_Philosophy_TikiTaka);
}

TEST(AIManagerPhilosophyTest, ATeamParkingTheBusByDesignIsNotTalkedIntoPressing) {
  // An explicitly defensive side stays defensive when it is winning.
  EXPECT_EQ(AIManager::ChoosePhilosophy(TeamPhilosophy::e_Philosophy_ParkTheBus,
                                        Situation(1, Minutes(85))),
            TeamPhilosophy::e_Philosophy_ParkTheBus);
  // But it will still chase a deficit rather than sit on a loss.
  EXPECT_EQ(AIManager::ChoosePhilosophy(TeamPhilosophy::e_Philosophy_ParkTheBus,
                                        Situation(-2, Minutes(82))),
            TeamPhilosophy::e_Philosophy_Gegenpressing);
}

TEST(AIManagerPhilosophyTest, TheChoiceIsStableAcrossRepeatedCalls) {
  const AIManager::MatchSituation situation = Situation(-1, Minutes(88));
  const TeamPhilosophy::e_Philosophy first =
      AIManager::ChoosePhilosophy(TeamPhilosophy::e_Philosophy_Balanced, situation);
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(AIManager::ChoosePhilosophy(TeamPhilosophy::e_Philosophy_Balanced, situation), first);
  }
}

// --- Substitution decisions ---

TEST(AIManagerSubstitutionTest, MakesNoSubstitutionWhileEveryoneIsFresh) {
  std::vector<AIManager::SubstitutionCandidate> squad = {
      Candidate(0.0f, 0.0f, true), Candidate(0.05f, 0.0f, true), Candidate(0.0f, 0.0f, false)};

  const AIManager::SubstitutionPlan plan =
      AIManager::PlanSubstitution(squad, Situation(0, Minutes(60)), 3);
  EXPECT_FALSE(plan.wanted);
}

TEST(AIManagerSubstitutionTest, TakesOffAnExhaustedPlayerForAFreshOne) {
  std::vector<AIManager::SubstitutionCandidate> squad = {
      Candidate(0.0f, 0.0f, true),   // fresh starter
      Candidate(0.75f, 0.0f, true),  // dead on his feet
      Candidate(0.0f, 0.0f, false),  // bench
  };

  const AIManager::SubstitutionPlan plan =
      AIManager::PlanSubstitution(squad, Situation(0, Minutes(65)), 3);
  EXPECT_TRUE(plan.wanted);
  EXPECT_EQ(plan.playerOutIndex, 1);
  EXPECT_EQ(plan.playerInIndex, 2);
}

TEST(AIManagerSubstitutionTest, AnInjuredPlayerComesOffAheadOfATiredOne) {
  std::vector<AIManager::SubstitutionCandidate> squad = {
      Candidate(0.8f, 0.0f, true),   // exhausted
      Candidate(0.1f, 0.5f, true),   // injured
      Candidate(0.0f, 0.0f, false),
  };

  const AIManager::SubstitutionPlan plan =
      AIManager::PlanSubstitution(squad, Situation(0, Minutes(50)), 3);
  EXPECT_TRUE(plan.wanted);
  EXPECT_EQ(plan.playerOutIndex, 1);
}

TEST(AIManagerSubstitutionTest, PicksTheBestAvailableReplacement) {
  std::vector<AIManager::SubstitutionCandidate> squad = {
      Candidate(0.8f, 0.0f, true),
      Candidate(0.0f, 0.0f, false),
      Candidate(0.0f, 0.0f, false),
  };
  squad[1].averageStat = 0.4f;
  squad[2].averageStat = 0.8f;

  const AIManager::SubstitutionPlan plan =
      AIManager::PlanSubstitution(squad, Situation(0, Minutes(70)), 3);
  EXPECT_TRUE(plan.wanted);
  EXPECT_EQ(plan.playerInIndex, 2);
}

TEST(AIManagerSubstitutionTest, DoesNothingWithoutSubstitutionsLeft) {
  std::vector<AIManager::SubstitutionCandidate> squad = {Candidate(0.9f, 0.0f, true),
                                                        Candidate(0.0f, 0.0f, false)};

  const AIManager::SubstitutionPlan plan =
      AIManager::PlanSubstitution(squad, Situation(0, Minutes(70)), 0);
  EXPECT_FALSE(plan.wanted);
}

TEST(AIManagerSubstitutionTest, DoesNothingWithAnEmptyBench) {
  std::vector<AIManager::SubstitutionCandidate> squad = {Candidate(0.9f, 0.0f, true),
                                                        Candidate(0.9f, 0.0f, true)};

  const AIManager::SubstitutionPlan plan =
      AIManager::PlanSubstitution(squad, Situation(0, Minutes(70)), 3);
  EXPECT_FALSE(plan.wanted);
}

TEST(AIManagerSubstitutionTest, HoldsSubstitutionsBackEarlyInTheMatch) {
  std::vector<AIManager::SubstitutionCandidate> squad = {Candidate(0.7f, 0.0f, true),
                                                        Candidate(0.0f, 0.0f, false)};

  // Tired after ten minutes is not a reason to burn a substitution.
  EXPECT_FALSE(AIManager::PlanSubstitution(squad, Situation(0, Minutes(10)), 3).wanted);
  EXPECT_TRUE(AIManager::PlanSubstitution(squad, Situation(0, Minutes(60)), 3).wanted);
}

TEST(AIManagerSubstitutionTest, AnInjuryIsWorthASubstitutionAtAnyTime) {
  std::vector<AIManager::SubstitutionCandidate> squad = {Candidate(0.0f, 0.6f, true),
                                                        Candidate(0.0f, 0.0f, false)};

  const AIManager::SubstitutionPlan plan =
      AIManager::PlanSubstitution(squad, Situation(0, Minutes(8)), 3);
  EXPECT_TRUE(plan.wanted);
  EXPECT_EQ(plan.playerOutIndex, 0);
}

TEST(AIManagerSubstitutionTest, NeverSuggestsSwappingAPlayerWithHimself) {
  std::vector<AIManager::SubstitutionCandidate> squad = {Candidate(0.9f, 0.0f, true),
                                                        Candidate(0.0f, 0.0f, false)};

  const AIManager::SubstitutionPlan plan =
      AIManager::PlanSubstitution(squad, Situation(0, Minutes(70)), 3);
  ASSERT_TRUE(plan.wanted);
  EXPECT_NE(plan.playerOutIndex, plan.playerInIndex);
  EXPECT_TRUE(squad[plan.playerOutIndex].isOnPitch);
  EXPECT_FALSE(squad[plan.playerInIndex].isOnPitch);
}

// --- Formation selection ---

TEST(AIManagerFormationTest, KeepsTheTeamsOwnShapeWhileTheGameIsAlive) {
  EXPECT_EQ(AIManager::ChooseFormation(Formations::e_Formation_433, Situation(0, Minutes(30))),
            Formations::e_Formation_433);
  EXPECT_EQ(AIManager::ChooseFormation(Formations::e_Formation_352, Situation(1, Minutes(60))),
            Formations::e_Formation_352);
}

TEST(AIManagerFormationTest, ThrowsAnExtraForwardOnWhenChasingLate) {
  const Formations::e_Formation chosen =
      AIManager::ChooseFormation(Formations::e_Formation_442, Situation(-1, Minutes(80)));
  EXPECT_GT(Formations::GetShape(chosen).forwards,
            Formations::GetShape(Formations::e_Formation_442).forwards);
}

TEST(AIManagerFormationTest, GoesAllOutWhenTwoDown) {
  const Formations::e_Formation chosen =
      AIManager::ChooseFormation(Formations::e_Formation_442, Situation(-2, Minutes(85)));
  EXPECT_EQ(Formations::GetShape(chosen).forwards, 4);
}

TEST(AIManagerFormationTest, PacksTheDefenceToSeeOutALead) {
  const Formations::e_Formation chosen =
      AIManager::ChooseFormation(Formations::e_Formation_442, Situation(1, Minutes(85)));
  EXPECT_GE(Formations::GetShape(chosen).defenders, 5);
}

TEST(AIManagerFormationTest, TheChoiceIsStableWhileTheSituationIs) {
  const AIManager::MatchSituation situation = Situation(-1, Minutes(84));
  const Formations::e_Formation first =
      AIManager::ChooseFormation(Formations::e_Formation_451, situation);
  for (int i = 0; i < 4; i++)
    EXPECT_EQ(AIManager::ChooseFormation(Formations::e_Formation_451, situation), first);
}

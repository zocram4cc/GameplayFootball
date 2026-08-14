// Law 12: careless / reckless / excessive force is judged by what the
// challenge was, not by which animation produced it. GF's card ladder used to
// be reachable only from a sliding tackle; standing tackles were hardcoded to
// a plain foul and light trips were dropped entirely (RULESET_AUDIT.md gap 3).

#include <cmath>
#include <gtest/gtest.h>

#include "onthepitch/foulseverity.hpp"
#include "onthepitch/refereeprofile.hpp"

namespace {

FoulSeverity::Contact Slide(bool hasTouchData, float timingError, float ballDistance,
                            float fromBehind) {
  FoulSeverity::Contact contact;
  contact.tackleType = 3;
  contact.hasTouchData = hasTouchData;
  contact.timingError = timingError;
  contact.ballDistance_m = ballDistance;
  contact.fromBehind = fromBehind;
  return contact;
}

FoulSeverity::Contact Standing(int tackleType, float ballDistance, float fromBehind) {
  FoulSeverity::Contact contact;
  contact.tackleType = tackleType;
  contact.hasTouchData = false;
  contact.ballDistance_m = ballDistance;
  contact.fromBehind = fromBehind;
  return contact;
}

int TypeOf(const FoulSeverity::Contact& contact) {
  return RefereeProfile::GetFoulType(RefereeProfile::e_Profile_Standard,
                                     FoulSeverity::Score(contact));
}

}  // namespace

// --- Sliding tackles keep their historical judgement exactly ---

TEST(FoulSeverityTest, SlideScoreReproducesTheHistoricalFormula) {
  // timing^0.7 * 0.5 + (dist/2) * 0.5 + fromBehind
  const float expected = std::pow(0.5f, 0.7f) * 0.5f + (1.0f / 2.0f) * 0.5f + 0.75f;
  EXPECT_NEAR(FoulSeverity::Score(Slide(true, 0.5f, 1.0f, 0.75f)), expected, 1e-5f);
}

TEST(FoulSeverityTest, SlideWithoutTouchDataKeepsTheHistoricalBase) {
  EXPECT_NEAR(FoulSeverity::Score(Slide(false, 0.0f, 0.0f, 0.3f)), 1.3f, 1e-5f);
}

TEST(FoulSeverityTest, ACleanFrontalSlideIsPlayOn) {
  EXPECT_EQ(TypeOf(Slide(true, 0.0f, 0.0f, 0.0f)), 0);
}

TEST(FoulSeverityTest, ARecklessSlideFromBehindIsStillARed) {
  EXPECT_EQ(TypeOf(Slide(false, 0.0f, 0.0f, 1.0f)), 3);
}

// --- Standing tackles are no longer automatically innocent ---

TEST(FoulSeverityTest, AFrontalStandingTackleThatFloorsAManIsAFoul) {
  EXPECT_EQ(TypeOf(Standing(2, 0.0f, 0.0f)), 1);
}

TEST(FoulSeverityTest, AStandingTackleFromBehindOffTheBallIsBooked) {
  EXPECT_GE(TypeOf(Standing(2, 1.5f, 1.0f)), 2);
}

// --- Light contact (interfere trips, shirt-pull-type fouls) is judged too ---

TEST(FoulSeverityTest, LightFrontalContactNearTheBallIsPlayOn) {
  EXPECT_EQ(TypeOf(Standing(1, 0.0f, 0.0f)), 0);
}

TEST(FoulSeverityTest, ALightTripFromBehindAwayFromTheBallCanBeBooked) {
  EXPECT_GE(TypeOf(Standing(1, 2.0f, 1.0f)), 2);
}

// --- Inputs are clamped so producers cannot feed the ladder garbage ---

TEST(FoulSeverityTest, InputsAreClamped) {
  EXPECT_NEAR(FoulSeverity::Score(Slide(true, 5.0f, 100.0f, 2.0f)),
              FoulSeverity::Score(Slide(true, 1.0f, 2.0f, 1.0f)), 1e-5f);
  EXPECT_NEAR(FoulSeverity::Score(Slide(true, -1.0f, -5.0f, -1.0f)),
              FoulSeverity::Score(Slide(true, 0.0f, 0.0f, 0.0f)), 1e-5f);
}

// --- DOGSO: denying an obvious goal-scoring opportunity (Law 12) ---
//
// PES 2021's check, adopted: the fouled player is ahead of the defensive
// reference line in the attack direction and within the width of the penalty
// area. attackDirSign is the sign of the attacked goal's x.

TEST(FoulSeverityDOGSOTest, ThroughOnGoalInTheCentralChannelIsDOGSO) {
  EXPECT_TRUE(FoulSeverity::DeniesObviousChance(1.0f, 30.0f, 5.0f, 25.0f, 20.0f));
  EXPECT_TRUE(FoulSeverity::DeniesObviousChance(-1.0f, -30.0f, -5.0f, -25.0f, 20.0f));
}

TEST(FoulSeverityDOGSOTest, BehindTheDefensiveLineIsNotDOGSO) {
  EXPECT_FALSE(FoulSeverity::DeniesObviousChance(1.0f, 20.0f, 5.0f, 25.0f, 20.0f));
  EXPECT_FALSE(FoulSeverity::DeniesObviousChance(-1.0f, -20.0f, -5.0f, -25.0f, 20.0f));
}

TEST(FoulSeverityDOGSOTest, OutWideIsNotDOGSO) {
  EXPECT_FALSE(FoulSeverity::DeniesObviousChance(1.0f, 30.0f, 25.0f, 25.0f, 20.0f));
  EXPECT_FALSE(FoulSeverity::DeniesObviousChance(1.0f, 30.0f, -25.0f, 25.0f, 20.0f));
}

// IFAB 2016: DOGSO is a sending-off, except in the penalty area where a
// genuine attempt to play the ball reduces it to a caution (the penalty kick
// restores the lost chance). PES caps every DOGSO to a yellow everywhere,
// which the audit explicitly says not to copy.

TEST(FoulSeverityDOGSOTest, DOGSOOutsideTheBoxIsAStraightRed) {
  EXPECT_EQ(FoulSeverity::EscalateForDOGSO(1, false, true), 3);
  EXPECT_EQ(FoulSeverity::EscalateForDOGSO(1, false, false), 3);
}

TEST(FoulSeverityDOGSOTest, DOGSOInTheBoxWithAPlayOnTheBallIsACaution) {
  EXPECT_EQ(FoulSeverity::EscalateForDOGSO(1, true, true), 2);
  // ...but never downgrades a worse offence.
  EXPECT_EQ(FoulSeverity::EscalateForDOGSO(3, true, true), 3);
}

TEST(FoulSeverityDOGSOTest, DOGSOInTheBoxWithNoAttemptAtTheBallIsStillARed) {
  EXPECT_EQ(FoulSeverity::EscalateForDOGSO(1, true, false), 3);
}

TEST(FoulSeverityDOGSOTest, NoFoulMeansNothingToEscalate) {
  EXPECT_EQ(FoulSeverity::EscalateForDOGSO(0, false, false), 0);
}

// Worse challenges never score lower.
TEST(FoulSeverityTest, ScoreIsMonotonicInEveryTerm) {
  for (int type : {1, 2}) {
    EXPECT_LT(FoulSeverity::Score(Standing(type, 0.5f, 0.2f)),
              FoulSeverity::Score(Standing(type, 0.5f, 0.8f)));
    EXPECT_LT(FoulSeverity::Score(Standing(type, 0.5f, 0.2f)),
              FoulSeverity::Score(Standing(type, 1.8f, 0.2f)));
  }
}

// PES 2021's foul decision (docs/re/pes-re-duels-fouls.md 2). Synthetic
// constants exercise the formulas; one install test checks the loader against
// the user's own PES2021.exe through relations, not pinned numbers.
#include <gtest/gtest.h>

#include <cstdlib>

#include "onthepitch/pesjudge.hpp"

namespace {

pes::JudgeConstants Synthetic() {
  pes::JudgeConstants k{};
  k.angleFloor_deg = 30.0f;
  k.angleSpan_deg = 100.0f;
  k.slideAngleSpan_deg = 90.0f;
  k.slideAngleKnee_deg = 70.0f;
  k.slideGateRamp_deg = 10.0f;
  k.slideAlignKnee_deg = 50.0f;
  k.slideAlignRamp_deg = -20.0f;
  k.angleMaxPoints = 50.0f;
  k.notFallenFactor = 0.5f;
  k.kmhPerMetrePerSecond = 4.0f;
  k.speedFloor_kmh = 4.0f;
  k.speedSpan_kmh = 16.0f;
  k.speedMaxPoints = 20.0f;
  k.fallenMinSpeedPoints = 7;
  k.jostleSpeedDivisor = 10;
  k.recklessPoints = 33;
  k.tacklePoints = 21;
  k.recklessFallenBonus = 11;
  k.tackleFallenBonus = 9;
  k.dogsoBonus = 27;
  k.foulThreshold = 30;
  k.boxFoulThreshold = 50;
  k.cautionThreshold = 70;
  k.redThreshold = 100;
  k.spotBias_m = 0.05f;
  k.whistleHold_s = 0.9f;
  k.advantageWindow_s = 3.0f;
  return k;
}

}  // namespace

TEST(PesJudge, TackleFromBehindScoresTheFullAngle) {
  const auto k = Synthetic();
  pes::JudgeContact c;
  c.action = pes::JudgeAction::Tackle;
  c.victimFell = true;
  c.approachAngle_deg = 130.0f;  // floor + span
  // angle 50 + speed floor-min 7 + action 21+9
  EXPECT_EQ(pes::JudgePoints(c, k), 50 + 7 + 30);
  c.approachAngle_deg = 0.0f;  // head on: no angle points
  EXPECT_EQ(pes::JudgePoints(c, k), 7 + 30);
}

TEST(PesJudge, StayingOnTheFeetScalesTheAngleAndDropsTheBonuses) {
  const auto k = Synthetic();
  pes::JudgeContact c;
  c.action = pes::JudgeAction::Tackle;
  c.approachAngle_deg = 130.0f;
  EXPECT_EQ(pes::JudgePoints(c, k), 25 + 0 + 21);
}

TEST(PesJudge, SpeedRampsBetweenFloorAndCap) {
  const auto k = Synthetic();
  pes::JudgeContact c;
  c.action = pes::JudgeAction::Tackle;
  c.relativeSpeed_mps = 3.0f;  // 12 km/h: (12-4)/16 = 0.5 -> 10
  EXPECT_EQ(pes::JudgePoints(c, k), 10 + 21);
  c.relativeSpeed_mps = 10.0f;  // 40 km/h >= the 20 cap -> full
  EXPECT_EQ(pes::JudgePoints(c, k), 20 + 21);
}

TEST(PesJudge, JostleScoresOnlyDividedSpeed) {
  const auto k = Synthetic();
  pes::JudgeContact c;
  c.action = pes::JudgeAction::Jostle;
  c.approachAngle_deg = 180.0f;
  c.relativeSpeed_mps = 10.0f;
  EXPECT_EQ(pes::JudgePoints(c, k), 20 / 10);
}

TEST(PesJudge, SlideAngleIsPulledBackOnlyWhenBothGatesOpen) {
  const auto k = Synthetic();
  pes::JudgeContact c;
  c.action = pes::JudgeAction::Slide;
  c.victimFell = true;
  c.approachAngle_deg = 120.0f;
  // Gates closed: theta stays 120 -> (120-30)/90 = 1 -> 50.
  c.slideOffLine_deg = 0.0f;
  c.headingsDelta_deg = 90.0f;
  const int open = pes::JudgePoints(c, k);
  EXPECT_EQ(open, 50 + 7 + 33 + 11);
  // Both gates fully open: theta pulled to the 70 knee -> (70-30)/90 * 50 = 22.
  c.slideOffLine_deg = 60.0f;
  c.headingsDelta_deg = 20.0f;
  EXPECT_EQ(pes::JudgePoints(c, k), 22 + 7 + 33 + 11);
}

TEST(PesJudge, DogsoAddsToAFallenReckless) {
  const auto k = Synthetic();
  pes::JudgeContact c;
  c.action = pes::JudgeAction::Slide;
  c.victimFell = true;
  const int base = pes::JudgePoints(c, k);
  c.dogso = true;
  EXPECT_EQ(pes::JudgePoints(c, k), base + 27);
  c.action = pes::JudgeAction::Tackle;
  const int tackle = pes::JudgePoints(c, k);
  c.dogso = false;
  EXPECT_EQ(pes::JudgePoints(c, k), tackle) << "a tackle does not take the DOGSO bonus";
}

TEST(PesJudge, LevelsFollowTheThresholdsAndTheWarningMidpoint) {
  const auto k = Synthetic();
  EXPECT_EQ(pes::JudgeLevelFromPoints(29, false, k), 0);
  EXPECT_EQ(pes::JudgeLevelFromPoints(30, false, k), 1);
  EXPECT_EQ(pes::JudgeLevelFromPoints(50, false, k), 1) << "the midpoint itself is no warning";
  EXPECT_EQ(pes::JudgeLevelFromPoints(51, false, k), 2);
  EXPECT_EQ(pes::JudgeLevelFromPoints(70, false, k), 3);
  EXPECT_EQ(pes::JudgeLevelFromPoints(100, false, k), 4);
  EXPECT_EQ(pes::JudgeLevelFromPoints(49, true, k), 0) << "the box raises the foul threshold";
  EXPECT_EQ(pes::JudgeLevelFromPoints(61, true, k), 2);
}

TEST(PesJudge, PenaltyAreaNeedsAFallAndCardsOnlyForDogso) {
  const auto k = Synthetic();
  pes::JudgeContact c;
  c.action = pes::JudgeAction::Slide;
  c.inAttackingBox = true;
  c.approachAngle_deg = 120.0f;
  c.relativeSpeed_mps = 10.0f;
  EXPECT_EQ(pes::JudgeLevel(c, k), 0) << "no foul in the box unless the victim fell";
  c.victimFell = true;
  EXPECT_EQ(pes::JudgeLevel(c, k), 1) << "a red-level penalty without DOGSO is no card";
  c.dogso = true;
  EXPECT_EQ(pes::JudgeLevel(c, k), 3) << "DOGSO red in the box drops to yellow";
  c.inAttackingBox = false;
  EXPECT_EQ(pes::JudgeLevel(c, k), 4);
}

TEST(PesJudgeAdvantage, DecisionOnlyInTheLastThirdOfTheHold) {
  const auto k = Synthetic();
  EXPECT_FALSE(pes::InAdvantageDecisionWindow(0.5f, k));
  EXPECT_TRUE(pes::InAdvantageDecisionWindow(0.6f, k));
  EXPECT_FALSE(pes::InAdvantageDecisionWindow(0.9f, k)) << "the hold is over: whistle";
}

TEST(PesJudgeAdvantage, NeverOnAPenaltyOrARedAndNotForABookedMansYellow) {
  EXPECT_TRUE(pes::ShouldPlayAdvantage(1, false, false, 3.0f, 5.0f));
  EXPECT_FALSE(pes::ShouldPlayAdvantage(1, true, false, 3.0f, 5.0f));
  EXPECT_FALSE(pes::ShouldPlayAdvantage(4, false, false, 3.0f, 5.0f));
  EXPECT_TRUE(pes::ShouldPlayAdvantage(3, false, false, 3.0f, 5.0f));
  EXPECT_FALSE(pes::ShouldPlayAdvantage(3, false, true, 3.0f, 5.0f));
  EXPECT_FALSE(pes::ShouldPlayAdvantage(1, false, false, 5.0f, 3.0f))
      << "the fouling team is nearer the ball";
}

TEST(PesJudgeAdvantage, ClockRunsDoubleWithTheBallAndPassesBurnASecond) {
  EXPECT_FLOAT_EQ(pes::AdvanceAdvantage(3.0f, 0.01f, false, false), 2.99f);
  EXPECT_FLOAT_EQ(pes::AdvanceAdvantage(3.0f, 0.01f, true, false), 2.98f);
  EXPECT_FLOAT_EQ(pes::AdvanceAdvantage(3.0f, 0.01f, true, true), 1.98f);
  EXPECT_FLOAT_EQ(pes::AdvanceAdvantage(0.5f, 0.01f, false, true), 0.0f);
}

// The chief referee's restart marks (docs/re/pes-re-protection.md 3.3). Every
// constant distinct, so a constant used in the wrong place moves a mark.
pes::RefereeSpotConstants SyntheticSpot() {
  pes::RefereeSpotConstants k{};
  k.kickOffBehind_m = 6.0f;
  k.kickOffAcross_m = 11.0f;
  k.throwInUpfieldGate_m = 2.0f;
  k.throwInTrail_m = 7.0f;
  k.throwInLeadDivisor = 4.0f;
  k.throwInLeadBase_m = 9.0f;
  k.throwInInset_m = 5.0f;
  k.goalKickX_m = -3.0f;
  k.goalKickDrop_m = 1.5f;
  k.cornerInset_m = 0.75f;
  k.cornerOut_m = 2.5f;
  k.penaltyAhead_m = 4.25f;
  k.penaltyOut_m = 5.25f;
  k.fkHalf = 0.5f;
  k.fkDeepX_m = -13.0f;
  k.fkOwnHalfOut_m = 1.25f;
  k.fkOwnBoxX_m = 2.5f;
  k.fkOwnBoxIn_m = 3.25f;
  k.fkWideDivisor = 4.0f;
  k.fkBoxCornerInset_m = 4.5f;
  k.fkBoxCornerOut_m = 2.25f;
  k.fkQuarter = 0.2f;
  k.fkEdgeInset_m = 3.75f;
  k.fkEdgeOut_m = 3.5f;
  k.fkWideOut_m = 1.75f;
  k.fkFarAcross_m = 9.5f;
  k.fkCentreAhead_m = 10.5f;
  k.fkCentreAcross_m = 8.5f;
  k.fkFlankAhead_m = 5.5f;
  k.fkFlankAcross_m = 14.5f;
  return k;
}

constexpr pes::FieldMarks SYNTHETIC_FIELD{50.0f, 10.0f, 30.0f, 20.0f};

void ExpectSpot(e_SetPiece kind, pes::PitchSpot restart, int dir, pes::PitchSpot want) {
  const auto got = pes::ChiefRefereeSpot(kind, restart, dir, SYNTHETIC_FIELD, SyntheticSpot());
  ASSERT_TRUE(got) << "restart (" << restart.x << ", " << restart.z << ")";
  EXPECT_FLOAT_EQ(got->x, want.x) << "restart (" << restart.x << ", " << restart.z << ")";
  EXPECT_FLOAT_EQ(got->z, want.z) << "restart (" << restart.x << ", " << restart.z << ")";
}

TEST(PesRefereeSpot, KickOffBehindAndAcrossFromTheKickers) {
  ExpectSpot(e_SetPiece_KickOff, {0, 0}, 1, {-6.0f, -11.0f});
  ExpectSpot(e_SetPiece_KickOff, {0, 0}, -1, {6.0f, 11.0f});
}

TEST(PesRefereeSpot, GoalKickDropIsNotMirrored) {
  ExpectSpot(e_SetPiece_GoalKick, {-45.0f, 3.0f}, 1, {3.0f, 8.5f});
  ExpectSpot(e_SetPiece_GoalKick, {-45.0f, 0.0f}, 1, {3.0f, 8.5f});  // PES's zero is positive
  ExpectSpot(e_SetPiece_GoalKick, {-45.0f, -3.0f}, 1, {3.0f, -11.5f});
}

TEST(PesRefereeSpot, CornerAndPenaltyTakeBothSignsFromTheSpotX) {
  ExpectSpot(e_SetPiece_Corner, {50.0f, -30.0f}, 1, {29.25f, -12.5f});
  ExpectSpot(e_SetPiece_Corner, {-50.0f, -30.0f}, -1, {-29.25f, 12.5f});
  ExpectSpot(e_SetPiece_Penalty, {39.0f, 0.0f}, 1, {34.25f, -15.25f});
}

TEST(PesRefereeSpot, ThrowInTrailsOrLeadsThenClampsAtTheBox) {
  ExpectSpot(e_SetPiece_ThrowIn, {20.0f, 34.0f}, 1, {13.0f, 15.0f});
  ExpectSpot(e_SetPiece_ThrowIn, {-20.0f, -34.0f}, 1, {-6.0f, -15.0f});
  ExpectSpot(e_SetPiece_ThrowIn, {-40.0f, -34.0f}, -1, {-25.0f, -10.0f});
  ExpectSpot(e_SetPiece_ThrowIn, {40.0f, 34.0f}, 1, {25.0f, 10.0f});
}

TEST(PesRefereeSpot, FreeKickInTheKickersOwnHalf) {
  ExpectSpot(e_SetPiece_FreeKick, {-10.0f, 5.0f}, 1, {13.0f, 11.25f});
  ExpectSpot(e_SetPiece_FreeKick, {-20.0f, -5.0f}, 1, {-2.0f, -11.25f});
  ExpectSpot(e_SetPiece_FreeKick, {-35.0f, 5.0f}, 1, {-2.5f, 6.75f});
}

TEST(PesRefereeSpot, FreeKickZonesInTheAttackingHalf) {
  ExpectSpot(e_SetPiece_FreeKick, {35.0f, 25.0f}, 1, {25.5f, -12.25f});   // 1
  ExpectSpot(e_SetPiece_FreeKick, {20.0f, -25.0f}, 1, {10.0f, -5.0f});    // 3
  ExpectSpot(e_SetPiece_FreeKick, {20.0f, 3.0f}, 1, {26.25f, -13.5f});    // 4
  ExpectSpot(e_SetPiece_FreeKick, {20.0f, -10.0f}, 1, {31.75f, -21.75f});  // 5
  ExpectSpot(e_SetPiece_FreeKick, {20.0f, 10.0f}, 1, {26.25f, -13.5f});   // 6
  ExpectSpot(e_SetPiece_FreeKick, {10.0f, 25.0f}, 1, {12.0f, 15.5f});     // 7
  ExpectSpot(e_SetPiece_FreeKick, {10.0f, -5.0f}, 1, {20.5f, -13.5f});    // 8
  ExpectSpot(e_SetPiece_FreeKick, {10.0f, 5.0f}, 1, {15.5f, -9.5f});      // 9
  ExpectSpot(e_SetPiece_FreeKick, {-20.0f, -10.0f}, -1, {-26.25f, 13.5f});  // 6, mirrored
}

TEST(PesRefereeSpot, FreeKickInsideTheBoxWidthLeavesTheRefereeWhereHeIs) {
  EXPECT_FALSE(pes::ChiefRefereeSpot(e_SetPiece_FreeKick, {35.0f, 5.0f}, 1, SYNTHETIC_FIELD,
                                     SyntheticSpot()));
}

TEST(PesJudgeInstall, LoadsFromTheExe) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto k = pes::LoadJudgeConstants(*exe, &err);
  ASSERT_TRUE(k) << err;
  EXPECT_LT(k->foulThreshold, k->boxFoulThreshold);
  EXPECT_LT(k->boxFoulThreshold, k->cautionThreshold);
  EXPECT_LT(k->cautionThreshold, k->redThreshold);
  EXPECT_GT(k->recklessPoints, k->tacklePoints);
  EXPECT_GT(k->angleSpan_deg, 0.0f);
  EXPECT_GT(k->speedSpan_kmh, 0.0f);
  EXPECT_GT(k->jostleSpeedDivisor, 1);
  EXPECT_GT(k->spotBias_m, 0.0f);
  EXPECT_GT(k->whistleHold_s, 0.0f);
  EXPECT_GT(k->advantageWindow_s, k->whistleHold_s);
  // A from-behind slide at sprint speed that brings the man down is a card.
  pes::JudgeContact c;
  c.action = pes::JudgeAction::Slide;
  c.victimFell = true;
  c.approachAngle_deg = 180.0f;
  c.relativeSpeed_mps = 9.0f;
  EXPECT_GE(pes::JudgeLevel(c, *k), 3);
}

TEST(PesRefereeSpotInstall, ReproducesTheLiveMarks) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto k = pes::LoadJudgeConstants(*exe, &err);
  ASSERT_TRUE(k) << err;
  // PES's live FieldInfo (docs/re/pes-re-stadium-camera.md 2) and the live
  // restarts of docs/re/pes-re-setpiece-choreography.md 8.2 and
  // pes-re-protection.md 3.3, to the centimetre they were sampled at.
  constexpr pes::FieldMarks PES_FIELD{52.56f, 9.16f, 36.14f, 20.12f};
  constexpr float SAMPLE_TOLERANCE_M = 0.01f;
  const auto at = [&](e_SetPiece kind, pes::PitchSpot restart, int dir) {
    return *pes::ChiefRefereeSpot(kind, restart, dir, PES_FIELD, k->refereeSpot);
  };
  const auto kickOff = at(e_SetPiece_KickOff, {0, 0}, 1);
  EXPECT_NEAR(kickOff.x, -5.0f, SAMPLE_TOLERANCE_M);
  EXPECT_NEAR(kickOff.z, -10.0f, SAMPLE_TOLERANCE_M);
  const auto freeKick = at(e_SetPiece_FreeKick, {35.113f, -30.729f}, 1);
  EXPECT_NEAR(freeKick.x, 13.14f, SAMPLE_TOLERANCE_M);
  EXPECT_NEAR(freeKick.z, -6.71f, SAMPLE_TOLERANCE_M);
  const auto goalKick = at(e_SetPiece_GoalKick, {-47.06f, 5.0f}, 1);
  EXPECT_NEAR(goalKick.x, 4.0f, SAMPLE_TOLERANCE_M);
  EXPECT_NEAR(goalKick.z, 6.66f, SAMPLE_TOLERANCE_M);
  const auto corner = at(e_SetPiece_Corner, {-51.78f, 33.0f}, -1);
  EXPECT_NEAR(corner.x, -35.64f, SAMPLE_TOLERANCE_M);
  EXPECT_NEAR(corner.z, 11.16f, SAMPLE_TOLERANCE_M);
}

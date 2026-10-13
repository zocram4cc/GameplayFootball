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
  // A from-behind slide at sprint speed that brings the man down is a card.
  pes::JudgeContact c;
  c.action = pes::JudgeAction::Slide;
  c.victimFell = true;
  c.approachAngle_deg = 180.0f;
  c.relativeSpeed_mps = 9.0f;
  EXPECT_GE(pes::JudgeLevel(c, *k), 3);
}

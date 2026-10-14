// PES 2021's contact reaction (docs/re/pes-re-contact.md §1-2). Synthetic
// constants exercise the level table on both axes, the body forced-fall speed,
// the forced falls skipping the vetoes, the vetoes, the receiving clash, the
// carrier cap and the balance gauge. One install test checks the loader
// through relations, not pinned numbers.
#include <gtest/gtest.h>

#include <cstdlib>

#include "onthepitch/pescontact.hpp"

namespace {

using pes::ContactInput;
using pes::ContactOpponentMode;
using pes::ContactPart;

pes::ContactConstants Synthetic() {
  pes::ContactConstants k;
  for (int t = 0; t < 6; ++t) {
    k.dS_m[t] = 0.2f;
    k.dF_m[t] = 0.4f;
  }
  k.dS_m[1] = 0.1f;  // body plain
  k.dF_m[1] = 0.3f;
  k.vS_kmh = 20.0f;
  k.vF_kmh = 30.0f;
  k.forcedFallSpeedBody_kmh = 50.0f;
  k.forcedFallDisplacement_m = 0.6f;
  k.forcedFallDisplacementTackling_m = 0.9f;
  const int table[9] = {0, 2, 3, 1, 2, 3, 2, 3, 3};
  for (int i = 0; i < 9; ++i) k.levelTable[i] = table[i];
  const int action[4] = {0, 0x58, 0x58, 0x59};
  for (int i = 0; i < 4; ++i) k.levelAction[i] = action[i];
  k.fallAction = 0x59;
  k.keeperDS_m = 0.01f;
  k.keeperDF_m = 0.02f;
  k.keeperVS_kmh = 1.0f;
  k.keeperVF_kmh = 2.0f;
  k.committedScale = 2.0f;
  k.abilityFloor = 40.0f;
  k.abilitySpan = 60.0f;
  k.clashAbilityGain_kmh = 6.0f;
  k.clashTrapFall_kmh = 40.0f;
  k.clashTrapStagger_kmh = 30.0f;
  k.clashPartner_m = 0.7f;
  k.clashBallClear_m = 0.3f;
  k.approachAngle_deg = 100.0f;
  k.behindAngle_deg = 150.0f;
  k.runnerSpeed_kmh = 15.0f;
  k.vetoRelativeSpeed_kmh = 8.0f;
  k.vetoStillExtra_kmh = 10.0f;
  k.vetoStill_kmh = 4.0f;
  k.vetoHoldExtra_kmh = -4.0f;
  k.vetoFacing_deg = 50.0f;
  k.gaugeFull = 1000;
  k.gaugeDrainNumerator = -1000;
  k.gaugeRefillSeconds = 4;
  k.gaugeSideMin_deg = 40.0f;
  k.gaugeSideMax_deg = 140.0f;
  k.gaugeSideSpan_deg = 100.0f;
  k.gaugeBase_s = 2.0f;
  k.gaugeAbilityDivisor = 100.0f;
  k.gaugeHalf = 0.5f;
  k.gaugeOne = 1.0f;
  k.gaugeContactMin = 40;
  k.gaugeContactMax = 90;
  k.kmhPerMps = 3.6f;
  return k;
}

// A body hit from a non-tackling opponent who is not running alongside: no
// veto fires (not both running, fast enough, facing away from each other).
ContactInput Open() {
  ContactInput in;
  in.relativeSpeed_kmh = 30.0f;
  in.headingDelta_deg = 170.0f;
  in.movementDelta_deg = 170.0f;
  in.selfSpeed_kmh = 10.0f;
  in.oppSpeed_kmh = 10.0f;
  in.selfContactAbility = 70;
  in.partnerDistance_m = 0.5f;
  in.selfBallDistance_m = 5.0f;
  return in;
}

TEST(PesContactLevel, DisplacementAxisWalksTheTable) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.displacement_m = 0.05f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.displacement_m = 0.2f;  // dS body 0.1 .. dF body 0.3
  EXPECT_EQ(pes::ContactLevel(k, in), 1);
  in.displacement_m = 0.35f;
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  // A leg hit reads the leg pair (0.2/0.4), not the body pair.
  in.selfPart = ContactPart::kLeftLeg;
  EXPECT_EQ(pes::ContactLevel(k, in), 1);
}

TEST(PesContactLevel, ClosingSpeedAxisWalksTheTable) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.closingSpeed_kmh = 25.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.closingSpeed_kmh = 35.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
}

TEST(PesContactLevel, BodyHitAboveTheForcedSpeedTakesTheTopDisplacementRow) {
  auto k = Synthetic();
  // Make the top speed column unreachable so only the row change shows.
  k.vF_kmh = 1000.0f;
  ContactInput in = Open();
  in.closingSpeed_kmh = 60.0f;  // > 50 forced, vS..vF column
  EXPECT_EQ(pes::ContactLevel(k, in), 3);  // M[2][1]
  in.selfPart = ContactPart::kRightLeg;  // legs ignore the forced speed
  EXPECT_EQ(pes::ContactLevel(k, in), 2);  // M[0][1]
}

TEST(PesContactLevel, KeeperAndCommittedModifiers) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.displacement_m = 0.05f;
  in.oppKeeperHandling = true;  // keeper thresholds 0.01 / 0.02
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.oppKeeperHandling = false;
  in.displacement_m = 0.25f;
  in.selfCommitted = true;  // body thresholds x2: 0.2 / 0.6
  EXPECT_EQ(pes::ContactLevel(k, in), 1);
  in.displacement_m = 0.15f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactForcedFall, SlideIntoTheLegsFallsEvenThroughVetoes) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.mode = ContactOpponentMode::kSlide;
  in.selfPart = ContactPart::kLeftLeg;
  in.oppPart = ContactPart::kRightLeg;
  in.slideApproach_deg = 30.0f;
  in.oppTackling = true;
  in.oppBeforeContactFrame = true;  // veto 3 would zero any table level
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  in.slideApproach_deg = 120.0f;  // outside the approach window
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  // A slider's body against the shin is not a forced fall.
  in.slideApproach_deg = 30.0f;
  in.oppPart = ContactPart::kBody;
  in.oppBeforeContactFrame = false;
  in.relativeSpeed_kmh = 30.0f;
  EXPECT_LT(pes::ContactLevel(k, in), 3);
}

TEST(PesContactForcedFall, LargeDisplacementFallsAndTacklingRaisesTheBar) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.selfLocomotion = in.oppLocomotion = true;  // veto 7 would zero the table
  in.displacement_m = 0.7f;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  in.selfTackling = true;  // bar 0.9
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactForcedFall, FromBehindOnlyWhenTheRecordSwitchesItOn) {
  auto k = Synthetic();
  ContactInput in = Open();
  in.selfPart = ContactPart::kLeftLeg;
  in.oppTackling = true;
  in.oppBeforeContactFrame = true;
  in.oppBehind_deg = 170.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  k.fromBehindFallEnabled = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
}

TEST(PesContactVeto, TwoRunnersNeverKnockEachOtherOver) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.closingSpeed_kmh = 35.0f;
  in.selfLocomotion = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  in.oppLocomotion = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactVeto, TackleBeforeItsContactFrame) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.closingSpeed_kmh = 35.0f;
  in.oppTackling = true;
  in.oppBeforeContactFrame = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactVeto, OpponentRunningAway) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.closingSpeed_kmh = 35.0f;
  in.oppAway_deg = 120.0f;
  in.oppSpeed_kmh = 20.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.oppSpeed_kmh = 10.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
}

TEST(PesContactVeto, SlowTouch) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.displacement_m = 0.35f;
  in.relativeSpeed_kmh = 7.0f;  // < 8
  // Facing apart: the slow touch is allowed.
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.headingDelta_deg = 20.0f;  // facing alike and slower than the 4 km/h floor? no: 7
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.relativeSpeed_kmh = 3.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  // Nearly still: the window widens to 18 km/h and needs no facing test.
  in.headingDelta_deg = in.movementDelta_deg = 170.0f;
  in.relativeSpeed_kmh = 15.0f;
  in.selfSpeed_kmh = 2.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactLevel, ReceivingClashUsesTheTrapThresholds) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.selfTrapping = true;
  in.selfBallDistance_m = 0.5f;
  in.selfContactAbility = 70;  // p = 6 * 0.5 = 3
  in.closingSpeed_kmh = 35.0f;  // table says 3, the clash says stagger (> 33)
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.closingSpeed_kmh = 44.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  in.closingSpeed_kmh = 32.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactLevel, RunningCarrierIsOnlyStaggered) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.closingSpeed_kmh = 35.0f;
  in.selfOwnsBall = true;
  in.selfSpeed_kmh = 20.0f;
  in.headingDelta_deg = 30.0f;
  in.movementDelta_deg = 170.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
}

TEST(PesContactLevel, LevelToReaction) {
  const auto k = Synthetic();
  EXPECT_EQ(pes::ReactionForLevel(k, 0), pes::ContactReaction::kNone);
  EXPECT_EQ(pes::ReactionForLevel(k, 1), pes::ContactReaction::kStagger);
  EXPECT_EQ(pes::ReactionForLevel(k, 2), pes::ContactReaction::kStagger);
  EXPECT_EQ(pes::ReactionForLevel(k, 3), pes::ContactReaction::kFall);
}

pes::BalanceGaugeInput SideBySide() {
  pes::BalanceGaugeInput in;
  in.touching = true;
  in.bothLocomotion = true;
  in.selfSpeed_kmh = in.partnerSpeed_kmh = 20.0f;
  in.selfBearing_deg = in.partnerBearing_deg = 90.0f;
  in.selfContactAbility = in.partnerContactAbility = 65;
  return in;
}

TEST(PesContactGauge, RefillsAndCaps) {
  const auto k = Synthetic();
  pes::BalanceGaugeInput in;
  // 1000 / (4 * 10) = 25 per frame.
  EXPECT_EQ(pes::UpdateBalanceGauge(k, in, 500, 10), 525);
  EXPECT_EQ(pes::UpdateBalanceGauge(k, in, 990, 10), 1000);
}

TEST(PesContactGauge, DrainsShoulderToShoulder) {
  const auto k = Synthetic();
  auto in = SideBySide();
  // a = 0.5, r = 0.5: T = 2 + 0 + 0.5*0*0.75 = 2 s -> -1000/(int)(20.5) = -50.
  EXPECT_EQ(pes::UpdateBalanceGauge(k, in, 500, 10), 450);
  // 30 points more contact: T = 2.3 -> -1000/23 = -43.
  in.selfContactAbility = 95;
  in.partnerContactAbility = 65;
  // At a = 0.5 the bearing term vanishes, so r does not enter here.
  EXPECT_EQ(pes::UpdateBalanceGauge(k, in, 500, 10), 457);
  // Ahead of the partner (a = 0.9) lasts longer: T = 2 + 0.5*0.8*(1 - 0.25) = 2.3.
  in = SideBySide();
  in.selfBearing_deg = 130.0f;
  EXPECT_EQ(pes::UpdateBalanceGauge(k, in, 500, 10), 457);
}

TEST(PesContactGauge, NoDrainOutsideTheSideBySideWindow) {
  const auto k = Synthetic();
  auto in = SideBySide();
  in.partnerBearing_deg = 30.0f;
  EXPECT_EQ(pes::UpdateBalanceGauge(k, in, 500, 10), 525);
  in = SideBySide();
  in.partnerSpeed_kmh = 10.0f;
  EXPECT_EQ(pes::UpdateBalanceGauge(k, in, 500, 10), 525);
  in = SideBySide();
  in.bothLocomotion = false;
  EXPECT_EQ(pes::UpdateBalanceGauge(k, in, 500, 10), 525);
}

TEST(PesContactInstall, TheInstallCarriesTheGamesOwnThresholds) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto k = pes::LoadContactConstants(*exe, vfs, &err);
  ASSERT_TRUE(k.has_value()) << err;
  for (int t = 0; t < 6; ++t) EXPECT_LT(k->dS_m[t], k->dF_m[t]) << t;
  EXPECT_LT(k->vS_kmh, k->vF_kmh);
  EXPECT_GT(k->forcedFallSpeedBody_kmh, k->vF_kmh);
  EXPECT_LT(k->forcedFallDisplacement_m, k->forcedFallDisplacementTackling_m);
  // The table grows along both axes and its corner is a fall.
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 2; ++c) EXPECT_LE(k->levelTable[r * 3 + c], k->levelTable[r * 3 + c + 1]);
  EXPECT_EQ(k->levelTable[0], 0);
  EXPECT_EQ(pes::ReactionForLevel(*k, 3), pes::ContactReaction::kFall);
  EXPECT_EQ(pes::ReactionForLevel(*k, 1), pes::ContactReaction::kStagger);
  EXPECT_EQ(k->gaugeDrainNumerator, -k->gaugeFull);
  EXPECT_GT(k->gaugeRefillSeconds, 0);
  EXPECT_LT(k->gaugeSideMin_deg, k->gaugeSideMax_deg);
  EXPECT_FLOAT_EQ(k->gaugeSideMax_deg - k->gaugeSideMin_deg, k->gaugeSideSpan_deg);
  EXPECT_LT(k->gaugeContactMin, k->gaugeContactMax);
  EXPECT_LT(k->vetoStill_kmh, k->vetoRelativeSpeed_kmh);
  EXPECT_LT(k->vetoHoldExtra_kmh, 0.0f);
  EXPECT_LT(k->approachAngle_deg, k->behindAngle_deg);
  EXPECT_LT(k->clashTrapStagger_kmh, k->clashTrapFall_kmh);
}

}  // namespace

// PES 2021's contact reaction (docs/re/pes-re-contact.md §1-4). Synthetic
// constants exercise the level table on both axes, every threshold modifier,
// the forced falls skipping the vetoes, each of the nine vetoes, the direct
// triggers, the jostle hold-up and shoulder-through, the displacement and
// closing-speed measures, the balance gauge and the playback rates. One
// install test checks the loader through relations, not pinned numbers.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>

#include "onthepitch/pescontact.hpp"

namespace {

using pes::ContactAction;
using pes::ContactInput;

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
  k.forcedFallDisplacementUpper_m = 0.9f;
  k.forcedFallDisplacementTackling_m = 0.9f;
  k.forcedFallPart3_m = 0.7f;
  const int table[9] = {0, 2, 3, 1, 2, 3, 2, 3, 3};
  for (int i = 0; i < 9; ++i) k.levelTable[i] = table[i];
  const int action[4] = {0, 0x58, 0x58, 0x59};
  for (int i = 0; i < 4; ++i) k.levelAction[i] = action[i];
  k.fallAction = 0x59;
  k.keeperDS_m = 0.01f;
  k.keeperDF_m = 0.02f;
  k.keeperVS_kmh = 1.0f;
  k.keeperVF_kmh = 2.0f;
  k.frontalHeading_deg = 80.0f;
  k.frontalDF_m = 0.05f;
  k.committedScale = 2.0f;
  k.pushAwayFrom_frames = 6.0f;
  k.pushAwayTo_frames = 12.0f;
  k.pushAwayScale = 3.0f;
  k.framesPerSecondRef = 60.0f;
  k.holdUpContactOffset = 60;
  k.holdUpFloor = 20.0f;
  k.holdUpCeil = 100.0f;
  k.holdUpSpan = 80.0f;
  k.holdUpFrames = 120.0f;
  k.holdUpDSScale = 0.005f;
  k.holdUpVS_kmh = -3.0f;
  k.holdUpHalf = 0.5f;
  k.shoulderThroughMargin = 5;
  k.abilityFloor = 40.0f;
  k.abilityTop = 100.0f;
  k.abilitySpan = 60.0f;
  k.clashAbilityGain_kmh = 6.0f;
  k.clashFall_kmh = 7.0f;
  k.clashStagger_kmh = -6.0f;
  k.clashTrapFall_kmh = 40.0f;
  k.clashTrapStagger_kmh = 30.0f;
  k.clashPartner_m = 0.7f;
  k.clashFeetHeight_m = 0.3f;
  k.approachAngle_deg = 100.0f;
  k.behindAngle_deg = 150.0f;
  k.sideAngle_deg = 45.0f;
  k.runnerSpeed_kmh = 15.0f;
  k.footTrail_m = -0.3f;
  k.vetoRelativeSpeed_kmh = 8.0f;
  k.vetoStillExtra_kmh = 10.0f;
  k.vetoStill_kmh = 4.0f;
  k.vetoHoldExtra_kmh = -4.0f;
  k.vetoFacing_deg = 50.0f;
  k.vetoPastContact_frames = 12.0f;
  k.slideLegDisplacement_m = 0.25f;
  k.directRange_m = 1.5f;
  k.directFootHead_m = 0.1f;
  k.directFootHeadGamesmanship_m = 0.2f;
  k.directNear_m = 1.2f;
  k.studsRunSpeed_kmh = 16.0f;
  k.studsContest_frames = 12.0f;
  k.studsBallLateral_m = 0.7f;
  k.studsBase_m = 0.3f;
  k.studsGain_m = 0.2f;
  k.tripRange_m = 1.0f;
  k.tripLateral_m = 0.5f;
  k.tackleContactWindow_frames = 4;
  k.tackleStudBase_m = 0.2f;
  k.tackleStudGain_m = 0.1f;
  k.pushTripFoot_m = 0.3f;
  k.pushTripLateral_m = 1.0f;
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
  k.staggerRateBase = 0.9f;
  k.staggerRateGain = 0.2f;
  k.riseRateLow = 0.8f;
  k.riseRateHigh = 1.2f;
  k.riseAbilitySpan = 60.0f;
  return k;
}

// A chest-on-chest hit from an opponent standing still while the self walks
// into him at a pace no veto stops (facing apart, not both running).
ContactInput Open() {
  ContactInput in;
  in.selfAction = ContactAction::kKick;  // not locomotion, past its contact
  in.oppAction = ContactAction::kOther;
  in.relativeSpeed_kmh = 30.0f;
  in.headingDelta_deg = 170.0f;
  in.movementDelta_deg = 170.0f;
  in.selfSpeed_kmh = 10.0f;
  in.oppSpeed_kmh = 10.0f;
  in.selfContactAbility = 70;
  in.oppContactAbility = 70;
  in.partnerDistance_m = 0.5f;
  in.selfToOppBearing_deg = 10.0f;
  return in;
}

TEST(PesContactParts, ClassesAndArms) {
  EXPECT_EQ(pes::PartClass(pes::kBoneFootL), pes::ContactPart::kLeftLeg);
  EXPECT_EQ(pes::PartClass(pes::kBoneThighR), pes::ContactPart::kRightLeg);
  EXPECT_EQ(pes::PartClass(pes::kBoneHip), pes::ContactPart::kBody);
  EXPECT_TRUE(pes::IsArmBone(pes::kBoneUpperArmL));
  EXPECT_TRUE(pes::IsArmBone(pes::kBoneForearmR));
  EXPECT_FALSE(pes::IsArmBone(pes::kBoneHandL));
  EXPECT_FALSE(pes::IsArmBone(pes::kBoneShoulderR));
}

TEST(PesContactLevel, DisplacementAxisWalksTheTable) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.displacement_m = in.displacementLegs_m = 0.05f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.displacement_m = in.displacementLegs_m = 0.2f;  // body pair 0.1 .. 0.3
  EXPECT_EQ(pes::ContactLevel(k, in), 1);
  in.displacement_m = in.displacementLegs_m = 0.35f;
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.selfBone = pes::kBoneLegL;  // the leg pair 0.2 / 0.4
  EXPECT_EQ(pes::ContactLevel(k, in), 1);
}

TEST(PesContactLevel, ClosingSpeedAxisAndTheBodyForcedRow) {
  auto k = Synthetic();
  ContactInput in = Open();
  in.closingSpeed_kmh = 25.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.closingSpeed_kmh = 35.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  k.vF_kmh = 1000.0f;
  in.closingSpeed_kmh = 60.0f;  // > 50: top displacement row for a body hit
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  in.selfBone = pes::kBoneFootR;
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
}

TEST(PesContactModifiers, KeeperHandlingOpponent) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.displacement_m = in.displacementLegs_m = 0.05f;
  in.oppAction = ContactAction::kKeeperHandle;  // 0.01 / 0.02
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
}

TEST(PesContactModifiers, FrontalStandingTackleAlwaysStaggers) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.oppAction = ContactAction::kTackle;
  in.oppClipProgress = 0.8f;  // past half its clip -> mode 1
  in.oppBone = pes::kBoneFootR;
  in.oppSlideLegBone = pes::kBoneFootR;
  in.displacement_m = in.displacementLegs_m = 0.03f;  // under the tackle pair 0.2
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.oppTackleFrontal = true;
  in.headingDelta_deg = 60.0f;  // < 80: dS 0, dF 0.05
  in.movementDelta_deg = 170.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 1);
  in.headingDelta_deg = 90.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactModifiers, CommittedScalesUnlessSliding) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.selfAction = ContactAction::kTackle;
  in.displacement_m = in.displacementLegs_m = 0.25f;  // body 0.1/0.3 -> level 1
  EXPECT_EQ(pes::ContactLevel(k, in), 1);
  in.oppAction = ContactAction::kStagger;  // tackling a staggering man: x2 -> 0.2/0.6
  EXPECT_EQ(pes::ContactLevel(k, in), 1);
  in.displacement_m = in.displacementLegs_m = 0.15f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactModifiers, PushAwayWindowHardensTheThresholds) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.displacement_m = in.displacementLegs_m = 0.25f;
  in.selfPushAway_s = 0.15f;  // 9 frames, inside 6..12: x3 -> 0.3/0.9
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.selfPushAway_s = 0.3f;  // 18 frames: outside
  EXPECT_EQ(pes::ContactLevel(k, in), 1);
}

TEST(PesContactJostle, HoldUpByPhysicalContact) {
  const auto k = Synthetic();
  // T = 120 + 120 * clamp((Pc + 60 - Pj - 20) / 80) frames at 60.
  EXPECT_FLOAT_EQ(pes::JostleHoldUp_s(k, 70, 70), 3.0f);
  EXPECT_FLOAT_EQ(pes::JostleHoldUp_s(k, 30, 70), 2.0f);
  EXPECT_FLOAT_EQ(pes::JostleHoldUp_s(k, 99, 50), 4.0f);
}

TEST(PesContactJostle, HoldUpTimeoutStaggersTheCarrier) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.oppAction = ContactAction::kJostle;
  in.selfOwnsBall = true;
  in.relativeSpeed_kmh = 9.0f;  // keep veto 8 out
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.oppJostleContact_s = 3.1f;  // past 3 s at equal PC: vS -3 -> speed level 1
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
}

TEST(PesContactJostle, ShoulderThroughNeedsTheMargin) {
  const auto k = Synthetic();
  EXPECT_FALSE(pes::ShoulderThrough(k, 75, 70));
  EXPECT_TRUE(pes::ShoulderThrough(k, 76, 70));
}

TEST(PesContactLevel, ReceivingClash) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.selfAction = ContactAction::kOther;
  in.selfInHitWindow = true;
  in.selfFeetHeight_m = 0.5f;  // both feet off the ground
  in.selfContactAbility = 70;  // p = 6 * 0.5 = 3
  in.closingSpeed_kmh = 12.0f;  // > p + 7
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  in.closingSpeed_kmh = 5.0f;  // > p - 6
  in.relativeSpeed_kmh = 30.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.selfAction = ContactAction::kTrap;  // trap: +40 / +30
  in.closingSpeed_kmh = 35.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
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

TEST(PesContactForcedFall, SlideLegOnTheShinFallsThroughVetoes) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.oppAction = ContactAction::kSlide;
  in.selfBone = pes::kBoneLegL;
  in.oppBone = pes::kBoneFootR;
  in.oppSlideLegBone = pes::kBoneFootR;
  in.slideApproach_deg = 30.0f;
  in.oppBeforeContactFrame = true;  // veto 3 would zero a table level
  EXPECT_EQ(pes::ContactLevel(k, in), 0);  // outside the slide's impact window
  in.oppSlidingFlag = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  in.slideApproach_deg = 120.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  // The slider's other leg is not its slide leg.
  in.slideApproach_deg = 30.0f;
  in.oppBone = pes::kBoneFootL;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  // His body above the knee is.
  in.oppBone = pes::kBoneHip;
  in.selfBone = pes::kBoneChest;
  in.contactHeight_m = 0.6f;
  in.selfKneeHeight_m = 0.5f;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  in.contactHeight_m = 0.4f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactForcedFall, DisplacementBarsPerPart) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.selfAction = in.oppAction = ContactAction::kLoco;  // veto 7 zeroes table levels
  in.displacement_m = 0.65f;
  in.displacementLegs_m = 0.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);  // part 0 > 0.6
  in.selfAction = ContactAction::kTackle;  // bar 0.9
  in.oppAction = ContactAction::kLoco;
  EXPECT_LT(pes::ContactLevel(k, in), 3);
  in.displacementLegs_m = 0.75f;  // part 3 > 0.7
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
}

TEST(PesContactForcedFall, FromBehindOnlyWhenTheRecordSwitchesItOn) {
  auto k = Synthetic();
  ContactInput in = Open();
  in.selfBone = pes::kBoneLegL;
  in.oppAction = ContactAction::kTackle;
  in.oppBeforeContactFrame = true;
  in.oppBehind_deg = 170.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  k.fromBehindFallEnabled = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
}

ContactInput Falling() {
  ContactInput in = Open();
  in.closingSpeed_kmh = 35.0f;  // a table fall unless vetoed
  return in;
}

TEST(PesContactVeto, V0ArmsAndLegBrushes) {
  const auto k = Synthetic();
  ContactInput in = Falling();
  ASSERT_EQ(pes::ContactLevel(k, in), 3);
  in.selfBone = pes::kBoneUpperArmR;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in = Falling();
  in.oppBone = pes::kBoneForearmL;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.oppAction = ContactAction::kKeeperHandle;
  EXPECT_GT(pes::ContactLevel(k, in), 0);
  // A body hit on a keeper running about is spared; an outfielder is not.
  in = Falling();
  in.selfAction = ContactAction::kLoco;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  in.selfIsKeeper = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  // Leg on leg while the opponent does nothing with his legs.
  in = Falling();
  in.selfBone = pes::kBoneLegL;
  in.oppBone = pes::kBoneLegR;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.oppAction = ContactAction::kTrap;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  // Running past with the touched foot trailing.
  in.selfSpeed_kmh = 20.0f;
  in.touchedFootForward_m = -0.4f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactVeto, V1KickBeforeContactAndKeeperDistribution) {
  const auto k = Synthetic();
  ContactInput in = Falling();
  in.oppAction = ContactAction::kKeeperDistribute;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in = Falling();
  in.selfBeforeContactFrame = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.selfAction = ContactAction::kDodge;
  in.selfBeforeContactFrame = false;
  in.selfToOppBearing_deg = 90.0f;
  in.closingSpeed_kmh = 70.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactVeto, V2AlreadyDownOrGettingUp) {
  const auto k = Synthetic();
  ContactInput in = Falling();
  in.selfAction = ContactAction::kFall;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.selfAction = ContactAction::kLoco;
  in.selfRecovering = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactVeto, V3TackleBeforeItsContactFrame) {
  const auto k = Synthetic();
  ContactInput in = Falling();
  in.oppAction = ContactAction::kTackle;
  in.oppBeforeContactFrame = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactVeto, V4TrapBeforeItsTouch) {
  const auto k = Synthetic();
  ContactInput in = Falling();
  in.selfAction = ContactAction::kTrap;
  in.selfBeforeContactFrame = true;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactVeto, V5DodgeAwayFromATackle) {
  const auto k = Synthetic();
  ContactInput in = Falling();
  in.selfAction = ContactAction::kDodge;
  in.closingSpeed_kmh = 70.0f;  // past the dodge's committed x2 thresholds
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.oppAction = ContactAction::kTackle;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
}

TEST(PesContactVeto, V6OpponentRunningAway) {
  const auto k = Synthetic();
  ContactInput in = Falling();
  in.oppAway_deg = 120.0f;
  in.oppSpeed_kmh = 20.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  in.oppSpeed_kmh = 10.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
}

TEST(PesContactVeto, V7RunnersAndAFinishedAction) {
  const auto k = Synthetic();
  ContactInput in = Falling();
  in.selfAction = ContactAction::kLoco;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
  in.oppAction = ContactAction::kLoco;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  // A dribbler is not locomotion.
  in.selfAction = ContactAction::kDribble;
  EXPECT_EQ(pes::ContactLevel(k, in), 3);
}

TEST(PesContactVeto, V8SlowTouch) {
  const auto k = Synthetic();
  ContactInput in = Open();
  in.displacement_m = in.displacementLegs_m = 0.35f;
  in.relativeSpeed_kmh = 7.0f;  // < 8, facing apart: allowed
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.headingDelta_deg = 20.0f;  // alike, but over the 4 km/h floor
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.relativeSpeed_kmh = 3.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  // Nearly still: the window widens to 18 and needs no facing test.
  in.headingDelta_deg = in.movementDelta_deg = 170.0f;
  in.relativeSpeed_kmh = 15.0f;
  in.selfSpeed_kmh = 2.0f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
  // Alike, opponent running, self past its contact by more than 12/60 s.
  in = Open();
  in.displacement_m = in.displacementLegs_m = 0.35f;
  in.relativeSpeed_kmh = 7.0f;
  in.headingDelta_deg = 20.0f;
  in.oppAction = ContactAction::kLoco;
  in.selfPastContact_s = 0.1f;
  EXPECT_EQ(pes::ContactLevel(k, in), 2);
  in.selfPastContact_s = 0.3f;
  EXPECT_EQ(pes::ContactLevel(k, in), 0);
}

TEST(PesContactMeasure, DisplacementAndClosingSpeed) {
  const auto k = Synthetic();
  auto d = pes::TrackedDisplacement(k, 0.3f, 0.4f, 0.0f, 0.1f, false);
  EXPECT_NEAR(d.part0_m, std::sqrt(0.09f + 0.09f), 1e-5f);
  EXPECT_NEAR(d.legs_m, 0.5f, 1e-5f);
  d = pes::TrackedDisplacement(k, 0.3f, 0.4f, 0.0f, 0.1f, true);
  EXPECT_FLOAT_EQ(d.part0_m, 0.25f);
  EXPECT_FLOAT_EQ(d.legs_m, 0.25f);
  // 2 m/s toward him, he 1 m/s toward me: 3 m/s = 10.8 km/h.
  EXPECT_NEAR(pes::ClosingSpeed(k, 2.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f), 10.8f, 1e-4f);
  // Sideways motion does not close.
  EXPECT_NEAR(pes::ClosingSpeed(k, 0.0f, 5.0f, 0.0f, 0.0f, 1.0f, 0.0f), 0.0f, 1e-4f);
}

pes::DirectTriggerInput Near() {
  pes::DirectTriggerInput in;
  in.distance_m = 0.8f;
  in.oppFootToSelfHead_m = in.oppFootToSelfChest_m = in.oppFootToSelfBelly_m = 2.0f;
  in.selfContactAbility = 70;
  in.headingDelta_deg = 170.0f;
  in.oppAlongSelf_m = in.selfAlongOpp_m = -1.0f;
  return in;
}

TEST(PesContactDirect, SlideFootAtTheHeadFalls) {
  const auto k = Synthetic();
  auto in = Near();
  in.oppAction = ContactAction::kTackle;
  in.oppFootToSelfHead_m = 0.05f;
  auto t = pes::DirectTriggers(k, in);
  EXPECT_EQ(t.reaction, pes::ContactReaction::kFall);
  EXPECT_EQ(t.cause, 4);
  in.oppFootToSelfHead_m = 0.15f;
  EXPECT_EQ(pes::DirectTriggers(k, in).reaction, pes::ContactReaction::kNone);
  in.selfGamesmanship = true;
  EXPECT_EQ(pes::DirectTriggers(k, in).cause, 4);
  in.selfAction = ContactAction::kStagger;
  EXPECT_EQ(pes::DirectTriggers(k, in).reaction, pes::ContactReaction::kNone);
}

TEST(PesContactDirect, StudsOnARunningCarrier) {
  const auto k = Synthetic();
  auto in = Near();
  in.selfOwnsBall = true;
  in.oppAction = ContactAction::kTackle;
  in.selfSpeed_kmh = 20.0f;
  in.selfContested_s = 0.25f;  // 15 frames > 12
  in.oppPastContactFrame = true;
  in.headingDelta_deg = 20.0f;
  in.ballLateral_m = 0.2f;
  in.selfContactAbility = 70;     // 0.3 + 0.2 * 0.5 = 0.4
  in.oppFootToSelfChest_m = 0.35f;
  auto t = pes::DirectTriggers(k, in);
  EXPECT_EQ(t.reaction, pes::ContactReaction::kStagger);
  EXPECT_EQ(t.cause, 5);
  in.selfContactAbility = 100;  // 0.3
  EXPECT_EQ(pes::DirectTriggers(k, in).reaction, pes::ContactReaction::kNone);
}

TEST(PesContactDirect, FeintRunCutTripsTheManBehind) {
  const auto k = Synthetic();
  auto in = Near();
  in.selfFeintBeforeContact = true;
  in.selfMoveVsHeading_deg = 20.0f;
  in.headingDelta_deg = 170.0f;
  in.oppAlongSelf_m = 0.4f;
  in.oppAcrossSelf_m = 0.2f;
  auto t = pes::DirectTriggers(k, in);
  EXPECT_EQ(t.reaction, pes::ContactReaction::kFall);
  EXPECT_EQ(t.cause, 3);
  in.oppAcrossSelf_m = 0.6f;
  EXPECT_EQ(pes::DirectTriggers(k, in).reaction, pes::ContactReaction::kNone);
}

TEST(PesContactDirect, StandingTackleAtItsContactFrame) {
  const auto k = Synthetic();
  auto in = Near();
  in.oppAction = ContactAction::kTackle;
  in.oppStandingTackleKind1 = true;
  in.oppFromContact_frames = 2.0f;
  in.oppFootToSelfBelly_m = 0.22f;  // 0.2 + 0.1 * 0.5 = 0.25
  auto t = pes::DirectTriggers(k, in);
  EXPECT_EQ(t.cause, 7);
  EXPECT_EQ(t.reaction, pes::ContactReaction::kFall);
  in.oppFromContact_frames = 5.0f;
  EXPECT_EQ(pes::DirectTriggers(k, in).reaction, pes::ContactReaction::kNone);
}

TEST(PesContactDirect, PushAwayStudTrip) {
  const auto k = Synthetic();
  auto in = Near();
  in.oppPushAway_s = 0.15f;  // 9 frames
  in.headingDelta_deg = 20.0f;
  in.oppFootToSelfBelly_m = 0.2f;
  in.selfAlongOpp_m = 0.1f;
  in.selfAcrossOpp_m = 0.5f;
  auto t = pes::DirectTriggers(k, in);
  EXPECT_EQ(t.reaction, pes::ContactReaction::kStagger);
  EXPECT_EQ(t.cause, 1);
}

TEST(PesContactJostle, PushAwayStartsBesideTheDribbler) {
  auto k = Synthetic();
  k.pushFoot_m = 0.5f;
  k.pushBallAhead_m = 0.3f;
  k.pushBallAcross_m = 0.5f;
  k.pushAngle_deg = 90.0f;
  k.pushOppSpeed_kmh = 8.0f;
  k.pushBallDir_deg = 67.5f;
  k.pushOppAlong_m = -0.1f;
  pes::PushAwayInput in;
  in.dribbling = true;
  in.oppFootToSelfBelly_m = 0.4f;
  in.ballAhead_m = 0.5f;
  in.headingVsOppHeading_deg = 20.0f;
  in.headingVsOppMove_deg = 20.0f;
  in.oppSpeed_kmh = 15.0f;
  in.oppAlong_m = -0.2f;
  EXPECT_TRUE(pes::PushAwayStarts(k, in));
  in.oppAlong_m = 0.1f;  // ahead of him: no
  EXPECT_FALSE(pes::PushAwayStarts(k, in));
  in.oppAlong_m = -0.2f;
  in.ballAhead_m = 0.1f;
  EXPECT_FALSE(pes::PushAwayStarts(k, in));
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
  // a = 0.5: T = 2 s -> -1000 / (int)(20.5) = -50.
  EXPECT_EQ(pes::UpdateBalanceGauge(k, in, 500, 10), 450);
  // 30 points more contact: T = 2.3 -> -1000/23 = -43.
  in.selfContactAbility = 95;
  EXPECT_EQ(pes::UpdateBalanceGauge(k, in, 500, 10), 457);
  // Ahead of the partner (a = 0.9): T = 2 + 0.5*0.8*(1 - 0.25) = 2.3.
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

TEST(PesContactPlayback, RatesByBalance) {
  const auto k = Synthetic();
  // b = clamp((B - 40) / 60): stagger 0.9 + 0.2 b^2, rise 0.8 (1 - b) + 1.2 b.
  EXPECT_FLOAT_EQ(pes::StaggerRate(k, 40), 0.9f);
  EXPECT_FLOAT_EQ(pes::StaggerRate(k, 70), 0.95f);
  EXPECT_FLOAT_EQ(pes::StaggerRate(k, 120), 1.1f);
  EXPECT_FLOAT_EQ(pes::RiseUpRate(k, 70), 1.0f);
  EXPECT_FLOAT_EQ(pes::RiseUpRate(k, 20), 0.8f);
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
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 2; ++c) EXPECT_LE(k->levelTable[r * 3 + c], k->levelTable[r * 3 + c + 1]);
  EXPECT_EQ(k->levelTable[0], 0);
  EXPECT_EQ(pes::ReactionForLevel(*k, 3), pes::ContactReaction::kFall);
  EXPECT_EQ(pes::ReactionForLevel(*k, 1), pes::ContactReaction::kStagger);
  EXPECT_EQ(k->gaugeDrainNumerator, -k->gaugeFull);
  EXPECT_GT(k->gaugeRefillSeconds, 0);
  EXPECT_FLOAT_EQ(k->gaugeSideMax_deg - k->gaugeSideMin_deg, k->gaugeSideSpan_deg);
  EXPECT_LT(k->gaugeContactMin, k->gaugeContactMax);
  EXPECT_LT(k->vetoStill_kmh, k->vetoRelativeSpeed_kmh);
  EXPECT_LT(k->vetoHoldExtra_kmh, 0.0f);
  EXPECT_LT(k->approachAngle_deg, k->behindAngle_deg);
  EXPECT_LT(k->clashTrapStagger_kmh, k->clashTrapFall_kmh);
  EXPECT_LT(k->clashStagger_kmh, k->clashFall_kmh);
  EXPECT_LT(k->footTrail_m, 0.0f);
  EXPECT_LT(k->frontalHeading_deg, k->approachAngle_deg);
  EXPECT_LT(k->pushAwayFrom_frames, k->pushAwayTo_frames);
  EXPECT_GT(k->pushAwayScale, k->committedScale);
  // The hold-up runs from the base to twice it.
  EXPECT_LT(k->holdUpFloor, k->holdUpCeil);
  EXPECT_FLOAT_EQ(pes::JostleHoldUp_s(*k, 99, 40) / pes::JostleHoldUp_s(*k, 40, 99), 2.0f);
  EXPECT_GT(k->shoulderThroughMargin, 0);
  EXPECT_LT(k->staggerRateBase, pes::StaggerRate(*k, 99));
  EXPECT_LT(k->riseRateLow, k->riseRateHigh);
  EXPECT_LT(k->directFootHead_m, k->studsBase_m);
  EXPECT_LT(k->directNear_m, k->directRange_m);
}

}  // namespace

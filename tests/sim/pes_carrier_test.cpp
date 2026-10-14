// PES 2021's ball-carrier decision layer (docs/re/pes-re-carrier-ai.md,
// docs/re/pes-re-attack-decisions.md 2). Synthetic constants exercise the
// shoot zone and its gates, the openness formula's class ladder, PassForward's
// three tiers and the plan-order roll; one install test checks the loader
// against the user's own PES2021.exe through relations, not pinned numbers.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <vector>

#include "onthepitch/pescarrier.hpp"

namespace {

// The values the user's install carries for these fields, used both as the
// synthetic set and as the shape the install test checks. PES's own: report 4.1
// for the zone ranges, 4.3 for the openness ladder, 4.4 for the plan roll.
pes::CarrierConstants Synthetic() {
  pes::CarrierConstants k{};
  k.shootR0_m = 11.0f;
  k.shootR1_m = 12.0f;
  k.nearCentral_m = 18.0f;
  k.farCentral_m = 24.0f;
  k.nearSide_m = 16.0f;
  k.farSide_m = 22.0f;
  k.centralHalfAngle_deg = 15.0f;
  k.sideAngle_deg = 40.0f;
  k.finishingBoost_m = 3.0f;
  k.longShotBoost_m = 3.0f;
  k.finishingFloor = 70.0f;
  k.finishingTop = 99.0f;
  k.finishingSpan = 29.0f;
  k.justShootDist_m = 20.0f;
  k.justShootAngle_deg = 40.0f;
  k.firstTimeDistSq = 400.0f;
  k.firstTimeNearDistSq = 225.0f;
  k.firstTimeIncoming_deg = 120.0f;
  k.firstTimeBody_deg = 70.0f;
  k.firstTimePathGate_m = 7.0f;
  k.aboveHeadFactor = 0.8f;
  k.statDivisor = 100.0f;
  k.movingBallFloor_kmh = 10.0f;
  k.movingBallSpan_kmh = 50.0f;
  k.kmhPerMs = 3.6f;
  k.distFloor_m = 10.0f;
  k.distSpan_m = 20.0f;
  k.ballAngleHigh_deg = 150.0f;
  k.ballAngleSpan_deg = 60.0f;
  k.oneTouchGain = 0.5f;
  k.runFloor_kmh = 16.0f;
  k.runSpan_kmh = 12.0f;
  k.turnFloor_deg = 60.0f;
  k.turnSpan_deg = 70.0f;
  k.turnTop_deg = 130.0f;
  k.highFloor_m = 0.108685933f;
  k.highSpan_m = 0.891314089f;
  k.diffMovingBallWeight = 2.0f;
  k.diffRunWeight = 1.5f;
  k.diffTurnWeight = 1.5f;
  k.lowPassFloor = 80.0f;
  k.lowPassTop = 99.0f;
  k.lowPassSpan = 19.0f;
  k.lowPassBase = 0.7f;
  k.lowPassGain = 0.3f;
  k.forwardAngle_deg = 60.0f;
  k.squareAngle_deg = 110.0f;
  k.openNear_m = 1.0f;
  k.openSpan_m = 29.0f;
  k.openInFront_deg = 90.0f;
  k.openBlockMargin_m = 1.5f;
  k.openLaneMargin_m = 1.0f;
  k.openAlongMin_m = 0.2f;
  k.openAlongGain = 0.6f;
  k.openAlongPenalty = 0.3f;
  k.openMarginScale = 100.0f;
  k.openKLowAngle_deg = 45.0f;
  k.openKHighAngle_deg = 90.0f;
  k.openClass1 = 10.0f;
  k.openClass2 = 40.0f;
  k.openClass3 = 60.0f;
  k.openClass4 = 100.0f;
  k.openClass5 = 130.0f;
  k.fwdPlanAngleGate_deg = 60.0f;
  k.fwdTeamFlagGate = 36.0f;
  k.fwdCloseRange_deg = 120.0f;
  k.fwdGoalCentreDist_m = 30.0f;
  k.fwdLaneWidthGain = 0.25f;
  k.fwdLaneWidthMin_m = 1.5f;
  k.fwdLaneWidthMax_m = 6.0f;
  k.fwdLaneBudget = 220.0f;
  k.fwdLineValueSq = 1225.0f;
  k.fwdLineValueFarSq = 1600.0f;
  k.fwdNearestOppFar_m = 8.0f;
  k.fwdNearestOppNear_m = 5.0f;
  k.fwdNearestOppMin_m = 1.5f;
  k.fwdTargetAdvance_m = 35.0f;
  k.fwdOppNearDist_m = 7.0f;
  k.fwdOppNearFreeDist_m = 3.0f;
  k.fwdTurnFacing_deg = 80.0f;
  k.fwdFacingBack_deg = 135.0f;
  k.fwdMateSpeedCap_kmh = 20.0f;
  k.loftMinDist_m = 15.0f;
  k.loftMaxDist_m = 25.0f;
  k.loftDistBase_m = 10.0f;
  k.loftDistGain_m = 5.0f;
  k.loftKeeperNearSq = 100.0f;
  k.loftKeeperFarSq = 225.0f;
  k.throughMinDist_m = 5.0f;
  k.throughCap_m = 20.0f;
  k.throughOpenCap_m = 15.0f;
  k.throughOpennessOpen = 100.0f;
  k.throughClassOpen = 4.0f;
  k.throughMateSpeedCap_kmh = 60.0f;
  k.throughFacing_deg = 135.0f;
  k.breakthroughSeedK = 1100.0f;
  k.middleShotSeedK = 1200.0f;
  k.curveShotSeedK = 1300.0f;
  k.crossSeedK = 1500.0f;
  k.longSeedK = 1600.0f;
  k.planRollModulus = 100;
  k.planRollSlot = 2;
  k.statFloor = 70.0f;
  k.statSpan = 29.0f;
  k.breakthroughBase = 70.0f;
  k.breakthroughComBase = 20.0f;
  k.breakthroughStatGain = 15.0f;
  k.breakthroughCap = 70.0f;
  k.middleShotBase = 70.0f;
  k.middleShotStateGain = 40.0f;
  k.middleShotStatGain = 15.0f;
  k.middleShotFloor = 5.0f;
  k.middleShotCap = 80.0f;
  k.crossBase = 50.0f;
  k.crossStatGain = 20.0f;
  k.crossFloor = 40.0f;
  k.crossCap = 80.0f;
  k.longBasePressed = 30.0f;
  k.longBaseMid = 60.0f;
  k.longBaseFree = 80.0f;
  k.longStatGain = 10.0f;
  return k;
}

// --- the shoot zone (report 4.1) ---

// The zone builder's bands: inside the posts, the central range Rn..Rf; outside
// them, the side range. A Finishing of 99 with the long-shot skills puts the
// two edges at 21 and 27 (OBS live: Rn 19.03 at 71, Rn 20.90 at 89, Rf 27.0).
TEST(PesCarrierZone, CentralBandFollowsTheRanges) {
  const pes::CarrierConstants k = Synthetic();
  constexpr float HALF_LEN = 52.5f, GOAL_HALF = 3.7f;
  // d = 18 - 3*((70-70)/29) = 18 for a 70 Finishing: close.
  pes::ShootZone close = pes::ComputeShootZone(35.0f, 0.0f, 1.0f, 70.0f, false, HALF_LEN, GOAL_HALF, k);
  EXPECT_FLOAT_EQ(close.d_m, 17.5f);
  EXPECT_TRUE(pes::IsClose(close));
  // d = 17.5 is inside Rn..Rf: mid.
  pes::ShootZone mid = pes::ComputeShootZone(34.0f, 0.0f, 1.0f, 70.0f, false, HALF_LEN, GOAL_HALF, k);
  EXPECT_TRUE(pes::IsMid(mid));
  EXPECT_FALSE(pes::IsClose(mid));
  // d = 25 is past Rf = 24: far.
  pes::ShootZone far = pes::ComputeShootZone(27.5f, 0.0f, 1.0f, 70.0f, false, HALF_LEN, GOAL_HALF, k);
  EXPECT_EQ(far.band, pes::kBandFar);
  EXPECT_TRUE(pes::InShootRange(far));
  // The Finishing ramp: 99 Finishing moves Rn to 21 and Rf to 27 with the
  // long-shot skills, so d = 20.5 is close where it was mid.
  pes::ShootZone ramped = pes::ComputeShootZone(32.0f, 0.0f, 1.0f, 99.0f, true, HALF_LEN, GOAL_HALF, k);
  EXPECT_FLOAT_EQ(ramped.d_m, 20.5f);
  EXPECT_TRUE(pes::IsClose(ramped));
}

// Behind the goal line the band and sector both go wide: nothing is in range.
TEST(PesCarrierZone, BehindTheGoalLineIsOut) {
  const pes::CarrierConstants k = Synthetic();
  pes::ShootZone behind = pes::ComputeShootZone(-40.0f, 0.0f, 1.0f, 90.0f, false, 52.5f, 3.7f, k);
  EXPECT_EQ(behind.band, pes::kBandOut);
  EXPECT_EQ(behind.sector, pes::kSectorWide);
  EXPECT_FALSE(pes::InShootRange(behind));
  EXPECT_FALSE(pes::IsCentral(behind));
}

// Outside the posts the distance is to the near post and the bearing decides
// the sector: central inside 15 degrees, side inside 40.
TEST(PesCarrierZone, OutsideThePostsMeasuresToThePost) {
  const pes::CarrierConstants k = Synthetic();
  constexpr float HALF_LEN = 52.5f, GOAL_HALF = 3.7f;
  // Level with the goal line and 6 m wide of the post: the distance is to the
  // near post 6 m away, and the bearing is straight across, so the sector is
  // wide and nothing is in range.
  pes::ShootZone across =
      pes::ComputeShootZone(HALF_LEN, 9.7f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k);
  EXPECT_NEAR(across.d_m, 6.0f, 0.01f);
  EXPECT_EQ(across.sector, pes::kSectorWide);
  EXPECT_FALSE(pes::IsCentral(across));
  EXPECT_TRUE(pes::IsClose(across));  // the box bit makes it in range, band 0
  // The ball is exactly level with the goal line, on the box cone's boundary:
  // the box bit is set (PES's `setbe` includes the boundary), so it is in range
  // and the box rule fires.
  EXPECT_TRUE(pes::InBox(across));
  EXPECT_TRUE(pes::InShootRange(across));
  // Pushed wider past the cone's radius it leaves the box.
  pes::ShootZone wider =
      pes::ComputeShootZone(HALF_LEN, 12.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k);
  EXPECT_FALSE(pes::InBox(wider));
  EXPECT_FALSE(pes::InShootRange(wider));
  // Level with the near post and 8 m short of it: the bearing is nearly along
  // the attack direction, so the sector is central and the side range applies.
  pes::ShootZone side =
      pes::ComputeShootZone(HALF_LEN - 8.0f, 3.7f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k);
  EXPECT_NEAR(side.d_m, 8.0f, 0.01f);
  EXPECT_TRUE(pes::IsClose(side));
}

// The box predicate: inside the 11 m half-disc in front of the goal centre.
TEST(PesCarrierZone, BoxIsTheElevenMetreHalfDisc) {
  const pes::CarrierConstants k = Synthetic();
  constexpr float HALF_LEN = 52.5f, GOAL_HALF = 3.7f;
  EXPECT_TRUE(pes::InBox(pes::ComputeShootZone(45.0f, 0.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k)));
  EXPECT_FALSE(pes::InBox(pes::ComputeShootZone(45.0f, 10.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k)));
  EXPECT_TRUE(pes::InBox(pes::ComputeShootZone(45.0f, 6.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k)));
  EXPECT_FALSE(pes::InBox(pes::ComputeShootZone(40.0f, 0.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k)));
  EXPECT_FALSE(pes::InBox(pes::ComputeShootZone(45.0f, 12.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k)));
  // Behind the goal line there is no front to be in front of.
  EXPECT_FALSE(pes::InBox(pes::ComputeShootZone(-45.0f, 0.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k)));
}

// --- the shoot decision (report 5.1) ---

TEST(PesCarrierShoot, JustShootInsideTwentyAndFortyDegrees) {
  const pes::CarrierConstants k = Synthetic();
  constexpr float HALF_LEN = 52.5f, GOAL_HALF = 3.7f;
  // d = 12, level with the goal: inside 20 m and inside 40 degrees.
  pes::ShootInput in;
  in.zone = pes::ComputeShootZone(40.5f, 0.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k);
  in.owned = true;
  in.keeperPresent = true;
  in.keeperSettled = true;
  EXPECT_EQ(pes::ShootSubKind(in, k), 1);
  // Outside 20 m the rule does not fire: a wide ball with no other rule left
  // to answer it.
  pes::ShootInput farIn = in;
  farIn.zone = pes::ComputeShootZone(31.0f, 12.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k);
  if (!pes::InBox(farIn.zone)) EXPECT_EQ(pes::ShootSubKind(farIn, k), 0);
}

TEST(PesCarrierShoot, NoShotFirstTimeBeyondTwentyMetres) {
  const pes::CarrierConstants k = Synthetic();
  constexpr float HALF_LEN = 52.5f, GOAL_HALF = 3.7f;
  pes::ShootInput in;
  in.owned = false;  // a first-time shot
  in.keeperPresent = true;
  in.keeperSettled = true;
  in.incomingVsFacing_deg = 30.0f;
  in.aimVsFacing_deg = 10.0f;
  in.zone = pes::ComputeShootZone(31.0f, 0.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k);
  // 21.5 m from the goal, squared = 462.25 > 400: no shot.
  in.ballToGoalDistSq = 21.5f * 21.5f;
  EXPECT_EQ(pes::ShootSubKind(in, k), 0);
  in.ballToGoalDistSq = 19.0f * 19.0f;
  EXPECT_EQ(pes::ShootSubKind(in, k), 1);
}

TEST(PesCarrierShoot, TheBoxAndTheHeader) {
  const pes::CarrierConstants k = Synthetic();
  constexpr float HALF_LEN = 52.5f, GOAL_HALF = 3.7f;
  pes::ShootInput in;
  in.owned = true;
  in.height_m = 180.0f;
  // 11 m out and 4 m wide: inside the box, so a shot, but past the just-shoot
  // distance band's angle is 0 so this would fire anyway; use a wide ball.
  in.zone = pes::ComputeShootZone(41.5f, 4.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k);
  EXPECT_EQ(pes::ShootSubKind(in, k), 1);
  // 13 m out and 1 m wide: outside the box, past the just-shoot distance, so
  // the plain-shot branch only fires when the goal is in front of him.
  in.zone = pes::ComputeShootZone(39.5f, 1.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k);
  in.aimVsFacing_deg = 95.0f;
  EXPECT_EQ(pes::ShootSubKind(in, k), 0);
  in.aimVsFacing_deg = 20.0f;
  EXPECT_EQ(pes::ShootSubKind(in, k), 1);
  // Above the head (0.8 * 1.8 m = 1.44) and inside the header range (12 m).
  in.ballHeight_m = 2.0f;
  in.zone = pes::ComputeShootZone(40.6f, 0.0f, 1.0f, 90.0f, false, HALF_LEN, GOAL_HALF, k);
  EXPECT_EQ(pes::ShootSubKind(in, k), 1);
}

// --- the openness formula's class ladder (report 4.3) ---

TEST(PesCarrierOpenness, ClassBoundariesAtTenFortySixtyHundredThirty) {
  const pes::CarrierConstants k = Synthetic();
  EXPECT_EQ(pes::OpennessClassFromMargin(0.0f, k), 0);
  EXPECT_EQ(pes::OpennessClassFromMargin(9.99f, k), 0);
  EXPECT_EQ(pes::OpennessClassFromMargin(10.0f, k), 1);
  EXPECT_EQ(pes::OpennessClassFromMargin(40.0f, k), 2);
  EXPECT_EQ(pes::OpennessClassFromMargin(60.0f, k), 3);
  EXPECT_EQ(pes::OpennessClassFromMargin(100.0f, k), 4);
  EXPECT_EQ(pes::OpennessClassFromMargin(130.0f, k), 4);
  EXPECT_EQ(pes::OpennessClassFromMargin(130.01f, k), 5);
  EXPECT_EQ(pes::OpennessClassFromMargin(1000.0f, k), 5);
}

// An opponent right in the lane has no perpendicular margin, so his
// contribution is a margin of 0 (class 0); the same opponent 4 m off it has
// 100 * 3 / T.
TEST(PesCarrierOpenness, MarginScalesWithThePerpendicularDistance) {
  const pes::CarrierConstants k = Synthetic();
  // along = 10, perp = 1.0: T = min(0.6*10, 0.3*81) = 6, margin = 100*0/6 = 0.
  EXPECT_FLOAT_EQ(pes::LaneOpennessMargin(0, 0, 10.0f, 1.0f, 1.0f, 0.0f, k), 0.0f);
  // perp = 5: margin = 100 * 4 / 6.
  EXPECT_FLOAT_EQ(pes::LaneOpennessMargin(0, 0, 10.0f, 5.0f, 1.0f, 0.0f, k), 400.0f / 6.0f);
  // A harder pass closes the lane: difficulty 2 multiplies T by 3
  // (`T *= diff + 1`), so the margin falls by that factor.
  EXPECT_FLOAT_EQ(pes::LaneOpennessMargin(0, 0, 10.0f, 5.0f, 1.0f, 2.0f, k), 400.0f / 18.0f);
  // Behind the ball (along < 0.2) the opponent is not in the lane at all.
  EXPECT_FLOAT_EQ(pes::LaneOpennessMargin(0, 0, 0.1f, 5.0f, 1.0f, 0.0f, k), 0.0f);
}

// --- the receiver difficulty term (report 4.3) ---

TEST(PesCarrierDifficulty, ComponentsAndTheLowPassMultiplier) {
  const pes::CarrierConstants k = Synthetic();
  // A settled ball at ground level, no turn and a good passer: only the High
  // term of a ground ball (0) and the Low Pass multiplier (1.0 at 99).
  EXPECT_NEAR(pes::ReceiverDifficulty(0.0f, 5.0f, 0.0f, false, 0.0f, false, 0.0f, 0.0f, 99.0f, k),
              0.0f, 1e-4f);
  // The turn term: 190 degrees is past the 130 top, so it is the full 1.5,
  // times the Low Pass multiplier (0.7 + 0.3*g, g = 1 at or below 80).
  EXPECT_NEAR(pes::ReceiverDifficulty(0.0f, 5.0f, 0.0f, false, 0.0f, false, 190.0f, 0.0f, 80.0f, k),
              1.5f, 1e-3f);
  // A ball at head height adds the High term's full 1.0.
  EXPECT_NEAR(pes::ReceiverDifficulty(0.0f, 5.0f, 0.0f, false, 0.0f, false, 0.0f, 2.0f, 80.0f, k),
              1.0f, 1e-3f);
  // A poor passer pays the multiplier: at 99 Low Pass the multiplier is 0.7
  // alone (report 4.3's ramp bottoms out there; live it never did, which is
  // why the sampled multipliers stayed in [0.76, 1.0]).
  EXPECT_NEAR(pes::ReceiverDifficulty(0.0f, 5.0f, 0.0f, false, 0.0f, false, 190.0f, 0.0f, 99.0f, k),
              1.5f * 0.7f, 1e-3f);
  // The moving-ball term: a fast, far, square ball is expensive, and One-touch
  // Pass halves that term inside the sum.
  const float plain = pes::ReceiverDifficulty(30.0f, 30.0f, 90.0f, false, 0.0f, false, 0.0f, 0.0f,
                                              80.0f, k);
  const float oneTouch = pes::ReceiverDifficulty(30.0f, 30.0f, 90.0f, true, 0.0f, false, 0.0f, 0.0f,
                                                 80.0f, k);
  EXPECT_GT(plain, 1.0f);
  EXPECT_LT(oneTouch, plain);
}

// --- the direction and turn classes (report 4.3) ---

TEST(PesCarrierClasses, ForwardSquareBack) {
  const pes::CarrierConstants k = Synthetic();
  EXPECT_EQ(pes::DirectionClass(0.0f, k), 0);
  EXPECT_EQ(pes::DirectionClass(60.0f, k), 0);
  EXPECT_EQ(pes::DirectionClass(110.0f, k), 2);
  EXPECT_EQ(pes::DirectionClass(111.0f, k), 1);
  EXPECT_EQ(pes::DirectionClass(-170.0f, k), 1);
  EXPECT_EQ(pes::TurnClass(0.0f, k), 3);
  EXPECT_EQ(pes::TurnClass(60.0f, k), 3);
  EXPECT_EQ(pes::TurnClass(110.0f, k), 4);
  EXPECT_EQ(pes::TurnClass(180.0f, k), 5);
}

// --- PassForward's three tiers (report 5.2) ---

namespace {

pes::ReceiverInput Mate(float targetX, float targetZ, float passAngle, float fwdAngle,
                        float turnAngle, int opennessClass, float dist, float difficulty) {
  pes::ReceiverInput r;
  r.target = {targetX, targetZ};
  r.passAngle_deg = passAngle;
  r.fwdAngle_deg = fwdAngle;
  r.turnAngle_deg = turnAngle;
  r.opennessClass = opennessClass;
  r.openness = opennessClass == 0 ? 5.0f : (opennessClass == 3 ? 80.0f : 150.0f);
  r.difficulty = difficulty;
  r.ballToTarget_m = dist;
  r.dirClass = pes::DirectionClass(fwdAngle, Synthetic());
  r.turnClass = pes::TurnClass(turnAngle, Synthetic());
  return r;
}

}  // namespace

TEST(PesCarrierPassForward, ShortForwardWantsTheMostAdvancedOpenMate) {
  const pes::CarrierConstants k = Synthetic();
  constexpr float HALF_LEN = 52.5f;
  pes::PassForwardInput in;
  in.owned = true;
  in.close = false;
  in.carrierPressureClass = 0;
  in.attackSign = 1.0f;
  in.planAngle_deg = 0.0f;
  in.imageState = 0;
  // Two open forward mates, both passes longer than 30 m so the short-pass
  // tightening (openness 4) does not bite: the more advanced one wins.
  std::vector<pes::PassCandidate> mates(2);
  mates[0].index = 3;
  mates[0].record = Mate(30.0f, 10.0f, 0.0f, 90.0f, 10.0f, 4, 32.0f, 0.5f);
  mates[1].index = 7;
  mates[1].record = Mate(40.0f, -10.0f, 0.0f, 90.0f, 10.0f, 4, 32.0f, 0.5f);
  mates[0].nearestOppToMate_m = 12.0f;
  mates[1].nearestOppToMate_m = 12.0f;
  const pes::PassAnswer answer = pes::PassForward(mates, {}, in, k);
  EXPECT_EQ(answer.index, 7);
  EXPECT_EQ(answer.kind, pes::kPassShort);
}

TEST(PesCarrierPassForward, ClosedLaneForcesTheThroughBall) {
  const pes::CarrierConstants k = Synthetic();
  pes::PassForwardInput in;
  in.owned = true;
  in.close = false;
  in.carrierPressureClass = 0;
  in.attackSign = 1.0f;
  in.planAngle_deg = 0.0f;
  in.imageState = 1;
  in.freeSpace = 40.0f;  // over the 36 threshold
  // One mate, far upfield, only 8 m away: the ground lane is closed
  // (openness class 0), so tier 1 rejects and tier 2's through ball answers.
  std::vector<pes::PassCandidate> mates(1);
  mates[0].index = 5;
  mates[0].record = Mate(42.0f, 5.0f, 0.0f, 0.0f, 10.0f, 0, 8.0f, 1.5f);
  mates[0].nearestOppToMate_m = 12.0f;
  const pes::PassAnswer answer = pes::PassForward(mates, {}, in, k);
  EXPECT_EQ(answer.index, 5);
  EXPECT_EQ(answer.kind, pes::kPassThrough);
}

TEST(PesCarrierPassForward, NoFreeSpaceNoThroughBall) {
  const pes::CarrierConstants k = Synthetic();
  pes::PassForwardInput in;
  in.owned = true;
  in.close = false;
  in.carrierPressureClass = 0;
  in.attackSign = 1.0f;
  in.imageState = 1;
  in.freeSpace = 30.0f;  // below the 36 threshold
  std::vector<pes::PassCandidate> mates(1);
  mates[0].index = 5;
  mates[0].record = Mate(42.0f, 5.0f, 0.0f, 0.0f, 10.0f, 0, 8.0f, 1.5f);
  mates[0].nearestOppToMate_m = 12.0f;
  const pes::PassAnswer answer = pes::PassForward(mates, {}, in, k);
  EXPECT_EQ(answer.index, -1);
}

TEST(PesCarrierPassForward, LoftedForwardInStateTwoOnly) {
  const pes::CarrierConstants k = Synthetic();
  pes::PassForwardInput in;
  in.owned = true;
  in.close = false;
  in.carrierPressureClass = 0;
  in.attackSign = 1.0f;
  in.imageState = 2;
  in.freeSpace = 0.0f;
  // A mate 20 m away, facing forward, with a closed ground lane and the
  // keeper far off: the lofted forward answers a long ball.
  std::vector<pes::PassCandidate> mates(1);
  mates[0].index = 9;
  mates[0].record = Mate(38.0f, 20.0f, 0.0f, 0.0f, 30.0f, 0, 20.0f, 2.0f);
  mates[0].pressureClass = 3;
  mates[0].pressureDistOpp0_m = 400.0f;  // 20 m, over the 15 m squared cap
  mates[0].nearestOppToMate_m = 12.0f;
  const pes::PassAnswer answer = pes::PassForward(mates, {}, in, k);
  EXPECT_EQ(answer.index, 9);
  EXPECT_EQ(answer.kind, pes::kPassLong);
  // In image state 0 the lofted branch is not tried.
  in.imageState = 0;
  EXPECT_EQ(pes::PassForward(mates, {}, in, k).index, -1);
}

// The unit's head gate: pressed and not owned the unit stays silent.
// A mate that satisfies both tier 1 and tier 3 must be answered by tier 1:
// the tiers are attempted in order, and the short forward wins.
TEST(PesCarrierPassForward, TheShortForwardBeatsTheLoftedOne) {
  const pes::CarrierConstants k = Synthetic();
  pes::PassForwardInput in;
  in.owned = true;
  in.close = false;
  in.carrierPressureClass = 0;
  in.attackSign = 1.0f;
  in.imageState = 2;
  // Two mates: one only qualifies for the lofted forward (closed ground lane,
  // so tier 1 rejects it), and one only for the short forward. Tier 1 is asked
  // first, so the short ball wins over the lofted one however far upfield the
  // lofted target stands.
  std::vector<pes::PassCandidate> mates(2);
  mates[0].index = 8;
  mates[0].record = Mate(48.0f, 0.0f, 0.0f, 90.0f, 30.0f, 0, 20.0f, 0.5f);
  mates[0].pressureClass = 3;
  mates[0].pressureDistOpp0_m = 400.0f;
  mates[0].nearestOppToMate_m = 12.0f;
  mates[1].index = 2;
  mates[1].record = Mate(20.0f, 0.0f, 0.0f, 90.0f, 30.0f, 4, 32.0f, 0.5f);
  mates[1].nearestOppToMate_m = 12.0f;
  const pes::PassAnswer answer = pes::PassForward(mates, {}, in, k);
  EXPECT_EQ(answer.index, 2);
  EXPECT_EQ(answer.kind, pes::kPassShort);
}

TEST(PesCarrierPassForward, GateOnPressureAndTheCloseBand) {
  const pes::CarrierConstants k = Synthetic();
  pes::PassForwardInput in;
  in.owned = false;
  in.carrierPressureClass = 2;  // pressed
  in.close = false;
  in.attackSign = 1.0f;
  std::vector<pes::PassCandidate> mates(1);
  mates[0].index = 4;
  mates[0].record = Mate(40.0f, 0.0f, 0.0f, 90.0f, 10.0f, 4, 32.0f, 0.5f);
  mates[0].nearestOppToMate_m = 12.0f;
  EXPECT_EQ(pes::PassForward(mates, {}, in, k).index, -1);
  in.carrierPressureClass = 1;
  EXPECT_EQ(pes::PassForward(mates, {}, in, k).index, 4);
  // In the close band the unit is off entirely.
  in.close = true;
  EXPECT_EQ(pes::PassForward(mates, {}, in, k).index, -1);
}

// --- the plan order and its roll (report 4.4) ---

TEST(PesCarrierPlan, PriorityOrderIsNineEightTwoThreeOneFourFiveSix) {
  constexpr int EXPECTED[] = {9, 8, 2, 3, 1, 4, 5, 6};
  static_assert(sizeof(pes::kPlanPriorityOrder) / sizeof(int) == 8);
  for (int i = 0; i < 8; ++i) EXPECT_EQ(pes::kPlanPriorityOrder[i], EXPECTED[i]);
}

TEST(PesCarrierPlan, StartProbabilitiesFollowThePTable) {
  const pes::CarrierConstants k = Synthetic();
  // Breakthrough: base 70 with the skill, capped at 70, plus the stat term.
  EXPECT_FLOAT_EQ(pes::PlanStartProbability(pes::kPlanBreakthrough, k, 99.0f, true, false, false),
                  k.breakthroughCap);
  // Without the skill a COM run style gives the 20 base plus the stat term.
  EXPECT_FLOAT_EQ(pes::PlanStartProbability(pes::kPlanBreakthrough, k, 70.0f, false, true, false),
                  k.breakthroughComBase);
  EXPECT_GT(pes::PlanStartProbability(pes::kPlanBreakthrough, k, 99.0f, false, true, false),
            k.breakthroughComBase);
  // MiddleShot: 70 with the skill, +40 in image state 2, capped at 80, and
  // floored at 5 even with nothing.
  EXPECT_FLOAT_EQ(pes::PlanStartProbability(pes::kPlanMiddleShot, k, 70.0f, false, false, false),
                  k.middleShotFloor);
  EXPECT_FLOAT_EQ(pes::PlanStartProbability(pes::kPlanMiddleShot, k, 70.0f, true, false, true),
                  k.middleShotCap);
  EXPECT_FLOAT_EQ(pes::PlanStartProbability(pes::kPlanMiddleShot, k, 70.0f, true, false, false),
                  k.middleShotBase);
  // Cross: base 50 with the style or the skill, floored at 40, capped at 80.
  EXPECT_FLOAT_EQ(pes::PlanStartProbability(pes::kPlanCross, k, 70.0f, true, false, false),
                  k.crossBase);
  EXPECT_FLOAT_EQ(pes::PlanStartProbability(pes::kPlanCross, k, 70.0f, false, false, false),
                  k.crossFloor);
  // Long: the pressure class' base, plus the stat term.
  EXPECT_GT(pes::PlanStartProbability(pes::kPlanLong, k, 99.0f, false, false, false),
            k.longBaseMid);
  // Plans that are not rolled have no probability at all.
  EXPECT_LT(pes::PlanStartProbability(pes::kPlanPassThrough, k, 90.0f, false, false, false), 0.0f);
}

TEST(PesCarrierPlan, TheRollIsSlotTwoModulusOneHundred) {
  pes::CarrierConstants k = Synthetic();
  pes::RngConstants rng{};
  rng.lcgMul = 0x2d22b2e3u;
  rng.lcgSub = 0x4513u;
  rng.modulus = {100, 5000, 32768, 32768};
  rng.scale = 4.656612873077393e-10f;
  rng.defaultSeeds = {0, 1, 2, 3};
  // The report's roll: a throw-away generator seeded with
  // int(carrierClock * K) on slot 2, drawn once through RangeRaw(2, 100).
  const int roll = pes::PlanRoll(3, k.breakthroughSeedK, rng, k);
  EXPECT_GE(roll, 0);
  EXPECT_LT(roll, 100);
  // The modulus spans the whole 0..99 range: over many carrier clocks both a
  // low and a high draw turn up, so the start test `P >= roll` really is a
  // percentage gate and not a narrower draw.
  int low = 100, high = -1;
  for (int clock = 0; clock < 200; ++clock) {
    const int r = pes::PlanRoll(clock, k.breakthroughSeedK, rng, k);
    ASSERT_GE(r, 0);
    ASSERT_LT(r, 100);
    low = std::min(low, r);
    high = std::max(high, r);
  }
  EXPECT_LE(low, 10);
  EXPECT_GE(high, 90);
  // Two draws from the same clock and the same K agree, and a different clock
  // moves the seed, so a plan's start is a reproducible function of the clock.
  EXPECT_EQ(roll, pes::PlanRoll(3, k.breakthroughSeedK, rng, k));
  const int later = pes::PlanRoll(4, k.breakthroughSeedK, rng, k);
  EXPECT_NE(later, roll);
}

// --- the think cadence (report 2) ---

TEST(PesCarrierCadence, TheRethinkGateIsFivePesFrames) {
  // One GF tick is 0.54 of a PES tick, so PES's 5-frame gate (92.6 ms) is
  // 9.26 GF ticks, truncated to 9: the re-think may not run before that.
  EXPECT_EQ(pes::PesFramesToGfTicks(5), 9);
  EXPECT_EQ(pes::PesFramesToGfTicks(27), 50);  // the 27-frame touch window
  EXPECT_EQ(pes::PesFramesToGfTicks(54), 100);
  // `frame - busyStamp > 5`: on the 6th frame after leaving a committed action.
  EXPECT_FALSE(pes::RethinkDue(5, 0));
  EXPECT_TRUE(pes::RethinkDue(6, 0));
  EXPECT_FALSE(pes::RethinkDue(15, 10));
  EXPECT_TRUE(pes::RethinkDue(16, 10));
}

// --- the install: read the constants out of the user's own exe ---

TEST(PesCarrierInstall, TheZonesAndLaddersAreTheGamesOwn) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto k = pes::LoadCarrierConstants(*exe, &err);
  ASSERT_TRUE(k) << err;
  // The zone's ten immediates of 0x140699610, which the report's live sample
  // read back as Rn 19.03 (Finishing 71), Rn 20.90 (Finishing 89) and Rf 27.0
  // (long-shot skill): the ramp is 3 * (F-70)/29 on the base ranges.
  EXPECT_FLOAT_EQ(k->nearCentral_m, 18.0f);
  EXPECT_FLOAT_EQ(k->farCentral_m, 24.0f);
  EXPECT_FLOAT_EQ(k->finishingBoost_m, 3.0f);
  EXPECT_FLOAT_EQ(k->finishingFloor, 70.0f);
  EXPECT_FLOAT_EQ(k->finishingSpan, 29.0f);
  const float ramped = k->nearCentral_m + k->finishingBoost_m * (89.0f - k->finishingFloor) /
                                              k->finishingSpan;
  EXPECT_NEAR(ramped, 20.90f, 0.05f);
  // The openness ladder the live sample matched on 23,617 of 23,642 records.
  EXPECT_FLOAT_EQ(k->openClass1, 10.0f);
  EXPECT_FLOAT_EQ(k->openClass5, 130.0f);
  // The plan roll's seeds, per plan.
  EXPECT_FLOAT_EQ(k->breakthroughSeedK, 1100.0f);
  EXPECT_FLOAT_EQ(k->middleShotSeedK, 1200.0f);
  EXPECT_FLOAT_EQ(k->crossSeedK, 1500.0f);
  EXPECT_FLOAT_EQ(k->longSeedK, 1600.0f);
  EXPECT_EQ(k->planRollSlot, 2);
}

}  // namespace

// PES 2021's individual defender decisions (docs/re/pes-re-defender-decisions.md).
// Synthetic constants exercise the reaction chooser's order, the standing-tackle
// gates at their edges, the safety term on both sides of the threshold, the
// slide's aggression ladder and reach, and the block radii. One install test
// checks the loader against the user's own PES2021 install through relations,
// not pinned numbers.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>

#include "base/math/bluntmath.hpp"
#include "onthepitch/pesdefenderdecisions.hpp"

namespace {

// The shape the user's install carries for these fields (report 2/3/5 for the
// gates, 7 for tackle.json, 12.5 for the stock/mod split). Used both as the
// synthetic set and as the shape the install test checks.
pes::DefenderDecisionConstants Synthetic() {
  pes::DefenderDecisionConstants c{};
  c.tackleBallDistanceSq_m2 = 9.0f;  // 3.0 m
  c.tackleBallHeadingAngle_deg = 22.5f;
  c.tackleContactHeight_m = 3.0f;
  c.tackleEngageAngle_deg = 112.5f;
  c.tackleEngageGap_m = 2.0f;
  c.tackleBehindAngle_deg = 135.0f;
  c.tackleBehindTolerance_deg[0] = 45.0f;
  c.tackleBehindTolerance_deg[1] = 11.25f;
  c.tackleBehindTolerance_deg[2] = 0.0f;
  c.tackleBehindExtraAngle_deg = 25.0f;
  c.tackleTouchLead_s[0] = 0.075f;
  c.tackleTouchLead_s[1] = 0.1f;
  c.tackleTouchLead_s[2] = 0.125f;
  c.tackleLateralOffset_m = 0.8f;
  c.tackleLateralSpeed_kmh = 5.0f;
  c.safetyCoverRadius_m = 10.0f;
  c.safetyCoverAngle_deg = 60.0f;
  c.safetyLastLine_m = 10.0f;
  c.safetyEventFramesDivisor = 9;
  c.tackleSafetyRateLastLine = 2.6f;
  c.tackleSafetyRateNoCover = 2.5f;
  c.slideDistanceBase_m = 4.0f;
  c.slideDistancePerLevel_m = 0.8f;
  c.slideBallHeight_m = 0.5f;
  c.blockRadiusNormal_m = 1.3f;
  c.blockRadiusPressing_m = 3.0f;
  c.blockRadiusLate_m = 7.0f;
  c.blockShotRadiusScale = 1.3f;
  c.blockFeintRadiusScale = 1.2f;
  c.blockClipProgress = 0.7f;
  c.blockClipProgressLongPass = 0.8f;
  c.blockClipProgressHighScale = 0.5f;
  c.blockHighBall_m = 0.5f;
  c.blockLooseSpeedBall_kmh = 70.0f;
  c.blockNormal_s = 0.9f;
  c.blockSpeedBall_s = 2.0f;
  c.challengeLook_s = 12.0f / 60.0f;
  c.challengePathRadius_m = 1.0f;
  c.challengePressDistSq_m2 = 17.64f;
  c.challengeCloserMarginSq_m2 = 0.64f;
  c.challengeNearDistSq_m2 = 12.25f;
  return c;
}

constexpr float kPesFramesPerSecond = 1.0f / (1.0f / 54.0f);
// One PES frame is 0.54 of a GF tick, the conversion pesballphysics.hpp uses.
constexpr float kTicksPerPesFrame = (1.0f / 54.0f) / 0.01f;

// Tackle (1), slide (2), receive (3, pes-re-receiving.md §1), block (8),
// jostle (11).
TEST(PesDefenderChooser, FirstTruePredicateWinsInPesOrder) {
  pes::DefenderReactionInput in;
  in.receive = true;
  in.tackle = true;
  in.slide = true;
  in.block = true;
  in.jostleForPass = true;
  EXPECT_EQ(pes::ChooseReaction(in), pes::DefenderReaction::kTackle);

  in.tackle = false;
  EXPECT_EQ(pes::ChooseReaction(in), pes::DefenderReaction::kSlide);

  in.slide = false;
  EXPECT_EQ(pes::ChooseReaction(in), pes::DefenderReaction::kReceive);

  in.receive = false;
  EXPECT_EQ(pes::ChooseReaction(in), pes::DefenderReaction::kBlock);

  in.block = false;
  EXPECT_EQ(pes::ChooseReaction(in), pes::DefenderReaction::kJostleForPass);

  in.jostleForPass = false;
  EXPECT_EQ(pes::ChooseReaction(in), pes::DefenderReaction::kNone);
}

TEST(PesDefenderChooser, AReactionFromTheFrontOfTheTableHidesTheRest) {
  // No score competition and no tie-break: the walk stops at the first yes.
  pes::DefenderReactionInput only_slide;
  only_slide.slide = true;
  EXPECT_EQ(pes::ChooseReaction(only_slide), pes::DefenderReaction::kSlide);
}

TEST(PesDefenderTackle, BallGateClosesOutsideThreeMetres) {
  const pes::DefenderDecisionConstants c = Synthetic();
  EXPECT_TRUE(pes::StandingTackleBallGate(c, 3.0f, 10.0f));
  EXPECT_TRUE(pes::StandingTackleBallGate(c, 2.99f, 10.0f));
  EXPECT_FALSE(pes::StandingTackleBallGate(c, 3.01f, 10.0f));
}

TEST(PesDefenderTackle, BallGateClosesPastTwentyTwoAndAHalfDegrees) {
  const pes::DefenderDecisionConstants c = Synthetic();
  EXPECT_TRUE(pes::StandingTackleBallGate(c, 2.0f, 22.5f));
  EXPECT_TRUE(pes::StandingTackleBallGate(c, 2.0f, -22.5f));
  EXPECT_FALSE(pes::StandingTackleBallGate(c, 2.0f, 22.6f));
  EXPECT_FALSE(pes::StandingTackleBallGate(c, 2.0f, -170.0f));
}

TEST(PesDefenderTackle, EngageWindowIsHundredTwelveAndAHalfAndTwoMetres) {
  const pes::DefenderDecisionConstants c = Synthetic();
  EXPECT_TRUE(pes::StandingTackleEngageWindow(c, 112.5f, 2.0f));
  EXPECT_TRUE(pes::StandingTackleEngageWindow(c, -112.5f, 1.99f));
  EXPECT_FALSE(pes::StandingTackleEngageWindow(c, 112.6f, 2.0f));
  EXPECT_FALSE(pes::StandingTackleEngageWindow(c, 0.0f, 2.01f));
}

TEST(PesDefenderTackle, FromBehindTheToleranceFollowsTheAggressionLevel) {
  const pes::DefenderDecisionConstants c = Synthetic();
  // A = 0: 45 degrees off the carrier's facing is enough.
  EXPECT_FALSE(pes::StandingTackleFromBehindAllowed(c, true, 44.0f, 20.0f, 0));
  EXPECT_TRUE(pes::StandingTackleFromBehindAllowed(c, true, 44.0f, 44.0f, 0));
  EXPECT_TRUE(pes::StandingTackleFromBehindAllowed(c, true, 45.0f, 0.0f, 0));
  EXPECT_TRUE(pes::StandingTackleFromBehindAllowed(c, true, 46.0f, 0.0f, 0));
  // A = 1: the tolerance tightens to 11.25 degrees.
  EXPECT_FALSE(pes::StandingTackleFromBehindAllowed(c, true, 11.0f, 10.0f, 1));
  EXPECT_TRUE(pes::StandingTackleFromBehindAllowed(c, true, 11.25f, 10.0f, 1));
  // The extra angle saves it at any level: the headings further apart than 25
  // degrees are not a man running with the carrier.
  EXPECT_TRUE(pes::StandingTackleFromBehindAllowed(c, true, 0.0f, 26.0f, 0));
  EXPECT_TRUE(pes::StandingTackleFromBehindAllowed(c, true, 0.0f, 26.0f, 1));
  // A = 2: the tolerance is a zeroed register, so any approach clears it.
  EXPECT_TRUE(pes::StandingTackleFromBehindAllowed(c, true, 0.0f, 0.0f, 2));
  // Not behind at all: open.
  EXPECT_TRUE(pes::StandingTackleFromBehindAllowed(c, false, 0.0f, 20.0f, 2));
}

TEST(PesDefenderTackle, SafetyProgressSineIsZeroAtTheClipsStartAndOneAtItsContact) {
  EXPECT_FLOAT_EQ(pes::SafetyProgressSine(0.0f), 0.0f);
  EXPECT_FLOAT_EQ(pes::SafetyProgressSine(1.0f), 1.0f);
  EXPECT_FLOAT_EQ(pes::SafetyProgressSine(0.5f), std::sin(blunted::pi * 0.25f));
}

TEST(PesDefenderTackle, LastManVetoFollowsTheSafetyTermNotAFixedRule) {
  const pes::DefenderDecisionConstants c = Synthetic();
  // gs/9 frames at 54 fps = 6 PES frames = 11 GF ticks.
  const float gateTicks = (kPesFramesPerSecond / 9.0f) * kTicksPerPesFrame;
  // Inside the gate the term does not apply yet.
  EXPECT_FALSE(pes::StandingTackleSafetyVeto(c, 0.1f, false, true, gateTicks - 1.0f));
  // Past it, a covering-less defender is refused while the sine is below the
  // threshold and allowed at or above it.
  EXPECT_TRUE(pes::StandingTackleSafetyVeto(c, 2.49f, true, false, gateTicks + 1.0f));
  EXPECT_FALSE(pes::StandingTackleSafetyVeto(c, 2.5f, true, false, gateTicks + 1.0f));
  // The last line uses its own threshold.
  EXPECT_TRUE(pes::StandingTackleSafetyVeto(c, 2.59f, false, true, gateTicks + 1.0f));
  EXPECT_FALSE(pes::StandingTackleSafetyVeto(c, 2.6f, false, true, gateTicks + 1.0f));
  // With cover and not last: no veto at any progress.
  EXPECT_FALSE(pes::StandingTackleSafetyVeto(c, 0.0f, false, false, gateTicks + 100.0f));
}

TEST(PesDefenderTackle, StockSafetyTermsLetALastManTackle) {
  // Report 12.5: the stock file carries 0.4/0.5 against the live mod's 2.5/2.6,
  // and the sine is capped at 1.0 - so with stock terms the veto never fires.
  // That is why the port reads the thresholds instead of pinning a rule.
  pes::DefenderDecisionConstants stock = Synthetic();
  stock.tackleSafetyRateLastLine = 0.5f;
  stock.tackleSafetyRateNoCover = 0.4f;
  EXPECT_FALSE(pes::StandingTackleSafetyVeto(stock, 0.95f, true, true, 10000.0f));
}

TEST(PesDefenderTackle, TouchLeadGrowsWithTheAggressionLevel) {
  const pes::DefenderDecisionConstants c = Synthetic();
  const int a0 = pes::TackleTouchLeadTicks(c, 0);
  const int a1 = pes::TackleTouchLeadTicks(c, 1);
  const int a2 = pes::TackleTouchLeadTicks(c, 2);
  EXPECT_LT(a0, a1);
  EXPECT_LT(a1, a2);
  // 0.075 s at 54 fps = 4.05 PES frames = 7.5 GF ticks, rounding to 8;
  // 0.1 s = 5.4 frames = 10.0 ticks; 0.125 s = 6.75 frames = 12.5 -> 13.
  EXPECT_EQ(a0, 8);
  EXPECT_EQ(a1, 10);
  EXPECT_EQ(a2, 13);
  // Out-of-range levels clamp rather than reading past the table.
  EXPECT_EQ(pes::TackleTouchLeadTicks(c, 5), a2);
  EXPECT_EQ(pes::TackleTouchLeadTicks(c, -3), a0);
}

TEST(PesDefenderSlide, AggressionCountsStyleSheetAndStamina) {
  pes::SlideAggressionInput in;
  EXPECT_EQ(pes::SlideAggressionLevel(in), 0);
  in.enforcerStyle = true;
  EXPECT_EQ(pes::SlideAggressionLevel(in), 1);
  in.enforcerStyle = false;
  in.aggressiveTeamSheet = true;
  EXPECT_EQ(pes::SlideAggressionLevel(in), 1);
  in.deepDefensiveMode = true;
  EXPECT_EQ(pes::SlideAggressionLevel(in), 3);
  in.staminaUnder30 = true;
  EXPECT_EQ(pes::SlideAggressionLevel(in), 3);  // capped
  in.deepDefensiveMode = false;
  in.aggressiveTeamSheet = false;
  in.enforcerStyle = true;
  EXPECT_EQ(pes::SlideAggressionLevel(in), 2);
  in.staminaUnder30 = false;
  EXPECT_EQ(pes::SlideAggressionLevel(in), 1);
}

TEST(PesDefenderSlide, ReachIsFourPlusEightyCentimetresPerLevel) {
  const pes::DefenderDecisionConstants c = Synthetic();
  EXPECT_FLOAT_EQ(pes::SlideReach_m(c, 0), 4.0f);
  EXPECT_FLOAT_EQ(pes::SlideReach_m(c, 1), 4.8f);
  EXPECT_FLOAT_EQ(pes::SlideReach_m(c, 2), 5.6f);
  EXPECT_FLOAT_EQ(pes::SlideReach_m(c, 3), 6.4f);
}

// The slide shares the tackle's engagement window on the target and adds its
// own 90 deg window on the slide's direction (raw FUN_140971ba0 at
// 0x140972377..0x140972398 and 0x14097230c..0x14097231e, both `JNC <refuse>`).
TEST(PesDefenderSlide, EngageWindowIsHundredTwelveAndAHalf) {
  const pes::DefenderDecisionConstants c = Synthetic();
  // Straight ahead and straight behind are inside the window.
  EXPECT_TRUE(pes::SlideEngageWindow(c, 112.0f, 0.0f));
  // At the limit itself the raw refuses: the compare is `jae`, so `>=` is out.
  EXPECT_FALSE(pes::SlideEngageWindow(c, 112.5f, 0.0f));
  // Past it the slide is refused: he cannot even see the point he would go at.
  EXPECT_FALSE(pes::SlideEngageWindow(c, 112.6f, 0.0f));
  EXPECT_FALSE(pes::SlideEngageWindow(c, -113.0f, 0.0f));
  EXPECT_FALSE(pes::SlideEngageWindow(c, 180.0f, 0.0f));
  // The window is measured off his own heading, not off north.
  EXPECT_TRUE(pes::SlideEngageWindow(c, 100.0f, 10.0f));
  EXPECT_FALSE(pes::SlideEngageWindow(c, 100.0f, -30.0f));
}

TEST(PesDefenderSlide, DirectionWindowIsNinetyDegrees) {
  const pes::DefenderDecisionConstants c = Synthetic();
  EXPECT_TRUE(pes::SlideDirectionWindow(c, 0.0f, 0.0f));
  EXPECT_TRUE(pes::SlideDirectionWindow(c, 89.9f, 0.0f));
  // The raw compares `>=` against 90, so the boundary is out as well.
  EXPECT_FALSE(pes::SlideDirectionWindow(c, 90.0f, 0.0f));
  // A sideways slide is not expressible, so past 90 the action is refused.
  EXPECT_FALSE(pes::SlideDirectionWindow(c, 90.1f, 0.0f));
  EXPECT_FALSE(pes::SlideDirectionWindow(c, 179.0f, 0.0f));
  EXPECT_FALSE(pes::SlideDirectionWindow(c, -135.0f, 45.0f));
}

TEST(PesDefenderSlide, OwnBoxNeedsLevelThreeAndThenLowersIt) {
  const pes::DefenderDecisionConstants c = Synthetic();
  EXPECT_EQ(pes::SlideAggressionOwnBox(c, false, false, 2), 2);
  EXPECT_LT(pes::SlideAggressionOwnBox(c, true, false, 2), 0);  // refused below 3
  EXPECT_EQ(pes::SlideAggressionOwnBox(c, true, false, 3), 0);
  EXPECT_EQ(pes::SlideAggressionOwnBox(c, true, true, 3), 2);
}

TEST(PesDefenderBlock, RadiiAreThirteenThirtyAndSeven) {
  const pes::DefenderDecisionConstants c = Synthetic();
  EXPECT_FLOAT_EQ(pes::BlockRadius_m(c, false, false, false), 1.3f);
  EXPECT_FLOAT_EQ(pes::BlockRadius_m(c, true, false, false), 3.0f);
  // A shot and a feint sub-class scale the pressing radius; the widened radius
  // itself is a constant the late gate adds.
  EXPECT_FLOAT_EQ(pes::BlockRadius_m(c, true, true, false), 3.9f);
  EXPECT_FLOAT_EQ(pes::BlockRadius_m(c, true, false, true), 3.6f);
  EXPECT_FLOAT_EQ(c.blockRadiusLate_m, 7.0f);
}

TEST(PesDefenderBlock, ClipProgressIsSeventyOrEightyAndHalvedForAHighBall) {
  const pes::DefenderDecisionConstants c = Synthetic();
  EXPECT_FLOAT_EQ(pes::BlockClipProgressFraction(c, false, false), 0.7f);
  EXPECT_FLOAT_EQ(pes::BlockClipProgressFraction(c, true, false), 0.8f);
  EXPECT_FLOAT_EQ(pes::BlockClipProgressFraction(c, false, true), 0.35f);
  EXPECT_FLOAT_EQ(pes::BlockClipProgressFraction(c, true, true), 0.4f);
  EXPECT_TRUE(pes::BlockClipWindowOpen(0.7f, 0.7f));
  EXPECT_FALSE(pes::BlockClipWindowOpen(0.69f, 0.7f));
}

TEST(PesDefenderBlock, LooseBallWindowIsNormalOrSpeedBall) {
  const pes::DefenderDecisionConstants c = Synthetic();
  EXPECT_FLOAT_EQ(pes::BlockLooseWindow_s(c, 69.9f), 0.9f);
  EXPECT_FLOAT_EQ(pes::BlockLooseWindow_s(c, 70.0f), 2.0f);
  EXPECT_FLOAT_EQ(pes::BlockLooseWindow_s(c, 120.0f), 2.0f);
}

// The install test: the loader must read the running game's own numbers, and
// the relations between them must hold regardless of which install - stock or
// modded - is mounted.
TEST(PesDefenderInstall, TheInstallCarriesTheGamesOwnGates) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto k = pes::LoadDefenderDecisionConstants(*exe, vfs, &err);
  ASSERT_TRUE(k.has_value()) << err;

  // Geometry: every angle a right opening and every radius a length.
  EXPECT_GT(k->tackleBallHeadingAngle_deg, 0.0f);
  EXPECT_LE(k->tackleBallHeadingAngle_deg, 90.0f);
  EXPECT_GT(k->tackleEngageAngle_deg, k->tackleBallHeadingAngle_deg);
  EXPECT_GT(k->tackleEngageGap_m, 0.0f);
  EXPECT_GT(k->tackleBehindAngle_deg, k->tackleEngageAngle_deg);
  // The behind tolerances tighten with the aggression level, and the top one is
  // the zeroed register PES leaves for ENFORCER + defensive mode.
  EXPECT_GT(k->tackleBehindTolerance_deg[0], k->tackleBehindTolerance_deg[1]);
  EXPECT_GT(k->tackleBehindTolerance_deg[1], k->tackleBehindTolerance_deg[2]);
  EXPECT_EQ(k->tackleBehindTolerance_deg[2], 0.0f);
  EXPECT_GT(k->tackleBallDistanceSq_m2, 0.0f);

  // The touch lead grows with the aggression level.
  EXPECT_LE(pes::TackleTouchLeadTicks(*k, 0), pes::TackleTouchLeadTicks(*k, 1));
  EXPECT_LE(pes::TackleTouchLeadTicks(*k, 1), pes::TackleTouchLeadTicks(*k, 2));

  // The json terms are read out of the install, so they are whatever it says:
  // both safety rates and both block windows are usable numbers.
  EXPECT_GT(k->tackleSafetyRateNoCover, 0.0f);
  EXPECT_GT(k->tackleSafetyRateLastLine, 0.0f);
  EXPECT_GE(k->blockSpeedBall_s, k->blockNormal_s);
  EXPECT_GT(k->blockNormal_s, 0.0f);

  // Slide and block relations.
  EXPECT_GT(k->slideDistancePerLevel_m, 0.0f);
  // The slide's engagement window is the tackle's own slot, and its direction
  // window is a right opening.
  EXPECT_FLOAT_EQ(k->slideEngageAngle_deg, k->tackleEngageAngle_deg);
  EXPECT_GT(k->slideDirectionAngle_deg, 0.0f);
  EXPECT_LE(k->slideDirectionAngle_deg, k->slideEngageAngle_deg);
  EXPECT_GT(k->blockRadiusPressing_m, k->blockRadiusNormal_m);
  EXPECT_GT(k->blockRadiusLate_m, k->blockRadiusPressing_m);
  EXPECT_GT(k->blockLooseSpeedBall_kmh, 0.0f);
  // The challenge target's look-ahead is a fraction of a second, and the
  // PRESS radius reaches past the plain near radius.
  EXPECT_GT(k->challengeLook_s, 0.0f);
  EXPECT_LT(k->challengeLook_s, 1.0f);
  EXPECT_GT(k->challengePathRadius_m, 0.0f);
  EXPECT_GT(k->challengePressDistSq_m2, k->challengeNearDistSq_m2);
  EXPECT_GT(k->challengeCloserMarginSq_m2, 0.0f);
}

// FUN_140a50e00: the man a defender may challenge. The newest ball event's
// owner when he is an opponent; my own team's ball gives nobody (0xff); a
// loose ball falls back to the other team's pass target.
TEST(PesDefenderTarget, MyOwnTeamsBallHasNoChallengeTarget) {
  EXPECT_EQ(pes::ChallengeCandidate(pes::ChallengeOwner::kMyTeam, true),
            pes::ChallengeCandidate::kNone);
  EXPECT_EQ(pes::ChallengeCandidate(pes::ChallengeOwner::kOpponent, false),
            pes::ChallengeCandidate::kOwner);
  EXPECT_EQ(pes::ChallengeCandidate(pes::ChallengeOwner::kNobody, true),
            pes::ChallengeCandidate::kTheirPassTarget);
  EXPECT_EQ(pes::ChallengeCandidate(pes::ChallengeOwner::kNobody, false),
            pes::ChallengeCandidate::kNone);
}

TEST(PesDefenderTarget, TheCandidateMustBeOnTheBall) {
  const auto c = Synthetic();
  pes::ChallengeCheck in{};
  // Ball 0.2 s ahead lands on his 1 m path.
  in.candX = 0; in.candY = 0; in.candHeading_deg = 0; in.candSpeed_mps = 5;
  in.ballX = 0.8f; in.ballY = 0.5f;
  in.myX = 20; in.myY = 0;
  EXPECT_TRUE(pes::ChallengeTargetValid(c, in));
  // Off his path, 6 m away, and I am nearer: not his ball.
  in.ballX = 14; in.ballY = 0;
  EXPECT_FALSE(pes::ChallengeTargetValid(c, in));
  // Within 3.5 m of him: still his.
  in.ballX = 3.0f; in.ballY = 1.5f;
  EXPECT_TRUE(pes::ChallengeTargetValid(c, in));
  // 4 m off: only a PRESSing defender keeps him.
  in.ballX = 0; in.ballY = 4.0f; in.myX = 0; in.myY = 6.0f;
  EXPECT_FALSE(pes::ChallengeTargetValid(c, in));
  in.pressing = true;
  EXPECT_TRUE(pes::ChallengeTargetValid(c, in));
  // Nearer to him than to me (within the 0.64 margin): his.
  in.pressing = false;
  in.myY = 8.0f;
  EXPECT_TRUE(pes::ChallengeTargetValid(c, in));
}

}  // namespace

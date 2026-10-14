// PES 2021's ball receive (docs/re/pes-re-receiving.md). Synthetic constants
// exercise the re-touch lockout ladder, the shot-through rule, the PASS_GET
// selector and its two-receiver arbitration, and the first-touch kind chooser
// (interception, planned 5 m touch, touch-line guard, turn clamp). One install
// test checks the loader against the user's own PES2021 install through
// relations, not pinned numbers.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <vector>

#include "onthepitch/pesreceive.hpp"

namespace {

// The shape the report gives for these fields (§3, §4, trap.json live).
pes::ReceiveConstants Synthetic() {
  pes::ReceiveConstants c{};
  c.lockDefence_s = 0.16f;
  c.lockDefenceKeep_s = 0.26f;
  c.lockOffence_s = 0.05f;
  c.lockShoot_s = 0.5f;
  c.lockShootSpeedBall_s = 0.75f;
  c.busyTime_s = 0.5f;
  c.checkMyBallPass = false;
  c.runTrapStopTurnAngle_deg = 90.0f;
  c.stopTrapStopTurnAngle_deg = -1.0f;
  c.turnClampUse = true;
  c.lockStatCoefficient_s = 0.1f;
  c.statFloor = 40.0f;
  c.statCeil = 99.0f;
  c.statSpan = 59.0f;
  c.highBallLock_m = 0.75f;
  c.shootLockSpeed_kmh = 80.0f;
  c.mpsToKmh = 3.6f;
  c.ownKickLockScale = 3.0f;
  c.shotThroughAngle_deg = 30.0f;
  c.shotThroughSpeed_kmh = 35.0f;
  c.interceptSpeedSq_m2s2 = 69.44f;
  c.interceptAgePercent = 50;
  c.plannedTouch_m = 5.0f;
  c.lineMargin_m = 1.0f;
  c.lineGuardBallAngle_deg = 25.0f;
  c.lineGuardBodyAngle_deg = 135.0f;
  c.lineGuardAhead_m = 1.5f;
  c.clampBallSpeedSq_m2s2 = 14.0625f;
  c.runningFacing_deg = 45.0f;
  c.runningSpeedSq = 2361.96f;
  c.arbitrationAngle_deg = 135.0f;
  c.arbitrationRadius_m = 3.0f;
  c.arbitrationLength_m = 5.0f;
  c.ballDistanceTableMaxSq_m2 = 100.0f;
  c.receivingPointMargin_m = 0.163f;
  return c;
}

constexpr float kFps = 54.0f;

pes::LockoutInput Loose(float stat99) {
  pes::LockoutInput in{};
  in.ballStat = stat99;
  return in;
}

// --- Re-touch lockout (report §3, FUN_1409fa720) ---

TEST(PesReceiveLockout, OwnedBallIsOffenceForTheOwnerAndDefenceKeepForEveryoneElse) {
  const auto c = Synthetic();
  pes::LockoutInput in{};
  in.ballOwned = true;
  in.ownerIsMe = true;
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.05f);
  in.ownerIsMe = false;
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.26f);
}

TEST(PesReceiveLockout, LooseBallIsDefencePlusTheBallControlTerm) {
  const auto c = Synthetic();
  // s = 0 below the floor: the whole 0.1 s.
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, Loose(30.0f)), 0.26f);
  // s = 1 at the ceiling: none of it.
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, Loose(99.0f)), 0.16f);
  // Half way: 1 - s^2 = 0.75.
  EXPECT_NEAR(pes::RetouchLockout_s(c, Loose(69.5f)), 0.16f + 0.075f, 1e-5f);
}

TEST(PesReceiveLockout, MyTeamsPassKeepsTheOffenceWindow) {
  const auto c = Synthetic();
  auto in = Loose(40.0f);
  in.eventKind = pes::BallEventKind::kPass;
  in.eventByMyTeam = true;
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.05f);
  in.eventByMyTeam = false;
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.26f);
}

TEST(PesReceiveLockout, AShotOrAHighBallTakesTheShootWindowBySpeed) {
  const auto c = Synthetic();
  auto in = Loose(99.0f);
  in.eventKind = pes::BallEventKind::kShot;
  in.ballSpeed_mps = 80.0f / 3.6f;  // exactly 80 km/h: still the slow window
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.5f);
  in.ballSpeed_mps = 81.0f / 3.6f;
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.75f);
  in.eventKind = pes::BallEventKind::kHighPass;
  in.ballHeight_m = 0.7f;  // not high enough: the loose window
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.16f);
  in.ballHeight_m = 0.8f;
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.75f);
}

TEST(PesReceiveLockout, MyOwnKickTriplesAndMyOwnRecentTouchDoubles) {
  const auto c = Synthetic();
  auto in = Loose(99.0f);
  in.eventKind = pes::BallEventKind::kPass;
  in.eventByMe = true;
  in.eventByMyTeam = true;
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.15f);
  in = Loose(99.0f);
  in.previousEventByMe = true;
  in.previousEventAge_s = 0.3f;  // inside 2L = 0.32
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.32f);
  in.previousEventAge_s = 0.33f;
  EXPECT_FLOAT_EQ(pes::RetouchLockout_s(c, in), 0.16f);
}

TEST(PesReceiveLockout, TheGateCountsWholeFrames) {
  const auto c = Synthetic();
  // int(54 * 0.16) = 8 frames.
  EXPECT_FALSE(pes::RetouchAllowed(c, Loose(99.0f), 7.0f / kFps, kFps));
  EXPECT_TRUE(pes::RetouchAllowed(c, Loose(99.0f), 8.0f / kFps, kFps));
}

TEST(PesReceiveShotThrough, AFastShotAtTheGoalIsLetThrough) {
  const auto c = Synthetic();
  pes::ShotThroughInput in{};
  in.ownTeamShot = true;
  in.ballX = 0.0f;
  in.ballY = 0.0f;
  in.goalX = 50.0f;
  in.goalHalfWidth = 3.66f;
  in.ballVelX = 20.0f;
  in.ballVelY = 0.0f;
  EXPECT_TRUE(pes::ShotRunsThrough(c, in));
  // 30 deg plus half the mouth (~4.2 deg) off still passes; 40 deg does not.
  in.ballVelX = 20.0f * std::cos(0.55f);
  in.ballVelY = 20.0f * std::sin(0.55f);
  EXPECT_TRUE(pes::ShotRunsThrough(c, in));
  in.ballVelX = 20.0f * std::cos(0.70f);
  in.ballVelY = 20.0f * std::sin(0.70f);
  EXPECT_FALSE(pes::ShotRunsThrough(c, in));
  // Slower than 35 km/h: trapped.
  in.ballVelX = 9.0f;
  in.ballVelY = 0.0f;
  EXPECT_FALSE(pes::ShotRunsThrough(c, in));
  // The opponents' shot is not let through.
  in.ballVelX = 20.0f;
  in.ownTeamShot = false;
  EXPECT_FALSE(pes::ShotRunsThrough(c, in));
}

// --- PASS_GET (report §2, FUN_140931c60) ---

TEST(PesReceivePassGet, TheTeamsPassTargetGoesToTheBall) {
  const auto c = Synthetic();
  pes::PassGetInput in{};
  in.iAmPassTarget = true;
  EXPECT_TRUE(pes::PassGetSelected(c, in));
  in.newestEventIsMyKick = true;
  EXPECT_FALSE(pes::PassGetSelected(c, in));
  in.newestEventIsMyKick = false;
  in.inKickBeforeContact = true;
  EXPECT_FALSE(pes::PassGetSelected(c, in));
  in.inKickBeforeContact = false;
  in.mateNearBallOwnsIt = true;
  EXPECT_FALSE(pes::PassGetSelected(c, in));
  in.mateNearBallOwnsIt = false;
  in.iAmPassTarget = false;
  EXPECT_FALSE(pes::PassGetSelected(c, in));
}

// FUN_140931c60's arbitration (raw 0x140931d98..0x140931e6b): when the other
// team's (stored) pass target faces the oncoming ball and I stand inside the
// 3 m x 5 m capsule ahead of him, I take the ball without the team-mate scan.
TEST(PesReceivePassGet, ArbitrationCapsuleRunsFromTheirTargetToMe) {
  const auto c = Synthetic();
  pes::ArbitrationInput in{};
  in.otherTargetValid = true;
  in.otherX = 0.0f;
  in.otherY = 0.0f;
  in.otherHeading_deg = 180.0f;  // he faces -x, the ball travels +x
  in.ballHeading_deg = 0.0f;
  in.myX = -4.0f;  // 4 m in front of him
  in.myY = 1.0f;
  EXPECT_TRUE(pes::ArbitrationWins(c, in));
  in.myY = 3.5f;  // outside the 3 m radius
  EXPECT_FALSE(pes::ArbitrationWins(c, in));
  in.myY = 1.0f;
  in.myX = -9.0f;  // beyond length + radius
  EXPECT_FALSE(pes::ArbitrationWins(c, in));
  in.myX = 4.0f;  // behind him
  EXPECT_FALSE(pes::ArbitrationWins(c, in));
  in.myX = -4.0f;
  in.ballHeading_deg = 90.0f;  // only 90 deg off his facing
  EXPECT_FALSE(pes::ArbitrationWins(c, in));
  in.ballHeading_deg = 0.0f;
  in.otherTargetValid = false;
  EXPECT_FALSE(pes::ArbitrationWins(c, in));
}

TEST(PesReceivePassGet, ArbitrationSkipsTheTeamMateScan) {
  const auto c = Synthetic();
  pes::PassGetInput in{};
  in.iAmPassTarget = true;
  in.mateNearBallOwnsIt = true;
  EXPECT_FALSE(pes::PassGetSelected(c, in));
  in.arbitrationWins = true;
  EXPECT_TRUE(pes::PassGetSelected(c, in));
  in.newestEventIsMyKick = true;  // the first gates still apply
  EXPECT_FALSE(pes::PassGetSelected(c, in));
}

// --- First touch (report §4, FUN_1409f8a50 / 1409fa0b0 / 1409fbce0) ---

pes::FirstTouchInput OwnPass() {
  pes::FirstTouchInput in{};
  in.hasPlannedDirection = true;
  in.plannedHeading_deg = 20.0f;
  in.bodyHeading_deg = 0.0f;
  in.eventByMyTeam = true;
  in.eventIsKick = true;
  in.eventAge_s = 1.0f;
  in.ballSpeedSq_m2s2 = 100.0f;
  in.ballHeading_deg = 180.0f;
  in.posX = 0.0f;
  in.posY = 0.0f;
  in.halfLength = 52.5f;
  in.halfWidth = 34.0f;
  return in;
}

TEST(PesReceiveFirstTouch, OwnTeamsPassWithAPlanIsTheFiveMetreTouch) {
  const auto c = Synthetic();
  auto out = pes::ChooseFirstTouch(c, OwnPass(), kFps);
  EXPECT_EQ(out.kind, pes::TouchKind::kDirectional);
  EXPECT_FLOAT_EQ(out.heading_deg, 20.0f);
  EXPECT_FLOAT_EQ(out.distance_m, 5.0f);
}

TEST(PesReceiveFirstTouch, NoPlanIsControlAlongTheBody) {
  const auto c = Synthetic();
  auto in = OwnPass();
  in.hasPlannedDirection = false;
  auto out = pes::ChooseFirstTouch(c, in, kFps);
  EXPECT_EQ(out.kind, pes::TouchKind::kControl);
  EXPECT_FLOAT_EQ(out.heading_deg, 0.0f);
}

TEST(PesReceiveFirstTouch, AFreshFastOpponentBallIsAPlainTrap) {
  const auto c = Synthetic();
  auto in = OwnPass();
  in.eventByMyTeam = false;
  in.eventAge_s = 26.0f / kFps;  // under int(54 * 50 / 100) = 27 frames
  in.ballSpeedSq_m2s2 = 70.0f;
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kControl);
  // Slow enough: the interception rule does not fire, but the turn clamp
  // still stops a stationary receiver turning.
  in.ballSpeedSq_m2s2 = 60.0f;
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kControl);
}

TEST(PesReceiveFirstTouch, TurnClampLetsARunnerTurnNinetyAndAStandingManNone) {
  const auto c = Synthetic();
  auto in = OwnPass();
  in.eventByMyTeam = false;
  in.eventAge_s = 0.4f;  // inside busyTime, past the interception age
  in.ballSpeedSq_m2s2 = 20.0f;
  // Standing: -1 deg allowance, any turn forces control.
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kControl);
  // Running along his facing: 20 deg is inside 90.
  in.moveHeading_deg = 10.0f;
  in.moveSpeed_mps = 1.0f;  // (1 * 54)^2 = 2916 > 2361.96
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kDirectional);
  in.plannedHeading_deg = 100.0f;
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kControl);
  // Past busyTime the clamp is off.
  in.eventAge_s = 0.6f;
  in.moveSpeed_mps = 0.0f;
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kDirectional);
}

TEST(PesReceiveFirstTouch, MyTeamsKickSkipsTheClampUnlessCheckMyBallPass) {
  auto c = Synthetic();
  auto in = OwnPass();
  in.eventAge_s = 0.1f;
  in.ballSpeedSq_m2s2 = 20.0f;
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kDirectional);
  c.checkMyBallPass = true;
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kControl);
  c.turnClampUse = false;
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kDirectional);
}

TEST(PesReceiveFirstTouch, TouchLineGuardStopsATurnIntoTheLine) {
  const auto c = Synthetic();
  auto in = OwnPass();
  // Near the +y touch line, ball travelling +y, body facing -y, plan +y.
  in.posY = 33.0f;
  in.ballHeading_deg = 90.0f;
  in.bodyHeading_deg = -90.0f;
  in.plannedHeading_deg = 90.0f;
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kControl);
  in.posY = 30.0f;  // 1.5 m ahead is 31.5: clear of the margin
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kDirectional);
}

TEST(PesReceiveFirstTouch, APressingOrDelayingDefenderJustControlsIt) {
  const auto c = Synthetic();
  auto in = OwnPass();
  in.pressOrDelay = true;
  EXPECT_EQ(pes::ChooseFirstTouch(c, OwnPass(), kFps).kind, pes::TouchKind::kDirectional);
  EXPECT_EQ(pes::ChooseFirstTouch(c, in, kFps).kind, pes::TouchKind::kControl);
}

// FUN_1409fa400: only a player within the ball-distance table's limit
// (MA+0x24710, squared metres, <= 100) may receive.
TEST(PesReceiveGate, BallDistanceTableLimitIsTenMetres) {
  auto c = Synthetic();
  EXPECT_TRUE(pes::WithinBallDistanceTable(c, 100.0f));
  EXPECT_FALSE(pes::WithinBallDistanceTable(c, 100.1f));
}

// FUN_140a4d340 + FUN_14094e3a0: the receiving point is the ball path's
// nearest point to the receiver, pulled back onto the pitch along the line
// from him when it lies more than the margin outside.
TEST(PesReceivePassGet, ReceivingPointIsTheNearestPathPointClampedIn) {
  const auto c = Synthetic();
  std::vector<pes::Point2> path = {{0, 0}, {5, 0}, {10, 0}, {15, 0}};
  auto p = pes::ReceivingPoint(c, path, {9, 3}, 52.5f, 34.0f);
  EXPECT_FLOAT_EQ(p.x, 10.0f);
  EXPECT_FLOAT_EQ(p.y, 0.0f);
  // A path running out over the touch line: the point is projected onto it.
  path = {{0, 30}, {0, 35}, {0, 40}};
  p = pes::ReceivingPoint(c, path, {2, 38}, 52.5f, 34.0f);
  EXPECT_FLOAT_EQ(p.y, 34.0f);
  // Along the line from him (2, 38) through (0, 40), back to y = 34.
  EXPECT_NEAR(p.x, 6.0f, 1e-4f);
  // Inside the 0.163 m margin: left alone.
  path = {{0, 34.1f}};
  p = pes::ReceivingPoint(c, path, {0, 30}, 52.5f, 34.0f);
  EXPECT_FLOAT_EQ(p.y, 34.1f);
}

TEST(PesReceiveInstall, TheInstallCarriesTheGamesOwnReceive) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe.has_value()) << err;
  pes::PesVfs vfs;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto k = pes::LoadReceiveConstants(*exe, vfs, &err);
  ASSERT_TRUE(k.has_value()) << err;

  // The lockout ladder: a team-mate's ball is the shortest wait, a held ball
  // longer than a loose one, a shot the longest and a fast shot longer still.
  EXPECT_GT(k->lockOffence_s, 0.0f);
  EXPECT_LT(k->lockOffence_s, k->lockDefence_s);
  EXPECT_LT(k->lockDefence_s, k->lockDefenceKeep_s);
  EXPECT_LT(k->lockDefenceKeep_s, k->lockShoot_s);
  EXPECT_LT(k->lockShoot_s, k->lockShootSpeedBall_s);
  EXPECT_GT(k->busyTime_s, 0.0f);
  // A running receiver may turn further than a standing one.
  EXPECT_GT(k->runTrapStopTurnAngle_deg, k->stopTrapStopTurnAngle_deg);
  // The stat clamp ladder: floor + span = ceiling.
  EXPECT_FLOAT_EQ(k->statFloor + k->statSpan, k->statCeil);
  EXPECT_GT(k->ownKickLockScale, 1.0f);
  EXPECT_GT(k->interceptSpeedSq_m2s2, k->clampBallSpeedSq_m2s2);
  EXPECT_GT(k->interceptAgePercent, 0);
  EXPECT_LT(k->interceptAgePercent, 100);
  EXPECT_GT(k->plannedTouch_m, k->lineGuardAhead_m);
  EXPECT_GT(k->lineGuardAhead_m, k->lineMargin_m);
  EXPECT_LT(k->lineGuardBallAngle_deg, k->runningFacing_deg);
  EXPECT_GT(k->lineGuardBodyAngle_deg, 90.0f);
  EXPECT_GT(k->shootLockSpeed_kmh, k->shotThroughSpeed_kmh);
  // The ball-distance table reaches further than the arbitration capsule, and
  // the receiving point's margin is a ball's width, not metres.
  EXPECT_GT(k->ballDistanceTableMaxSq_m2, k->arbitrationLength_m * k->arbitrationLength_m);
  EXPECT_GT(k->receivingPointMargin_m, 0.0f);
  EXPECT_LT(k->receivingPointMargin_m, 1.0f);
}

}  // namespace

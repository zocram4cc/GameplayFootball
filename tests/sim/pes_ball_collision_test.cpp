// PES 2021's ball vs goal frame (docs/re/pes-re-ball-collision.md 3 and 5).
// Synthetic constants pin each rule; one install test replays a live crossbar
// hit with the user's own exe constants.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <string>

#include "onthepitch/pesball.hpp"
#include "onthepitch/pesballcollision.hpp"
#include "pes/exe.hpp"

namespace {

using pes::Vec3;

constexpr float BALL_RADIUS_M = 0.1f;
constexpr float SPIN_SCALE = 500.0f;

pes::PesBallConstants Synthetic() {
  pes::PesBallConstants c;
  auto& k = c.image;
  k.gravity = 10.0f;
  k.ballRadius = BALL_RADIUS_M;
  k.kmhPerMs = 3.6f;
  k.tickHz = 50.0f;
  k.spinScale = SPIN_SCALE;
  k.invSpinScale = 1.0f / SPIN_SCALE;
  k.staticRestitution = 0.5f;
  k.staticRetention = 0.8f;
  k.frameTilt = 0.1f;
  k.degreesPerRadian = 57.2957795f;
  k.pushGain = 1.25f;
  k.recentHitDecayPerS = 4.0f;
  k.speedUpGateSq = 49.0f;
  k.speedCap_mps = 20.0f;
  k.verticalCap_mps = 12.0f;
  k.spinBlendBase = 0.2f;
  k.spinBlendSpan = 0.5f;
  k.spinBlendFloor_kmh = 3.0f;
  k.spinBlendRampPerKmh = 0.2f;
  k.spinCap_revPerS = 6.0f;
  k.snapSpeedChangeSq_kmh2 = 100.0f;
  return c;
}

constexpr pesball::GoalFrame FRAME{50.0f, 3.5f, 2.5f, 0.2f, 0.2f};
// The near post's axis: (50 - 0.1, 3.5 + 0.1), radius 0.1 + r.
constexpr float POST_X = 49.9f, POST_Z = 3.6f, HIT_RADIUS = 0.2f;

Vec3 Sub(Vec3 a, Vec3 b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }

}  // namespace

TEST(PesBallCollision, HeadOnPostHitReflectsWithTheStaticRestitution) {
  const auto c = Synthetic();
  const Vec3 v(10.0f, 0.0f, 0.0f);
  const Vec3 p1(POST_X - 0.15f, 1.0f, POST_Z);
  const auto hit = pesball::GoalFrameHit(FRAME, BALL_RADIUS_M, Sub(p1, v * 0.02f), p1, v);
  ASSERT_TRUE(hit);
  EXPECT_NEAR(hit->normal.x, -1.0f, 1e-6f);
  EXPECT_NEAR(hit->surface.x, POST_X - 0.1f, 1e-5f);
  pesball::BallState s{p1, v, Vec3()};
  pesball::FrameContact(c, *hit, 0.0f, &s);
  EXPECT_NEAR(s.velocity.x, -5.0f, 1e-4f);
  EXPECT_NEAR(s.position.x, POST_X - HIT_RADIUS, 1e-5f);  // snapped to the surface
}

TEST(PesBallCollision, ARecentHitMakesTheNextOneElasticAndPullsItDown) {
  const auto c = Synthetic();
  // Below the speed-up gate (7 m/s), so the elastic bounce is not rescaled.
  const Vec3 v(5.0f, 0.0f, 0.0f);
  const Vec3 p1(POST_X - 0.15f, 1.0f, POST_Z);
  const auto hit = pesball::GoalFrameHit(FRAME, BALL_RADIUS_M, Sub(p1, v * 0.04f), p1, v);
  ASSERT_TRUE(hit);
  pesball::BallState s{p1, v, Vec3()};
  pesball::FrameContact(c, *hit, 1.0f, &s);
  EXPECT_NEAR(s.velocity.x, -5.0f, 1e-4f);
  EXPECT_NEAR(s.velocity.y, -10.0f / 50.0f, 1e-5f);  // g/tickHz * b
}

TEST(PesBallCollision, AboveTheGateASpedUpBounceKeepsTheIncomingSpeed) {
  const auto c = Synthetic();
  const Vec3 v(10.0f, 0.0f, 0.0f);
  const Vec3 p1(POST_X - 0.15f, 1.0f, POST_Z);
  const auto hit = pesball::GoalFrameHit(FRAME, BALL_RADIUS_M, Sub(p1, v * 0.02f), p1, v);
  ASSERT_TRUE(hit);
  pesball::BallState s{p1, v, Vec3()};
  pesball::FrameContact(c, *hit, 1.0f, &s);
  EXPECT_NEAR(std::sqrt(s.velocity.x * s.velocity.x + s.velocity.y * s.velocity.y), 10.0f, 1e-4f);
}

TEST(PesBallCollision, MissesAboveThePostAndAFallingBallMissesTheBar) {
  const Vec3 v(10.0f, 0.0f, 0.0f);
  const Vec3 high(POST_X - 0.15f, 3.0f, POST_Z);
  EXPECT_FALSE(pesball::GoalFrameHit(FRAME, BALL_RADIUS_M, Sub(high, v * 0.02f), high, v));
  // PES's crossbar test packs (x, y, 0) but its `a` reads x and z: a ball
  // falling straight onto the bar has a = 0 and passes through (report 3.2).
  const Vec3 down(0.0f, -5.0f, 0.0f);
  const Vec3 onBar(49.9f, 2.75f, 0.0f);  // outside the bar's reach at the tick's start
  EXPECT_FALSE(pesball::GoalFrameHit(FRAME, BALL_RADIUS_M, Sub(onBar, down * 0.02f), onBar, down));
}

TEST(PesBallCollision, TheSnapNeverPutsTheBallIntoTheGround) {
  const auto c = Synthetic();
  const Vec3 v(10.0f, 0.0f, 0.0f);
  const Vec3 low(POST_X - 0.15f, 0.05f, POST_Z);
  const auto hit = pesball::GoalFrameHit(FRAME, BALL_RADIUS_M, Sub(low, v * 0.02f), low, v);
  ASSERT_TRUE(hit);
  pesball::BallState s{low, v, Vec3()};
  pesball::FrameContact(c, *hit, 0.0f, &s);
  EXPECT_FLOAT_EQ(s.position.y, BALL_RADIUS_M);
}

TEST(PesBallCollision, CrossbarHitPointsTheNormalAwayFromTheBarAxis) {
  const Vec3 v(10.0f, 0.0f, 0.0f);
  const Vec3 p1(49.75f, 2.6f, 1.0f);
  const auto hit = pesball::GoalFrameHit(FRAME, BALL_RADIUS_M, Sub(p1, v * 0.02f), p1, v);
  ASSERT_TRUE(hit);
  EXPECT_LT(hit->normal.x, 0.0f);
  EXPECT_NEAR(hit->normal.z, 0.0f, 1e-6f);
  EXPECT_NEAR(hit->surface.z, 1.0f, 1e-5f);
}

TEST(PesBallCollision, GlancingHitTiltsTheNormalTowardsTheTangent) {
  const auto c = Synthetic();
  // Grazing the post on its pitch side, moving along the goal line.
  const Vec3 v(2.0f, 0.0f, 10.0f);
  const Vec3 p1(POST_X - 0.15f, 1.0f, POST_Z - 0.12f);
  const auto hit = pesball::GoalFrameHit(FRAME, BALL_RADIUS_M, Sub(p1, v * 0.02f), p1, v);
  ASSERT_TRUE(hit);
  pesball::BallState untilted{p1, v, Vec3()}, tilted{p1, v, Vec3()};
  auto flat = c;
  flat.image.frameTilt = 0.0f;
  pesball::FrameContact(flat, *hit, 0.0f, &untilted);
  pesball::FrameContact(c, *hit, 0.0f, &tilted);
  EXPECT_GT(std::fabs(tilted.velocity.x - untilted.velocity.x), 1e-3f);
}

TEST(PesBallCollision, SpeedAndVerticalCapsBound) {
  auto c = Synthetic();
  c.image.staticRestitution = 1.0f;
  c.image.staticRetention = 1.0f;
  const Vec3 v(25.0f, 0.0f, 0.0f);
  const Vec3 p1(POST_X - 0.15f, 1.0f, POST_Z);
  const auto hit = pesball::GoalFrameHit(FRAME, BALL_RADIUS_M, Sub(p1, v * 0.01f), p1, v);
  ASSERT_TRUE(hit);
  pesball::BallState s{p1, v, Vec3()};
  pesball::FrameContact(c, *hit, 0.0f, &s);
  EXPECT_NEAR(std::sqrt(s.velocity.x * s.velocity.x + s.velocity.y * s.velocity.y +
                        s.velocity.z * s.velocity.z),
              20.0f, 1e-3f);

  pesball::BallState up{p1, Vec3(5.0f, 15.0f, 0.0f), Vec3()};
  pesball::FrameHit side{Vec3(POST_X - 0.1f, 1.0f, POST_Z), Vec3(-1.0f, 0.0f, 0.0f)};
  pesball::FrameContact(c, side, 0.0f, &up);
  EXPECT_NEAR(std::fabs(up.velocity.y), 12.0f, 1e-3f);
}

TEST(PesBallCollision, SpinIsCappedInRevolutionsPerSecond) {
  const auto c = Synthetic();
  const Vec3 v(10.0f, 0.0f, 0.0f);
  const Vec3 p1(POST_X - 0.15f, 1.0f, POST_Z);
  const auto hit = pesball::GoalFrameHit(FRAME, BALL_RADIUS_M, Sub(p1, v * 0.02f), p1, v);
  ASSERT_TRUE(hit);
  pesball::BallState s{p1, v, Vec3(0.0f, 200.0f / SPIN_SCALE, 0.0f)};
  pesball::FrameContact(c, *hit, 0.0f, &s);
  const float spin = std::sqrt(s.spinRaw.x * s.spinRaw.x + s.spinRaw.y * s.spinRaw.y +
                               s.spinRaw.z * s.spinRaw.z) *
                     SPIN_SCALE;
  EXPECT_NEAR(spin, 6.0f * 2.0f * 3.14159265f, 1e-2f);
}

TEST(PesBallCollision, RecentHitIsSetBySlowingAndDecaysPerSecond) {
  const auto c = Synthetic();
  EXPECT_FLOAT_EQ(pesball::RecentHitAfter(c, 0.0f, 100.0f, 25.0f, 0.01f), 1.0f);
  EXPECT_FLOAT_EQ(pesball::RecentHitAfter(c, 1.0f, 100.0f, 100.0f, 0.01f), 0.96f);
  EXPECT_FLOAT_EQ(pesball::RecentHitAfter(c, 0.02f, 100.0f, 100.0f, 0.01f), 0.0f);
}

TEST(PesBallCollisionInstall, ReplaysTheLiveCrossbarHit) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto image = pes::LoadBallImageConstants(*exe, &err);
  ASSERT_TRUE(image) << err;
  pes::PesBallConstants c;
  c.image = *image;
  // PES's live FieldInfo and event B of docs/re/pes-re-ball-collision.md 7.2.
  constexpr pesball::GoalFrame PES_FRAME{52.56f, 3.66f, 2.44f, 0.12f, 0.12f};
  const Vec3 p1(52.440f, 2.420f, 2.338f), v(11.208f, -1.804f, 6.340f);
  const Vec3 p0 = Sub(p1, v * (1.0f / c.image.tickHz));
  const auto hit = pesball::GoalFrameHit(PES_FRAME, c.image.ballRadius, p0, p1, v);
  ASSERT_TRUE(hit);
  pesball::BallState s{p1, v, Vec3(-5.72f, -40.50f, 4.06f) * c.image.invSpinScale};
  pesball::FrameContact(c, *hit, 0.0f, &s);
  constexpr float POSITION_TOLERANCE_M = 0.002f;     // logged to 1 mm
  constexpr float VELOCITY_TOLERANCE_MPS = 0.1f;      // report: frame hits within 0.09
  constexpr float SPIN_TOLERANCE_RADPS = 0.15f;       // logged to 0.01, through the scale
  EXPECT_NEAR(s.position.x, 52.345f, POSITION_TOLERANCE_M);
  EXPECT_NEAR(s.position.y, 2.433f, POSITION_TOLERANCE_M);
  EXPECT_NEAR(s.position.z, 2.286f, POSITION_TOLERANCE_M);
  EXPECT_NEAR(s.velocity.x, -2.676f, VELOCITY_TOLERANCE_MPS);
  EXPECT_NEAR(s.velocity.y, -7.311f, VELOCITY_TOLERANCE_MPS);
  EXPECT_NEAR(s.velocity.z, 6.105f, VELOCITY_TOLERANCE_MPS);
  const Vec3 w = s.spinRaw * c.image.spinScale;
  EXPECT_NEAR(w.x, -8.91f, SPIN_TOLERANCE_RADPS);
  EXPECT_NEAR(w.y, 12.83f, SPIN_TOLERANCE_RADPS);
  EXPECT_NEAR(w.z, 20.27f, SPIN_TOLERANCE_RADPS);
}

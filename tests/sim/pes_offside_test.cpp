// PES 2021's offside detection (docs/re/pes-re-offside.md): the line, the
// attacker test, the four categories and the staged-restart gate. Synthetic
// constants exercise the arithmetic; one install test checks the loader against
// the user's own PES2021.exe through relations, not pinned numbers.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "gametypes.hpp"
#include "onthepitch/offsiderule.hpp"
#include "onthepitch/pesjudge.hpp"
#include "pes/exe.hpp"

namespace {

using blunted::Vector3;

// Distinct values, so a constant used in the wrong place moves a line or a
// category. The torso margin is both the leading shift and the tolerance, as
// in PES, where one .rdata float serves both.
pes::JudgeConstants Synthetic() {
  pes::JudgeConstants k{};
  k.offsideMarginTorso_m = 0.30f;
  k.offsideMarginArm_m = 0.07f;
  k.offsideMarginToe_m = 0.15f;
  k.offsideArmTolerance_m = 0.60f;
  k.offsideRearShiftTorso_m = -0.20f;
  k.offsideRearShiftArm_m = -0.05f;
  k.offsideRearShiftToe_m = -0.12f;
  k.offsideNearBand_m = 1.5f;
  k.offsideClearBand_m = 5.0f;
  return k;
}

// The goal line's distance from the centre, PES's `pitch+0`.
constexpr float HALF_M = 50.0f;

Vector3 At(float x) { return Vector3(x, 0.0f, 0.0f); }

float Line(int defendingSide, const std::vector<Vector3>& defenders, float ballX) {
  return OffsideRule::OffsideLineX(defendingSide, defenders, ballX, HALF_M, Synthetic());
}

// The category of an attacker of `side` whose centre is `at`, judged against
// `lineX`.
int Category(float at, float lineX, int side) {
  return OffsideRule::OffsideCategory(at, lineX, side, Synthetic());
}

}  // namespace

TEST(PesOffsideLine, SecondRearmostDefenderSetsTheLine) {
  // The defending team attacks +x, so its own goal is at -HALF and its own half
  // is x < 0: a normal line is deep in it. The rearmost defender is the one at
  // -20, so the line is the other one's rear surface: -10 - 0.2.
  EXPECT_FLOAT_EQ(Line(1, {At(-20), At(-10)}, 0), -10.0f - 0.20f);
}

TEST(PesOffsideLine, TheBallCapsTheLine) {
  // Level with the ball is never offside: a ball deeper than the line drags it
  // back, and a ball ahead of the line does not deepen it.
  EXPECT_FLOAT_EQ(Line(1, {At(-20), At(-10)}, -15), -15.0f);
  EXPECT_FLOAT_EQ(Line(1, {At(-20), At(-10)}, 5), -10.0f - 0.20f);
  EXPECT_FLOAT_EQ(Line(1, {At(-20), At(-10)}, -30), -30.0f);
}

TEST(PesOffsideLine, HalfwayCapsTheLine) {
  // A defender beyond halfway never deepens the line: with the second-rearmost
  // of these beyond it, the running minima never leave 0.
  EXPECT_FLOAT_EQ(Line(1, {At(10), At(12)}, 8), 0.0f);
  EXPECT_FLOAT_EQ(Line(1, {At(-5), At(10)}, 0), 0.0f);
  // The ball still caps it from this engine's own half.
  EXPECT_FLOAT_EQ(Line(1, {At(10), At(12)}, -8), -8.0f);
}

TEST(PesOffsideLine, TheKeeperCountsLikeAnyoneElse) {
  // The keeper is simply the rearmost defender: the line is the second of the
  // outfielders, the one at -30.
  EXPECT_FLOAT_EQ(Line(1, {At(-49), At(-30), At(-28)}, 0), -30.0f - 0.20f);
}

TEST(PesOffsideLine, FewerThanTwoDefendersIsTheGoalLine) {
  EXPECT_FLOAT_EQ(Line(1, {At(30)}, 20), -HALF_M);
  EXPECT_FLOAT_EQ(Line(1, {}, 20), -HALF_M);
}

TEST(PesOffsideLine, ClampsToTheGoalLine) {
  // A ball out over the goal line, and a defender behind his own line, both
  // land on the goal line rather than beyond it.
  EXPECT_FLOAT_EQ(Line(1, {At(10), At(5)}, -60), -HALF_M);
  EXPECT_FLOAT_EQ(Line(1, {At(-55), At(-50.5)}, 0), -HALF_M);
}

TEST(PesOffsideLine, WorksInTheDefendingTeamsOwnDirection) {
  // The defending team attacks -x: its own goal is at +HALF and its own half is
  // x > 0, so a normal line sits at positive x and everything is mirrored.
  EXPECT_FLOAT_EQ(Line(-1, {At(20), At(10)}, 0), 10.0f + 0.20f);
  // No attack direction at all means no line.
  EXPECT_FLOAT_EQ(Line(0, {At(-20), At(-10)}, 0), 0.0f);
}

TEST(PesOffsideBeyond, TheLeadingSurfaceMustPassTheLinePlusTheTolerance) {
  // The torso point's surface must pass the line by more than the tolerance,
  // and with the margin and the tolerance the same size the attacker's centre
  // is that edge: exactly level is onside (category 3, near level), a hair
  // past it is beyond.
  const float L = 20.0f;
  EXPECT_EQ(Category(L - 0.01f, L, 1), 3);
  EXPECT_EQ(Category(L, L, 1), 3);
  EXPECT_EQ(Category(L + 0.01f, L, 1), 2);
  // The surface leads the centre, so a centre still short of the line by less
  // than the margin is beyond anyway.
  EXPECT_EQ(Category(L + 0.29f, L, 1), 2);
  EXPECT_EQ(Category(L + 0.31f, L, 1), 2);
  // Mirrored for the team attacking -x.
  EXPECT_EQ(Category(L + 0.01f, L, -1), 3);
  EXPECT_EQ(Category(L - 0.01f, L, -1), 2);
}

TEST(PesOffsideBeyond, ExactlyAtTheEdgeIsNeverBeyond) {
  // The strictness of `>`: a leading surface exactly a tolerance past the line
  // has not passed it.
  const auto k = Synthetic();
  const float L = 7.0f;
  const float centre = L;  // its surface is exactly L + tolerance
  EXPECT_EQ(OffsideRule::OffsideCategory(centre, L, 1, k), 3);
  EXPECT_EQ(OffsideRule::OffsideCategory(centre - 0.001f, L, 1, k), 3);
  EXPECT_EQ(OffsideRule::OffsideCategory(centre + 0.001f, L, 1, k), 2);
}

TEST(PesOffsideCategories, TheFourBands) {
  const float L = 0.0f;
  // Beyond, measured at the leading point's centre.
  EXPECT_EQ(Category(L + 1.49f, L, 1), 2);  // d < the near band
  EXPECT_EQ(Category(L + 1.50f, L, 1), 1);  // d at the near band
  EXPECT_EQ(Category(L + 4.99f, L, 1), 1);
  EXPECT_EQ(Category(L + 5.00f, L, 1), 1);  // d at the clear band is still 1
  EXPECT_EQ(Category(L + 5.01f, L, 1), 0);
  // Onside: near level is category 3, further back no bit at all.
  EXPECT_EQ(Category(L - 1.49f, L, 1), 3);
  EXPECT_EQ(Category(L - 1.50f, L, 1), -1);
  // Mirrored: for the team attacking -x, beyond is the other way.
  EXPECT_EQ(Category(L - 5.01f, L, -1), 0);
  EXPECT_EQ(Category(L + 1.49f, L, -1), 3);
}

TEST(OffsideGate, NoEvaluationWhileARestartIsStaged) {
  // The delivery of a restart that can never arm a flag: kick-off, throw-in,
  // goal kick, corner.
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_KickOff));
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_ThrowIn));
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_GoalKick));
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_Corner));
  // A free kick or a penalty may put its receivers offside, and with nothing
  // staged every touch is judged.
  EXPECT_TRUE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_FreeKick));
  EXPECT_TRUE(OffsideRule::ShouldSnapshot(true, true, e_SetPiece_Penalty));
  EXPECT_TRUE(OffsideRule::ShouldSnapshot(true, false, e_SetPiece_None));
  // Out of play nothing is recorded at all.
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(false, false, e_SetPiece_None));
  EXPECT_FALSE(OffsideRule::ShouldSnapshot(false, true, e_SetPiece_FreeKick));
}

TEST(PesOffsideInstall, LoadsFromTheExe) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  std::string err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto k = pes::LoadJudgeConstants(*exe, &err);
  ASSERT_TRUE(k) << err;
  // The categories' bands: 1 m near, 4 m clear.
  EXPECT_GT(k->offsideNearBand_m, 0.0f);
  EXPECT_LT(k->offsideNearBand_m, k->offsideClearBand_m);
  // An arm leading loosens the tolerance, and the torso is the widest margin.
  EXPECT_GT(k->offsideArmTolerance_m, k->offsideMarginTorso_m);
  EXPECT_GT(k->offsideMarginTorso_m, k->offsideMarginArm_m);
  EXPECT_GT(k->offsideMarginToe_m, k->offsideMarginArm_m);
  // The line's rear-surface shifts are the body points' margins negated, and
  // the leading shift and the tolerance are PES's one shared float.
  EXPECT_FLOAT_EQ(k->offsideRearShiftTorso_m, -k->offsideMarginTorso_m);
  EXPECT_FLOAT_EQ(k->offsideRearShiftArm_m, -k->offsideMarginArm_m);
  EXPECT_FLOAT_EQ(k->offsideRearShiftToe_m, -k->offsideMarginToe_m);
}

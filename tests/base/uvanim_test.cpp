// uvanim: the CPU side of the PES vertex-shader UV animation. The discrete
// functions (StepShift, TimingTile) are pinned to exact float32 bit patterns so a
// future change to the op order (which moves a tile boundary by a frame) fails.
// The continuous ones (Scroll, TimingUv) use tolerances; their exact values are
// float32 anyway but the tolerance keeps the test from pinning a ULP.
#include <gtest/gtest.h>

#include "utils/uvanim.hpp"

#include <cstring>

using blunted::UvAnimParams;
using blunted::UvAnimScrollShift;
using blunted::UvAnimStepShift;
using blunted::UvAnimStepTimingSweep;
using blunted::UvAnimTilesUsed;
using blunted::UvAnimTimingTile;
using blunted::UvAnimTimingUv;

namespace {

// A float32 written from its exact bit pattern (0x3f000000 = 0.5f).
float F32(unsigned int bits) {
  float out;
  std::memcpy(&out, &bits, sizeof out);
  return out;
}

UvAnimParams ScrollParams() {
  UvAnimParams p;
  p.family = UvAnimParams::Family::Scroll;
  p.speedU = 0.1f;
  p.speedV = -0.05f;
  p.offsetS = 0.25f;
  return p;
}

UvAnimParams StepParams(float tileU, float tileV, float tilesUsed, float secCycle) {
  UvAnimParams p;
  p.family = UvAnimParams::Family::Step;
  p.tileU = tileU;
  p.tileV = tileV;
  p.tilesUsed = tilesUsed;
  p.secCycle = secCycle;
  return p;
}

TEST(UvAnim, TilesUsedFallsBackToGrid) {
  UvAnimParams p;  // all defaults: tileU=tileV=1, tilesUsed=0
  EXPECT_FLOAT_EQ(UvAnimTilesUsed(p), 1.0f);
  p.tileU = 2.0f;
  p.tileV = 3.0f;
  EXPECT_FLOAT_EQ(UvAnimTilesUsed(p), 6.0f);
  p.tilesUsed = 4.0f;  // explicit wins
  EXPECT_FLOAT_EQ(UvAnimTilesUsed(p), 4.0f);
}

// The 2x2 grid, 4 tiles, at the PES 2.4s cycle constant. Exact float32: the
// cycle is stored as 2.4000000953674316, so the float32 division is what lands
// the boundary on the clean integer the user authored.
TEST(UvAnim, StepShiftWalksThe2x2Grid) {
  const UvAnimParams p = StepParams(2.0f, 2.0f, 4.0f, 2.4f);
  float du, dv;

  UvAnimStepShift(p, 0.0f, du, dv);
  EXPECT_FLOAT_EQ(du, F32(0x00000000u));
  EXPECT_FLOAT_EQ(dv, F32(0x00000000u));

  UvAnimStepShift(p, 0.6f, du, dv);
  EXPECT_FLOAT_EQ(du, F32(0x3f000000u));  // 0.5
  EXPECT_FLOAT_EQ(dv, F32(0x00000000u));

  UvAnimStepShift(p, 1.2f, du, dv);
  EXPECT_FLOAT_EQ(du, F32(0x00000000u));
  EXPECT_FLOAT_EQ(dv, F32(0x3f000000u));  // 0.5

  UvAnimStepShift(p, 2.1f, du, dv);
  EXPECT_FLOAT_EQ(du, F32(0x3f000000u));
  EXPECT_FLOAT_EQ(dv, F32(0x3f000000u));  // top-right, last tile
}

// The shipped degenerate material: 1x1 grid, 1 tile, 1s cycle. The base-UV
// shift is always (0,0) - nothing moves except the timing alpha.
TEST(UvAnim, StepShiftDegenerateIsZero) {
  const UvAnimParams p = StepParams(1.0f, 1.0f, 1.0f, 1.0f);
  for (float t : {0.0f, 0.5f, 0.999f, 1.0f}) {
    float du, dv;
    UvAnimStepShift(p, t, du, dv);
    EXPECT_FLOAT_EQ(du, 0.0f);
    EXPECT_FLOAT_EQ(dv, 0.0f);
  }
}

// No cycle (Seconds_Per_Animation_Cycle = 0) means no motion at any time.
TEST(UvAnim, StepShiftNoCycleIsZero) {
  const UvAnimParams p = StepParams(2.0f, 2.0f, 4.0f, 0.0f);
  float du, dv;
  UvAnimStepShift(p, 5.0f, du, dv);
  EXPECT_FLOAT_EQ(du, 0.0f);
  EXPECT_FLOAT_EQ(dv, 0.0f);
}

// Shipped content: one tile, so the R channel selects tile 0 no matter its value.
TEST(UvAnim, TimingTileSingleTile) {
  EXPECT_EQ(UvAnimTimingTile(0.0f, 1.0f), 0);
  EXPECT_EQ(UvAnimTimingTile(0.5f, 1.0f), 0);
  EXPECT_EQ(UvAnimTimingTile(0.9f, 1.0f), 0);
  EXPECT_EQ(UvAnimTimingTile(1.0f, 1.0f), 0);
}

// Two tiles: R selects the upper half for the second tile. (255/256 keeps r=1.0
// in tile 1, not an off-grid tile 2.)
TEST(UvAnim, TimingTileTwoTiles) {
  EXPECT_EQ(UvAnimTimingTile(0.0f, 2.0f), 0);
  EXPECT_EQ(UvAnimTimingTile(0.5f, 2.0f), 0);
  EXPECT_EQ(UvAnimTimingTile(0.75f, 2.0f), 1);
  EXPECT_EQ(UvAnimTimingTile(1.0f, 2.0f), 1);
}

TEST(UvAnim, TimingTileFourTiles) {
  EXPECT_EQ(UvAnimTimingTile(0.0f, 4.0f), 0);
  EXPECT_EQ(UvAnimTimingTile(0.1f, 4.0f), 0);
  EXPECT_EQ(UvAnimTimingTile(0.5f, 4.0f), 1);
  EXPECT_EQ(UvAnimTimingTile(0.9f, 4.0f), 3);
  EXPECT_EQ(UvAnimTimingTile(1.0f, 4.0f), 3);
}

// The timing sample UV after the sweep, wrapped to [0,1). Tolerance: continuous.
TEST(UvAnim, TimingUvWraps) {
  float tu, tv;
  UvAnimTimingUv(0.0f, 0.5f, 0.2f, 0.0f, tu, tv);
  EXPECT_NEAR(tu, 0.2f, 1e-6);
  EXPECT_NEAR(tv, 0.5f, 1e-6);

  UvAnimTimingUv(0.9f, 0.9f, 0.2f, 0.2f, tu, tv);  // both wrap
  EXPECT_NEAR(tu, 0.1f, 1e-6);
  EXPECT_NEAR(tv, 0.1f, 1e-6);

  UvAnimTimingUv(0.0f, 0.0f, 0.0f, 0.0f, tu, tv);
  EXPECT_FLOAT_EQ(tu, 0.0f);
  EXPECT_FLOAT_EQ(tv, 0.0f);
}

// uvscroll: a translation that grows with time, by (speed * (t+offset)).
TEST(UvAnim, ScrollShiftGrowsLinearly) {
  const UvAnimParams p = ScrollParams();
  float du, dv;
  UvAnimScrollShift(p, 0.0f, du, dv);
  EXPECT_NEAR(du, 0.1f * 0.25f, 1e-6);
  EXPECT_NEAR(dv, -0.05f * 0.25f, 1e-6);

  UvAnimScrollShift(p, 2.0f, du, dv);
  EXPECT_NEAR(du, 0.1f * 2.25f, 1e-6);
  EXPECT_NEAR(dv, -0.05f * 2.25f, 1e-6);
}

// The timing sweep offset is just time divided by the cycle; 0 cycle means 0.
TEST(UvAnim, StepTimingSweep) {
  UvAnimParams p;
  p.secTimingU = 0.65f;
  float du, dv;
  UvAnimStepTimingSweep(p, 0.0f, du, dv);
  EXPECT_FLOAT_EQ(du, 0.0f);
  EXPECT_FLOAT_EQ(dv, 0.0f);
  UvAnimStepTimingSweep(p, 1.3f, du, dv);
  EXPECT_NEAR(du, 1.3f / 0.65f, 1e-6);
  EXPECT_FLOAT_EQ(dv, 0.0f);

  p.secTimingV = 2.0f;
  UvAnimStepTimingSweep(p, 1.0f, du, dv);
  EXPECT_NEAR(dv, 0.5f, 1e-6);
}

// The ASE wire form: the exact line the importer writes for k2017's boots
// parses back to its parameters, in order.
TEST(UvAnim, ParseAseLineUvstep) {
  UvAnimParams p = blunted::UvAnimParseAseLine(
      "uvstep 1.000000 1.000000 1.000000 1.000000 1.000000 1.000000 0.650000 0.000000");
  EXPECT_EQ(p.family, UvAnimParams::Family::Step);
  EXPECT_FLOAT_EQ(p.tileU, 1.0f);
  EXPECT_FLOAT_EQ(p.tileV, 1.0f);
  EXPECT_FLOAT_EQ(p.tilesUsed, 1.0f);
  EXPECT_FLOAT_EQ(p.scaleUvToTiles, 1.0f);
  EXPECT_FLOAT_EQ(p.secCycle, 1.0f);
  EXPECT_TRUE(p.useTiming);
  EXPECT_FLOAT_EQ(p.secTimingU, 0.65f);
  EXPECT_FLOAT_EQ(p.secTimingV, 0.0f);
}

// The uvscroll family: three floats, no timing map.
TEST(UvAnim, ParseAseLineUvscroll) {
  UvAnimParams p = blunted::UvAnimParseAseLine("uvscroll 0.000000 0.000300 0.500000");
  EXPECT_EQ(p.family, UvAnimParams::Family::Scroll);
  EXPECT_FLOAT_EQ(p.speedU, 0.0f);
  EXPECT_NEAR(p.speedV, 0.0003f, 1e-9);
  EXPECT_FLOAT_EQ(p.offsetS, 0.5f);
}

// Anything unparseable is a static material, not a wrong animation.
TEST(UvAnim, ParseAseLineEmptyIsStatic) {
  EXPECT_EQ(blunted::UvAnimParseAseLine("").family, UvAnimParams::Family::None);
  EXPECT_EQ(blunted::UvAnimParseAseLine("ufo 1.0").family,
            UvAnimParams::Family::None);
  EXPECT_EQ(blunted::UvAnimParseAseLine("uvstep 1.0 2.0").family,
            UvAnimParams::Family::None);
}

// The base-UV scale the shader multiplies with: identity when the scale flag
// is 0 (GIF2PES full-texture UVs), 1/grid when set.
TEST(UvAnim, BaseScaleFollowsTheFlag) {
  UvAnimParams p;
  p.tileU = 4.0f;
  p.tileV = 8.0f;
  float su, sv;
  blunted::UvAnimBaseScale(p, su, sv);
  EXPECT_FLOAT_EQ(su, 1.0f);
  EXPECT_FLOAT_EQ(sv, 1.0f);
  p.scaleUvToTiles = 1.0f;
  blunted::UvAnimBaseScale(p, su, sv);
  EXPECT_FLOAT_EQ(su, 0.25f);
  EXPECT_FLOAT_EQ(sv, 0.125f);
}

}  // namespace

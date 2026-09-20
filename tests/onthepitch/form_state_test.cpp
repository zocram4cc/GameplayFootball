// PES's per-match modifiers: the condition arrow and position familiarity
// (docs/21Research.md, Arrays 2 and 3 of the stat modifier pipeline).

#include <gtest/gtest.h>

#include <cstring>

#include "data/formstate.hpp"

using namespace FormState;

TEST(ConditionArrow, PesTableRowsWorstToBest) {
  // The engine's own numbers: a purple arrow costs awareness 16% and physique
  // 12%; a red one gives awareness 12% and physique only 6%.
  EXPECT_FLOAT_EQ(ArrowMultiplier(Arrow::Terrible, ArrowTier::Awareness), 0.84f);
  EXPECT_FLOAT_EQ(ArrowMultiplier(Arrow::Terrible, ArrowTier::Physique), 0.88f);
  EXPECT_FLOAT_EQ(ArrowMultiplier(Arrow::Normal, ArrowTier::Technical), 1.00f);
  EXPECT_FLOAT_EQ(ArrowMultiplier(Arrow::Top, ArrowTier::Awareness), 1.12f);
  EXPECT_FLOAT_EQ(ArrowMultiplier(Arrow::Top, ArrowTier::Physique), 1.06f);
}

TEST(ConditionArrow, TiersFollowPesNotFamiliarity) {
  // Speed and jump are ATHLETIC for the arrow (tier 2) but TECHNICAL for
  // familiarity (tier 0); acceleration likewise moves between the two tables.
  EXPECT_EQ(ArrowTierOf("physical_velocity"), ArrowTier::Technical);
  EXPECT_EQ(FamiliarityTierOf("physical_velocity"), FamiliarityTier::Technical);
  EXPECT_EQ(ArrowTierOf("physical_acceleration"), ArrowTier::Technical);
  EXPECT_EQ(FamiliarityTierOf("physical_acceleration"), FamiliarityTier::Awareness);
  EXPECT_EQ(ArrowTierOf("gk_reflexes"), ArrowTier::Awareness);
  EXPECT_EQ(ArrowTierOf("physical_stamina"), ArrowTier::Physique);
  EXPECT_EQ(ArrowTierOf("physical_shotpower"), ArrowTier::Awareness);  // kicking power
}

TEST(ConditionArrow, AFixedPolicyHandsEveryoneThatArrow) {
  // The pre-match "Condition: Home" row set to red: every player red, whatever
  // his form or the roll.
  for (float form : {0.0f, 0.5f, 1.0f})
    for (float roll : {0.0f, 0.5f, 0.99f}) {
      EXPECT_EQ(Draw(Policy::AllTop, form, roll), Arrow::Top);
      EXPECT_EQ(Draw(Policy::AllTerrible, form, roll), Arrow::Terrible);
      EXPECT_EQ(Draw(Policy::AllNormal, form, roll), Arrow::Normal);
    }
}

TEST(ConditionArrow, RandomFollowsForm) {
  // A perfectly steady player is never purple and never red; a volatile one
  // reaches both. Normal is the most likely state for either.
  int steadyEnds = 0, volatileEnds = 0, steadyNormal = 0, volatileNormal = 0;
  const int n = 1000;
  for (int i = 0; i < n; i++) {
    const float roll = (i + 0.5f) / n;
    const Arrow steady = Draw(Policy::RandomPerPlayer, 1.0f, roll);
    const Arrow wild = Draw(Policy::RandomPerPlayer, 0.0f, roll);
    if (steady == Arrow::Terrible || steady == Arrow::Top) steadyEnds++;
    if (wild == Arrow::Terrible || wild == Arrow::Top) volatileEnds++;
    if (steady == Arrow::Normal) steadyNormal++;
    if (wild == Arrow::Normal) volatileNormal++;
  }
  EXPECT_EQ(steadyEnds, 0);
  EXPECT_GT(volatileEnds, n / 4);
  EXPECT_GT(steadyNormal, volatileNormal);
  EXPECT_GT(volatileNormal, n / 4);
}

TEST(ConditionArrow, PolicyRoundTripsThroughConfigText) {
  for (int i = 0; i < (int)Policy::Count; i++) {
    const Policy p = (Policy)i;
    EXPECT_EQ(ParsePolicy(SerializePolicy(p)), p);
  }
  EXPECT_EQ(ParsePolicy("red"), Policy::AllTop);
  EXPECT_EQ(ParsePolicy("top"), Policy::AllTop);     // the arrow's own name works too
  EXPECT_EQ(ParsePolicy("nonsense"), Policy::RandomPerPlayer);
}

TEST(PositionFamiliarity, PesTable) {
  EXPECT_FLOAT_EQ(FamiliarityMultiplier(Familiarity::Unfamiliar, FamiliarityTier::Technical), 0.80f);
  EXPECT_FLOAT_EQ(FamiliarityMultiplier(Familiarity::Unfamiliar, FamiliarityTier::Physique), 0.84f);
  EXPECT_FLOAT_EQ(FamiliarityMultiplier(Familiarity::Partial, FamiliarityTier::Awareness), 0.94f);
  EXPECT_FLOAT_EQ(FamiliarityMultiplier(Familiarity::Natural, FamiliarityTier::Technical), 1.00f);
}

TEST(PositionFamiliarity, LeftAndRightAreSeparateSlots) {
  // PES keeps the sides apart: a natural left-back is only partial on the right.
  const std::string lb = InferRatings({e_PlayerRole_LB});
  EXPECT_EQ(Rating(lb, Slot::LB), Familiarity::Natural);
  EXPECT_EQ(Rating(lb, Slot::RB), Familiarity::Partial);
  EXPECT_EQ(Rating(lb, Slot::CF), Familiarity::Unfamiliar);
  EXPECT_EQ(SlotFor(e_PlayerRole_CF, -1), Slot::LWF);
  EXPECT_EQ(SlotFor(e_PlayerRole_CF, 0), Slot::CF);
  EXPECT_EQ(SlotFor(e_PlayerRole_CF, +1), Slot::RWF);
}

TEST(PositionFamiliarity, PesWingersAndSecondStrikerHaveOwnSlots) {
  // PES's forward line has five positions: the engine's ten roles plus the
  // left winger, the right winger and the second striker. Each is its own
  // familiarity slot, the way the sides already are apart.
  EXPECT_EQ(SlotFor(e_PlayerRole_LW, 0), Slot::LWF);
  EXPECT_EQ(SlotFor(e_PlayerRole_LW, 1), Slot::LWF);
  EXPECT_EQ(SlotFor(e_PlayerRole_RW, -1), Slot::RWF);
  EXPECT_EQ(SlotFor(e_PlayerRole_RW, 0), Slot::RWF);
  EXPECT_EQ(SlotFor(e_PlayerRole_SS, 0), Slot::SS);
  EXPECT_EQ(SlotFor(e_PlayerRole_SS, 1), Slot::SS);

  const std::string lw = InferRatings({e_PlayerRole_LW});
  EXPECT_EQ(Rating(lw, Slot::LWF), Familiarity::Natural);
  EXPECT_EQ(Rating(lw, Slot::LMF), Familiarity::Partial);
  EXPECT_EQ(Rating(lw, Slot::CF), Familiarity::Partial);

  const std::string ss = InferRatings({e_PlayerRole_SS});
  EXPECT_EQ(Rating(ss, Slot::SS), Familiarity::Natural);
  EXPECT_EQ(Rating(ss, Slot::AMF), Familiarity::Partial);
  EXPECT_EQ(Rating(ss, Slot::CF), Familiarity::Partial);
}

TEST(PositionFamiliarity, MissingRatingsReadAsUnfamiliar) {
  EXPECT_EQ(Rating("", Slot::GK), Familiarity::Unfamiliar);
  EXPECT_EQ(Rating("A", Slot::CB), Familiarity::Unfamiliar);  // too short
  EXPECT_EQ(Rating("A", Slot::GK), Familiarity::Natural);
}

TEST(FormStateApply, NeverExceedsTheCeiling) {
  // A player already at 1.00 in everything on a red arrow stays at 1.00: the
  // profile scale is 0..1 and nothing downstream may see more (owner, 07-09).
  for (const char* stat : {"technical_shot", "mental_offensivepositioning", "physical_stamina",
                           "gk_reflexes", "physical_velocity"}) {
    EXPECT_FLOAT_EQ(Apply(1.0f, Arrow::Top, Familiarity::Natural, stat), 1.0f) << stat;
    // And a very good player on red does not overshoot either.
    EXPECT_LE(Apply(0.97f, Arrow::Top, Familiarity::Natural, stat), 1.0f) << stat;
  }
}

TEST(FormStateApply, ComposesBothArraysAsAProduct) {
  // Purple arrow AND out of position: 0.5 * 0.88 * 0.80 on a technical stat.
  EXPECT_NEAR(Apply(0.5f, Arrow::Terrible, Familiarity::Unfamiliar, "technical_shot"),
              0.5f * 0.88f * 0.80f, 1e-6f);
  // Green and natural is the identity.
  EXPECT_FLOAT_EQ(Apply(0.63f, Arrow::Normal, Familiarity::Natural, "technical_shot"), 0.63f);
}

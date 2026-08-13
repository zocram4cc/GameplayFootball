// Tests for player traits / specialties described in
// SIMULATION_IMPROVEMENT_PROPOSAL.md section 3A and TECHNICAL_ROADMAP.md
// section 4D.

#include "data/playertraits.hpp"

#include <gtest/gtest.h>

#include <set>

using blunted::Vector3;

TEST(PlayerTraitsParseTest, ParsesACommaSeparatedListIntoAMask) {
  const PlayerTraits::TraitMask mask = PlayerTraits::Parse("speed_merchant,target man");
  EXPECT_TRUE(PlayerTraits::Has(mask, PlayerTraits::e_Trait_SpeedMerchant));
  EXPECT_TRUE(PlayerTraits::Has(mask, PlayerTraits::e_Trait_TargetMan));
  EXPECT_FALSE(PlayerTraits::Has(mask, PlayerTraits::e_Trait_Knuckleballer));
}

TEST(PlayerTraitsParseTest, IgnoresUnknownEntriesAndEmptyInput) {
  EXPECT_EQ(PlayerTraits::Parse(""), PlayerTraits::traitMaskNone);
  EXPECT_EQ(PlayerTraits::Parse("   "), PlayerTraits::traitMaskNone);
  EXPECT_EQ(PlayerTraits::Parse("teleporter, ,"), PlayerTraits::traitMaskNone);
  EXPECT_EQ(PlayerTraits::Parse("teleporter,knuckleballer"),
            static_cast<PlayerTraits::TraitMask>(PlayerTraits::e_Trait_Knuckleballer));
}

TEST(PlayerTraitsParseTest, SerializeRoundTripsThroughParse) {
  const PlayerTraits::TraitMask mask =
      PlayerTraits::e_Trait_GoalPoacher | PlayerTraits::e_Trait_OneTouchPass;
  EXPECT_EQ(PlayerTraits::Parse(PlayerTraits::Serialize(mask)), mask);
  EXPECT_EQ(PlayerTraits::Serialize(PlayerTraits::traitMaskNone), "");
}

TEST(PlayerTraitsParseTest, EveryTraitHasAUniqueParsableName) {
  PlayerTraits::TraitMask seen = PlayerTraits::traitMaskNone;
  for (int i = 0; i < PlayerTraits::traitCount; i++) {
    const PlayerTraits::e_Trait trait = PlayerTraits::GetTraitAt(i);
    const std::string name = PlayerTraits::GetName(trait);
    EXPECT_FALSE(name.empty());
    EXPECT_EQ(PlayerTraits::Parse(name), static_cast<PlayerTraits::TraitMask>(trait)) << name;
    EXPECT_FALSE(PlayerTraits::Has(seen, trait)) << "duplicate bit for " << name;
    seen |= trait;
  }
}

// --- Speed Merchant: quicker off the mark, but loses his head at full tilt ---

TEST(SpeedMerchantTest, AcceleratesFasterThanAPlainPlayer) {
  EXPECT_GT(PlayerTraits::GetAccelerationMultiplier(PlayerTraits::e_Trait_SpeedMerchant), 1.0f);
  EXPECT_FLOAT_EQ(PlayerTraits::GetAccelerationMultiplier(PlayerTraits::traitMaskNone), 1.0f);
}

TEST(SpeedMerchantTest, LosesCalmnessOnlyWhenMovingFast) {
  const float baseCalmness = 0.8f;
  const PlayerTraits::TraitMask mask = PlayerTraits::e_Trait_SpeedMerchant;

  EXPECT_FLOAT_EQ(PlayerTraits::GetCalmnessAtSpeed(mask, baseCalmness, 0.0f), baseCalmness);
  EXPECT_LT(PlayerTraits::GetCalmnessAtSpeed(mask, baseCalmness, 1.0f), baseCalmness);
  // The penalty grows with speed.
  EXPECT_LT(PlayerTraits::GetCalmnessAtSpeed(mask, baseCalmness, 1.0f),
            PlayerTraits::GetCalmnessAtSpeed(mask, baseCalmness, 0.5f));
  // Never dips below zero, even with no calmness to spare.
  EXPECT_GE(PlayerTraits::GetCalmnessAtSpeed(mask, 0.0f, 1.0f), 0.0f);
}

TEST(SpeedMerchantTest, PlayersWithoutTheTraitKeepTheirCalmnessAtSpeed) {
  EXPECT_FLOAT_EQ(PlayerTraits::GetCalmnessAtSpeed(PlayerTraits::traitMaskNone, 0.8f, 1.0f), 0.8f);
  EXPECT_FLOAT_EQ(PlayerTraits::GetCalmnessAtSpeed(PlayerTraits::e_Trait_TargetMan, 0.8f, 1.0f),
                  0.8f);
}

// --- Target Man: heads it and shields it ---

TEST(TargetManTest, HeadsTheBallBetter) {
  EXPECT_GT(PlayerTraits::GetHeaderMultiplier(PlayerTraits::e_Trait_TargetMan), 1.0f);
  EXPECT_FLOAT_EQ(PlayerTraits::GetHeaderMultiplier(PlayerTraits::traitMaskNone), 1.0f);
  EXPECT_FLOAT_EQ(PlayerTraits::GetHeaderMultiplier(PlayerTraits::e_Trait_SpeedMerchant), 1.0f);
}

TEST(TargetManTest, ShieldsTheBallOnlyWhileHoldingPosition) {
  const PlayerTraits::TraitMask mask = PlayerTraits::e_Trait_TargetMan;
  EXPECT_GT(PlayerTraits::GetShieldingRadiusBonus(mask, true), 0.0f);
  EXPECT_FLOAT_EQ(PlayerTraits::GetShieldingRadiusBonus(mask, false), 0.0f);
  EXPECT_FLOAT_EQ(PlayerTraits::GetShieldingRadiusBonus(PlayerTraits::traitMaskNone, true), 0.0f);
  // A shielding bonus big enough to matter but small enough not to look silly.
  EXPECT_LE(PlayerTraits::GetShieldingRadiusBonus(mask, true), 0.15f);
}

// --- Knuckleballer: unpredictable flight on long shots ---

TEST(KnuckleballerTest, LeavesShortShotsAlone) {
  const Vector3 rotVec(0.0f, 0.0f, 4.0f);
  const Vector3 result = PlayerTraits::ApplyKnuckleballSpin(PlayerTraits::e_Trait_Knuckleballer,
                                                            rotVec, 12.0f, 1.0f);
  EXPECT_FLOAT_EQ(result.coords[0], rotVec.coords[0]);
  EXPECT_FLOAT_EQ(result.coords[1], rotVec.coords[1]);
  EXPECT_FLOAT_EQ(result.coords[2], rotVec.coords[2]);
}

TEST(KnuckleballerTest, PerturbsLongShotsInTheDirectionOfTheNoiseSample) {
  const Vector3 rotVec(0.0f, 0.0f, 4.0f);
  const Vector3 positive = PlayerTraits::ApplyKnuckleballSpin(PlayerTraits::e_Trait_Knuckleballer,
                                                              rotVec, 30.0f, 1.0f);
  const Vector3 negative = PlayerTraits::ApplyKnuckleballSpin(PlayerTraits::e_Trait_Knuckleballer,
                                                              rotVec, 30.0f, -1.0f);
  EXPECT_GT(positive.coords[1], rotVec.coords[1]);
  EXPECT_LT(negative.coords[1], rotVec.coords[1]);
  // Symmetric around the unperturbed value.
  EXPECT_NEAR(positive.coords[1] - rotVec.coords[1], rotVec.coords[1] - negative.coords[1], 1e-5f);
}

TEST(KnuckleballerTest, ZeroNoiseIsAlwaysANoOp) {
  const Vector3 rotVec(1.0f, 2.0f, 3.0f);
  const Vector3 result = PlayerTraits::ApplyKnuckleballSpin(PlayerTraits::e_Trait_Knuckleballer,
                                                            rotVec, 40.0f, 0.0f);
  EXPECT_FLOAT_EQ(result.coords[0], rotVec.coords[0]);
  EXPECT_FLOAT_EQ(result.coords[1], rotVec.coords[1]);
  EXPECT_FLOAT_EQ(result.coords[2], rotVec.coords[2]);
}

TEST(KnuckleballerTest, KeepsThePerturbationBounded) {
  const Vector3 rotVec(0.0f, 0.0f, 4.0f);
  for (float sample = -1.0f; sample <= 1.0f; sample += 0.25f) {
    const Vector3 result = PlayerTraits::ApplyKnuckleballSpin(PlayerTraits::e_Trait_Knuckleballer,
                                                              rotVec, 45.0f, sample);
    EXPECT_LE(std::abs(result.coords[1] - rotVec.coords[1]), PlayerTraits::knuckleballMaxSpin);
  }
}

TEST(KnuckleballerTest, PlayersWithoutTheTraitFlyTrue) {
  const Vector3 rotVec(0.0f, 0.0f, 4.0f);
  const Vector3 result =
      PlayerTraits::ApplyKnuckleballSpin(PlayerTraits::traitMaskNone, rotVec, 40.0f, 1.0f);
  EXPECT_FLOAT_EQ(result.coords[1], rotVec.coords[1]);
}

// --- Playing styles every player carries, so the flair actually shows up ---

TEST(PlayingStyleTest, EveryPlayerGetsAStyleWithoutTouchingTheDatabase) {
  // Same player, same style, every match: deterministic from his id.
  const PlayerTraits::TraitMask first = PlayerTraits::AssignForPlayer(1234, e_PlayerRole_CF, 0.7f);
  const PlayerTraits::TraitMask again = PlayerTraits::AssignForPlayer(1234, e_PlayerRole_CF, 0.7f);
  EXPECT_EQ(first, again);
  EXPECT_NE(first, PlayerTraits::traitMaskNone);
}

TEST(PlayingStyleTest, DifferentPlayersGetDifferentFlair) {
  std::set<PlayerTraits::TraitMask> masks;
  for (int id = 1; id <= 40; id++)
    masks.insert(PlayerTraits::AssignForPlayer(id, e_PlayerRole_CM, 0.6f));
  // Not everybody the same; a squad should look varied.
  EXPECT_GE(masks.size(), 4u);
}

TEST(PlayingStyleTest, StylesSuitThePositionTheyArePlayedIn) {
  // A striker can poach or finish, but never plays as a deep-lying anchor.
  for (int id = 1; id <= 60; id++) {
    const PlayerTraits::TraitMask striker = PlayerTraits::AssignForPlayer(id, e_PlayerRole_CF, 0.7f);
    EXPECT_FALSE(PlayerTraits::Has(striker, PlayerTraits::e_Trait_Anchorman)) << id;

    const PlayerTraits::TraitMask centreBack =
        PlayerTraits::AssignForPlayer(id, e_PlayerRole_CB, 0.4f);
    EXPECT_FALSE(PlayerTraits::Has(centreBack, PlayerTraits::e_Trait_GoalPoacher)) << id;
    EXPECT_FALSE(PlayerTraits::Has(centreBack, PlayerTraits::e_Trait_FoxInTheBox)) << id;
  }
}

TEST(PlayingStyleTest, GoodFinishersAreTheOnesWhoShootFromRange) {
  int rangeShootersAmongGoodStrikers = 0;
  int rangeShootersAmongPoorStrikers = 0;
  for (int id = 1; id <= 60; id++) {
    if (PlayerTraits::Has(PlayerTraits::AssignForPlayer(id, e_PlayerRole_AM, 0.95f),
                          PlayerTraits::e_Trait_LongRangeShooter))
      rangeShootersAmongGoodStrikers++;
    if (PlayerTraits::Has(PlayerTraits::AssignForPlayer(id, e_PlayerRole_AM, 0.15f),
                          PlayerTraits::e_Trait_LongRangeShooter))
      rangeShootersAmongPoorStrikers++;
  }
  EXPECT_GT(rangeShootersAmongGoodStrikers, rangeShootersAmongPoorStrikers);
}

TEST(PlayingStyleTest, NobodyIsOverloadedWithStyles) {
  for (int id = 1; id <= 60; id++) {
    const PlayerTraits::TraitMask mask = PlayerTraits::AssignForPlayer(id, e_PlayerRole_CM, 0.6f);
    int styles = 0;
    for (int i = 0; i < PlayerTraits::traitCount; i++) {
      if (PlayerTraits::Has(mask, PlayerTraits::GetTraitAt(i)))
        styles++;
    }
    EXPECT_GE(styles, 1) << id;
    EXPECT_LE(styles, 3) << id;
  }
}

// --- Shooting appetite: the direct lever on how open the game is ---

TEST(ShotAppetiteTest, APlainPlayerHasNeutralAppetite) {
  EXPECT_FLOAT_EQ(PlayerTraits::GetShotAppetite(PlayerTraits::traitMaskNone), 1.0f);
  EXPECT_FLOAT_EQ(PlayerTraits::GetShootingRangeBonus(PlayerTraits::traitMaskNone), 0.0f);
}

TEST(ShotAppetiteTest, RangeShootersTryFromFurtherOut) {
  EXPECT_GT(PlayerTraits::GetShootingRangeBonus(PlayerTraits::e_Trait_LongRangeShooter), 0.0f);
  EXPECT_GT(PlayerTraits::GetShotAppetite(PlayerTraits::e_Trait_LongRangeShooter), 1.0f);
}

TEST(ShotAppetiteTest, PoachersAndBoxFoxesShootMoreReadilyThanPlaymakers) {
  EXPECT_GT(PlayerTraits::GetShotAppetite(PlayerTraits::e_Trait_FoxInTheBox), 1.0f);
  EXPECT_GT(PlayerTraits::GetShotAppetite(PlayerTraits::e_Trait_GoalPoacher), 1.0f);
  EXPECT_LT(PlayerTraits::GetShotAppetite(PlayerTraits::e_Trait_CreativePlaymaker), 1.0f);
}

TEST(ShotAppetiteTest, AppetiteStaysSaneEvenWithEveryStyleStacked) {
  PlayerTraits::TraitMask everything = PlayerTraits::traitMaskNone;
  for (int i = 0; i < PlayerTraits::traitCount; i++)
    everything |= PlayerTraits::GetTraitAt(i);

  EXPECT_GT(PlayerTraits::GetShotAppetite(everything), 0.5f);
  EXPECT_LE(PlayerTraits::GetShotAppetite(everything), 2.0f);
  EXPECT_LE(PlayerTraits::GetShootingRangeBonus(everything), 14.0f);
}

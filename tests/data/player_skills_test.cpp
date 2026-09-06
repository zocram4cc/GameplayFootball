#include <gtest/gtest.h>

#include <set>
#include <string>

#include "data/playerdata.hpp"
#include "data/playerskills.hpp"
#include "main.hpp"
#include "utils/database.hpp"

// main.hpp's globals are stubbed by playing_styles_test.cpp in this binary.

namespace {

using PlayerSkills::Feint;
using PlayerSkills::Has;
using PlayerSkills::Mask;
using PlayerSkills::Skill;
using blunted::Vector3;

const float kPi = 3.14159265f;

const e_PlayerRole allRoles[] = {e_PlayerRole_GK, e_PlayerRole_CB, e_PlayerRole_LB, e_PlayerRole_RB,
                                 e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
                                 e_PlayerRole_AM, e_PlayerRole_CF};

Mask Bits(std::initializer_list<Skill> skills) {
  Mask mask = PlayerSkills::maskNone;
  for (Skill skill : skills)
    mask |= PlayerSkills::Bit(skill);
  return mask;
}

int Count(Mask mask) {
  int count = 0;
  for (int i = 0; i < PlayerSkills::skillCount; i++)
    if (Has(mask, PlayerSkills::GetAt(i)))
      count++;
  return count;
}

// A PlayerData whose ratings the test sets itself.
struct RatedPlayer : public PlayerData {
  void Rate(const char* stat, float value) { stats.Set(stat, value); }
};

// Two rows, one with the current <skills> tag and one with the pre-PES <traits>
// tag, so PlayerData(id) can be built from a profile.
void LoadProfileDatabase() {
  static bool loaded = false;
  if (loaded)
    return;
  ASSERT_TRUE(GetDB()->Load(":memory:"));
  GetDB()->Query(
      "CREATE TABLE players(id INTEGER PRIMARY KEY, firstname TEXT, lastname TEXT, role TEXT, "
      "age INTEGER, base_stat INTEGER, profile_xml TEXT, skincolor INTEGER, hairstyle TEXT, "
      "haircolor TEXT, height REAL);");
  GetDB()->Query(
      "INSERT INTO players VALUES (1, 'A', 'Skills', 'AM', 25, 60, "
      "'<technical_shot>0.7</technical_shot><skills>rabona, Cut Behind Turn</skills>', "
      "1, 'short01', 'darkblonde', 1.8);");
  GetDB()->Query(
      "INSERT INTO players VALUES (2, 'B', 'Traits', 'CF', 25, 60, "
      "'<technical_shot>0.7</technical_shot><traits>knuckleballer,target_man</traits>', "
      "1, 'short01', 'darkblonde', 1.8);");
  GetDB()->Query(
      "INSERT INTO players VALUES (3, 'C', 'None', 'CF', 25, 60, "
      "'<technical_shot>0.7</technical_shot><skills>none</skills>', "
      "1, 'short01', 'darkblonde', 1.8);");
  loaded = true;
}

PlayerSkills::FeintSituation ManInFront(float turnAngle = 0.0f) {
  PlayerSkills::FeintSituation situation;
  situation.opponentDistance = 2.5f;
  situation.opponentAngle = 0.2f;
  situation.turnAngle = turnAngle;
  return situation;
}

}  // namespace

// --- Parse / serialize ---

TEST(PlayerSkillsParseTest, EverySkillRoundTripsInPesBitOrder) {
  std::set<std::string> tokens;
  for (int i = 0; i < PlayerSkills::skillCount; i++) {
    const Skill skill = PlayerSkills::GetAt(i);
    const std::string token = PlayerSkills::GetName(skill);
    EXPECT_FALSE(token.empty());
    EXPECT_TRUE(tokens.insert(token).second) << "duplicate token " << token;
    EXPECT_EQ(PlayerSkills::Parse(token), PlayerSkills::Bit(skill)) << token;
  }
  // The importer's bits are PES's: bit 0 Scissors Feint, bit 40 Fighting Spirit.
  EXPECT_EQ(PlayerSkills::GetName(PlayerSkills::GetAt(0)), "scissors_feint");
  EXPECT_EQ(PlayerSkills::GetName(PlayerSkills::GetAt(PlayerSkills::pesSkillCount - 1)),
            "fighting_spirit");
  EXPECT_EQ(PlayerSkills::GetName(PlayerSkills::GetAt(PlayerSkills::pesSkillCount)),
            "speed_merchant");
}

TEST(PlayerSkillsParseTest, ListsLegacyNamesAndUnknownsParse) {
  const Mask mask = PlayerSkills::Parse("Cut Behind & Turn, knuckleballer,bogus, one_touch_pass");
  EXPECT_EQ(mask, Bits({Skill::CutBehindTurn, Skill::KnuckleShot, Skill::OneTouchPass}));
  EXPECT_EQ(PlayerSkills::Parse(PlayerSkills::Serialize(mask)), mask);
  EXPECT_EQ(PlayerSkills::Parse(""), PlayerSkills::maskNone);
  EXPECT_EQ(PlayerSkills::Parse("none"), PlayerSkills::maskNone);
  EXPECT_EQ(PlayerSkills::Serialize(PlayerSkills::maskNone), "");
}

TEST(PlayerSkillsParseTest, ProfileReadsSkillsOrTheLegacyTraitsTagAndInfersWhenAbsent) {
  LoadProfileDatabase();
  PlayerData withSkills(1);
  EXPECT_EQ(withSkills.GetSkills(), Bits({Skill::Rabona, Skill::CutBehindTurn}));
  PlayerData withTraits(2);
  EXPECT_EQ(withTraits.GetSkills(), Bits({Skill::KnuckleShot, Skill::TargetMan}));
  // "none" is PES's answer and stays none; only an absent tag is inferred.
  PlayerData none(3);
  EXPECT_EQ(none.GetSkills(), PlayerSkills::maskNone);
}

// --- Inference ---

TEST(PlayerSkillsInferTest, IsDeterministicLegalForTheRoleCappedAndVaried) {
  PlayerData flat;  // every rating 0.6, as the imported squads are
  for (e_PlayerRole role : allRoles) {
    std::set<Mask> seen;
    for (int id = 1; id <= 60; id++) {
      const Mask mask = PlayerSkills::Infer(id, role, flat);
      EXPECT_EQ(mask, PlayerSkills::Infer(id, role, flat));
      EXPECT_LE(Count(mask), 10) << role;
      for (int i = 0; i < PlayerSkills::skillCount; i++) {
        const Skill skill = PlayerSkills::GetAt(i);
        if (Has(mask, skill))
          EXPECT_TRUE(PlayerSkills::SuitsRole(skill, role))
              << PlayerSkills::GetName(skill) << " on role " << role;
      }
      if (role != e_PlayerRole_GK)
        EXPECT_NE(mask, PlayerSkills::maskNone) << role << " " << id;
      seen.insert(mask);
    }
    EXPECT_GE(seen.size(), 8u) << role;
  }
}

TEST(PlayerSkillsInferTest, TheRatingBehindASkillMakesItLikelier) {
  RatedPlayer dribbler;
  dribbler.Rate("technical_dribble", 0.95f);
  RatedPlayer plodder;
  plodder.Rate("technical_dribble", 0.45f);
  int tricksOfDribbler = 0;
  int tricksOfPlodder = 0;
  for (int id = 1; id <= 200; id++) {
    if (Has(PlayerSkills::Infer(id, e_PlayerRole_LM, dribbler), Skill::ScissorsFeint))
      tricksOfDribbler++;
    if (Has(PlayerSkills::Infer(id, e_PlayerRole_LM, plodder), Skill::ScissorsFeint))
      tricksOfPlodder++;
  }
  EXPECT_GT(tricksOfDribbler, tricksOfPlodder * 2);
  EXPECT_LT(tricksOfPlodder, 40);
}

// --- Trick moves ---

TEST(PlayerFeintTest, ClipNamesMapToAFamilyAndAnEntryAngle) {
  EXPECT_EQ(PlayerSkills::FeintFromClipName(
                "ballcontrol/dribble/feint/pes_feint_kick_0_3_090_in_cruijff_ver12.anim"),
            Feint::CutBehindTurn);
  EXPECT_EQ(PlayerSkills::FeintAngleFromClipName("pes_feint_kick_0_3_090_in_cruijff_ver12"), 90);
  EXPECT_EQ(PlayerSkills::FeintFromClipName("pes_feint_kick_0_3_f045_out_ver12.anim"),
            Feint::KickFeint);
  EXPECT_EQ(PlayerSkills::FeintAngleFromClipName("pes_feint_kick_0_3_f045_out_ver12.anim"), 45);
  EXPECT_EQ(PlayerSkills::FeintFromClipName("pes_feintrun_chapeau_3_3_f135_y0_out_ver20.anim"),
            Feint::Sombrero);
  EXPECT_EQ(PlayerSkills::FeintFromClipName("pes_feintslide090_sciin_3_3_slide_090_R.anim"),
            Feint::Scissors);
  EXPECT_EQ(PlayerSkills::FeintAngleFromClipName("pes_feintslide090_sciin_3_3_slide_090_R.anim"),
            90);
  EXPECT_EQ(PlayerSkills::FeintFromClipName("pes_feint_ballroll_rabona_0_3_f045_y0_out_ver01"),
            Feint::Rabona);
  EXPECT_EQ(PlayerSkills::FeintFromClipName("pes_feintrun_through_3_3_s_000_y4.anim"),
            Feint::Nutmeg);
  EXPECT_EQ(PlayerSkills::FeintAngleFromClipName("pes_feintrun_through_3_3_s_000_y4.anim"), 0);
  EXPECT_EQ(PlayerSkills::FeintFromClipName("pes_feint_roulette_0_3_ribery_f090.anim"),
            Feint::MarseilleTurn);
  EXPECT_EQ(PlayerSkills::FeintFromClipName("pes_feintrun_2_3_090_sidenear_y4_out_ver12.anim"),
            Feint::BodyFake);
  // Not a feint: the jostle clips share the directory, stock clips do not start with the prefix.
  EXPECT_EQ(PlayerSkills::FeintFromClipName("pes_js_run_2_2_000_hardpress_sub.anim"), Feint::None);
  EXPECT_EQ(PlayerSkills::FeintFromClipName("ballcontrol/idle/000.anim"), Feint::None);
  EXPECT_EQ(PlayerSkills::FeintAngleFromClipName("nothing_here"), -1);
}

TEST(PlayerFeintTest, NeedsTheSkillAManInRangeAndALuckyRoll) {
  const PlayingStyles::ComMask noCards = PlayingStyles::comMaskNone;
  const Mask rabona = Bits({Skill::Rabona});
  // Nobody without the skill ever tries, however the dice fall.
  for (int i = 0; i < 20; i++)
    EXPECT_EQ(PlayerSkills::PickFeint(PlayerSkills::maskNone, noCards, ManInFront(), i * 0.05f, 0.5f),
              Feint::None);
  // With it: a lucky roll and a man in front produce the move.
  EXPECT_EQ(PlayerSkills::PickFeint(rabona, noCards, ManInFront(), 0.0f, 0.5f), Feint::Rabona);
  // An unlucky roll, nobody near, a man behind or a man on top of him: nothing.
  EXPECT_EQ(PlayerSkills::PickFeint(rabona, noCards, ManInFront(), 0.99f, 0.5f), Feint::None);
  PlayerSkills::FeintSituation far = ManInFront();
  far.opponentDistance = 12.0f;
  EXPECT_EQ(PlayerSkills::PickFeint(rabona, noCards, far, 0.0f, 0.5f), Feint::None);
  PlayerSkills::FeintSituation behind = ManInFront();
  behind.opponentAngle = 0.9f * kPi;
  EXPECT_EQ(PlayerSkills::PickFeint(rabona, noCards, behind, 0.0f, 0.5f), Feint::None);
  PlayerSkills::FeintSituation onTop = ManInFront();
  onTop.opponentDistance = 0.3f;
  EXPECT_EQ(PlayerSkills::PickFeint(rabona, noCards, onTop, 0.0f, 0.5f), Feint::None);
}

TEST(PlayerFeintTest, TurnMovesWantATurnAndBeatMovesWantToCarryOn) {
  const PlayingStyles::ComMask noCards = PlayingStyles::comMaskNone;
  const Mask both = Bits({Skill::CutBehindTurn, Skill::ScissorsFeint});
  EXPECT_EQ(PlayerSkills::PickFeint(both, noCards, ManInFront(0.0f), 0.0f, 0.5f), Feint::Scissors);
  EXPECT_EQ(PlayerSkills::PickFeint(both, noCards, ManInFront(0.8f * kPi), 0.0f, 0.5f),
            Feint::CutBehindTurn);
  // Only a turn move, going straight on: nothing to play.
  EXPECT_EQ(PlayerSkills::PickFeint(Bits({Skill::MarseilleTurn}), noCards, ManInFront(0.0f), 0.0f,
                                    0.5f),
            Feint::None);
}

TEST(PlayerFeintTest, TheTricksterTriesMoreOftenAndTriesMovesNobodyElseWill) {
  const PlayingStyles::ComMask trickster = static_cast<PlayingStyles::ComMask>(
      PlayingStyles::Com::Trickster);
  const Mask rabona = Bits({Skill::Rabona});
  // A roll that is too unlucky for a plain player is good enough for a trickster.
  const float roll = 0.5f * (PlayerSkills::feintAttemptChance + PlayerSkills::tricksterFeintAttemptChance);
  EXPECT_EQ(PlayerSkills::PickFeint(rabona, PlayingStyles::comMaskNone, ManInFront(), roll, 0.0f),
            Feint::None);
  EXPECT_NE(PlayerSkills::PickFeint(rabona, trickster, ManInFront(), roll, 0.0f), Feint::None);
  // The body fake, kick feint and nutmeg are on no card: only the trickster plays them.
  EXPECT_FALSE(PlayerSkills::Unlocks(PlayerSkills::maskNone, PlayingStyles::comMaskNone,
                                     Feint::BodyFake));
  EXPECT_TRUE(PlayerSkills::Unlocks(PlayerSkills::maskNone, trickster, Feint::BodyFake));
  EXPECT_TRUE(PlayerSkills::Unlocks(PlayerSkills::maskNone, trickster, Feint::Nutmeg));
  EXPECT_NE(PlayerSkills::PickFeint(PlayerSkills::maskNone, trickster, ManInFront(), 0.0f, 0.5f),
            Feint::None);
  // Skills still count for him: a card never unlocks the rabona.
  EXPECT_FALSE(PlayerSkills::Unlocks(PlayerSkills::maskNone, trickster, Feint::Rabona));
}

TEST(PlayerFeintTest, APoorDribblerFumblesAndTheBallRunsLoose) {
  EXPECT_TRUE(PlayerSkills::FeintFumbled(0.4f, 0.4f, 0.6f));
  EXPECT_FALSE(PlayerSkills::FeintFumbled(0.95f, 0.95f, 0.6f));
  // Even the best can fumble, even the worst can pull it off.
  EXPECT_TRUE(PlayerSkills::FeintFumbled(1.0f, 1.0f, 0.999f));
  EXPECT_FALSE(PlayerSkills::FeintFumbled(0.0f, 0.0f, 0.1f));
  const Vector3 touch(4.0f, 0.0f, 0.0f);
  const Vector3 loose = PlayerSkills::FumbleTouch(touch, 1.0f);
  EXPECT_GT(loose.GetLength(), touch.GetLength() * 1.3f);
  EXPECT_GT(std::fabs(loose.GetAngle2D(touch)), 0.2f * kPi);
}

// --- One effector per non-trick group ---

TEST(PlayerSkillsEffectTest, ShotSpinKnucklesDipsAndRises) {
  const Vector3 none(0, 0, 0);
  const Vector3 forward(1, 0, 0);
  EXPECT_EQ(PlayerSkills::ApplyShotSpin(PlayerSkills::maskNone, none, forward, 30.0f, 1.0f).GetLength(),
            0.0f);
  // Knuckle: only from range, along the noise.
  EXPECT_EQ(PlayerSkills::ApplyShotSpin(Bits({Skill::KnuckleShot}), none, forward, 10.0f, 1.0f)
                .GetLength(),
            0.0f);
  EXPECT_GT(PlayerSkills::ApplyShotSpin(Bits({Skill::KnuckleShot}), none, forward, 30.0f, 1.0f)
                .coords[1],
            0.0f);
  // Dipping is topspin over the direction of travel; rising is its negative.
  const Vector3 dip = PlayerSkills::ApplyShotSpin(Bits({Skill::DippingShots}), none, forward, 10.0f, 0.0f);
  const Vector3 rise = PlayerSkills::ApplyShotSpin(Bits({Skill::RisingShots}), none, forward, 10.0f, 0.0f);
  EXPECT_GT(dip.coords[1], 0.0f);
  EXPECT_FLOAT_EQ(rise.coords[1], -dip.coords[1]);
  // Chip Shot Control lifts it only over a keeper who has come out.
  EXPECT_TRUE(PlayerSkills::WantsChip(Bits({Skill::ChipShotControl}), 8.0f, 6.0f));
  EXPECT_FALSE(PlayerSkills::WantsChip(Bits({Skill::ChipShotControl}), 8.0f, 1.0f));
  EXPECT_FALSE(PlayerSkills::WantsChip(PlayerSkills::maskNone, 8.0f, 6.0f));
  EXPECT_GT(PlayerSkills::GetShootingRangeBonus(Bits({Skill::LongRangeDrive})), 0.0f);
  EXPECT_GT(PlayerSkills::GetFirstTimeShotPowerMultiplier(Bits({Skill::FirstTimeShot}), true, 8.0f),
            1.0f);
  EXPECT_FLOAT_EQ(PlayerSkills::GetFirstTimeShotPowerMultiplier(Bits({Skill::FirstTimeShot}), false, 8.0f),
                  1.0f);
}

TEST(PlayerSkillsEffectTest, PassingSkillsEaseTheBallTheyAreNamedFor) {
  EXPECT_FLOAT_EQ(PlayerSkills::GetQuickReleaseAccuracyPenalty(Bits({Skill::OneTouchPass}), 100, 0.4f),
                  0.0f);
  EXPECT_FLOAT_EQ(PlayerSkills::GetQuickReleaseAccuracyPenalty(Bits({Skill::OneTouchPass}), 900, 0.4f),
                  0.4f);
  EXPECT_GT(PlayerSkills::GetBodyDirectionPassPenalty(Bits({Skill::NoLookPass}), 0.3f), 0.3f);
  EXPECT_FLOAT_EQ(PlayerSkills::GetBodyDirectionPassPenalty(PlayerSkills::maskNone, 0.3f), 0.3f);
  EXPECT_LT(PlayerSkills::GetPassDifficultyMultiplier(Bits({Skill::WeightedPass}),
                                                      e_FunctionType_ShortPass, false),
            1.0f);
  EXPECT_FLOAT_EQ(PlayerSkills::GetPassDifficultyMultiplier(Bits({Skill::WeightedPass}),
                                                            e_FunctionType_HighPass, true),
                  1.0f);
  EXPECT_LT(PlayerSkills::GetPassDifficultyMultiplier(Bits({Skill::PinpointCrossing}),
                                                      e_FunctionType_HighPass, true),
            1.0f);
  EXPECT_FLOAT_EQ(PlayerSkills::GetPassDifficultyMultiplier(Bits({Skill::PinpointCrossing}),
                                                            e_FunctionType_HighPass, false),
                  1.0f);
  const Vector3 lofted(10.0f, 0.0f, 6.0f);
  const Vector3 flat = PlayerSkills::ShapePassTouch(Bits({Skill::LowLoftedPass}),
                                                    e_FunctionType_HighPass, false, lofted);
  EXPECT_LT(flat.coords[2], lofted.coords[2]);
  EXPECT_GT(flat.coords[0], lofted.coords[0]);
  EXPECT_GT(PlayerSkills::GetThroughBallBonus(Bits({Skill::ThroughPassing}), 0.3f), 0.3f);
  EXPECT_GT(PlayerSkills::GetPassCurve(Bits({Skill::OutsideCurler}), 0.1f), 0.1f);
}

TEST(PlayerSkillsEffectTest, DefendingSkillsChangeTheAssignment) {
  EXPECT_GT(PlayerSkills::GetMarkingQualityBonus(Bits({Skill::ManMarking})), 0.0f);
  EXPECT_LT(PlayerSkills::GetTrackBackDepth(Bits({Skill::TrackBack}), false), 0.0f);
  EXPECT_FLOAT_EQ(PlayerSkills::GetTrackBackDepth(Bits({Skill::TrackBack}), true), 0.0f);
  EXPECT_GT(PlayerSkills::GetBallDuelLikeliness(Bits({Skill::Interception}), 0.5f), 0.5f);
  EXPECT_GT(PlayerSkills::GetFoulScoreBonus(Bits({Skill::Gamesmanship})), 0.0f);
}

TEST(PlayerSkillsEffectTest, KeeperAndPenaltySkills) {
  using PlayerSkills::Distribution;
  EXPECT_EQ(PlayerSkills::PickDistribution(PlayerSkills::maskNone, 0.0f), Distribution::Default);
  EXPECT_EQ(PlayerSkills::PickDistribution(Bits({Skill::GkLowPunt}), 0.0f), Distribution::LowPunt);
  // Even a carded keeper hoofs it now and then.
  EXPECT_EQ(PlayerSkills::PickDistribution(Bits({Skill::GkLowPunt}), 0.9f), Distribution::Default);
  EXPECT_EQ(PlayerSkills::PickDistribution(Bits({Skill::GkLongThrow, Skill::GkHighPunt}), 0.7f),
            Distribution::HighPunt);
  EXPECT_GT(PlayerSkills::GetPenaltyReflexes(Bits({Skill::GkPenaltySaver}), 0.6f), 0.6f);
  EXPECT_GT(PlayerSkills::GetPenaltyRating(Bits({Skill::PenaltySpecialist}), 0.6f), 0.6f);
  EXPECT_LE(PlayerSkills::GetPenaltyRating(Bits({Skill::PenaltySpecialist}), 0.95f), 1.0f);
}

TEST(PlayerSkillsEffectTest, MentalitySkills) {
  EXPECT_LT(PlayerSkills::GetStumbleChanceMultiplier(PlayerSkills::maskNone, true), 1.0f);
  EXPECT_LT(PlayerSkills::GetStumbleChanceMultiplier(Bits({Skill::FightingSpirit}), false), 1.0f);
  EXPECT_FLOAT_EQ(PlayerSkills::GetStumbleChanceMultiplier(PlayerSkills::maskNone, false), 1.0f);
  EXPECT_LT(PlayerSkills::GetFatigueDrainMultiplier(Bits({Skill::FightingSpirit})), 1.0f);
  EXPECT_GT(PlayerSkills::GetSubstituteBonus(Bits({Skill::SuperSub}), false), 0.0f);
  // Already on the pitch, the card buys him nothing.
  EXPECT_FLOAT_EQ(PlayerSkills::GetSubstituteBonus(Bits({Skill::SuperSub}), true), 0.0f);
}

TEST(PlayerSkillsEffectTest, EngineSpecialtiesSurvive) {
  EXPECT_GT(PlayerSkills::GetAccelerationMultiplier(Bits({Skill::SpeedMerchant})), 1.0f);
  EXPECT_LT(PlayerSkills::GetCalmnessAtSpeed(Bits({Skill::SpeedMerchant}), 0.8f, 1.0f), 0.8f);
  EXPECT_FLOAT_EQ(PlayerSkills::GetCalmnessAtSpeed(Bits({Skill::SpeedMerchant}), 0.8f, 0.0f), 0.8f);
  EXPECT_GT(PlayerSkills::GetHeaderMultiplier(Bits({Skill::TargetMan})), 1.0f);
  EXPECT_GT(PlayerSkills::GetHeaderMultiplier(Bits({Skill::Heading})), 1.0f);
  EXPECT_GT(PlayerSkills::GetShieldingRadiusBonus(Bits({Skill::TargetMan}), true), 0.0f);
  EXPECT_FLOAT_EQ(PlayerSkills::GetShieldingRadiusBonus(Bits({Skill::TargetMan}), false), 0.0f);
}

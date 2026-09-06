#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>

#include "base/properties.hpp"
#include "data/playerdata.hpp"
#include "data/playingstyles.hpp"
#include "main.hpp"
#include "utils/database.hpp"

// playerdata.cpp reaches into main.hpp globals; the same stubs the pass-failure
// test supplies. Only the default PlayerData() is constructed here, so the
// database is never touched.
Database* GetDB() {
  static blunted::Database database;
  return &database;
}
Properties* GetConfiguration() {
  static blunted::Properties configuration;
  return &configuration;
}
std::string GetActiveSaveDirectory() {
  return "";
}
void SetActiveSaveDirectory(const std::string& dir) {
  (void)dir;
}
bool Verbose() {
  return false;
}
std::shared_ptr<Scene2D> GetScene2D() {
  return nullptr;
}
std::shared_ptr<Scene3D> GetScene3D() {
  return nullptr;
}
GraphicsSystem* GetGraphicsSystem() {
  return nullptr;
}
std::shared_ptr<GameTask> GetGameTask() {
  return nullptr;
}
std::shared_ptr<MenuTask> GetMenuTask() {
  return nullptr;
}

namespace {

using PlayingStyles::Com;
using PlayingStyles::ComMask;
using Style = PlayingStyles::Player;  // the engine's class Player is also in scope

const int styleCount = static_cast<int>(Style::Count);

const e_PlayerRole allRoles[] = {e_PlayerRole_GK, e_PlayerRole_CB, e_PlayerRole_LB, e_PlayerRole_RB,
                                 e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
                                 e_PlayerRole_AM, e_PlayerRole_CF};

// Every card, so a COM effector can be checked against the empty hand.
ComMask AllCards() {
  ComMask mask = PlayingStyles::comMaskNone;
  for (int i = 0; i < PlayingStyles::comCount; i++)
    mask |= static_cast<ComMask>(PlayingStyles::GetComAt(i));
  return mask;
}

}  // namespace

TEST(PlayingStylesParseTest, EveryStyleRoundTripsThroughItsToken) {
  std::set<std::string> tokens;
  for (int i = 0; i < styleCount; i++) {
    const Style style = static_cast<Style>(i);
    const std::string token = PlayingStyles::Serialize(style);
    EXPECT_FALSE(token.empty());
    EXPECT_TRUE(tokens.insert(token).second) << "duplicate token " << token;
    EXPECT_EQ(PlayingStyles::ParsePlayer(token), style) << token;
  }
  // The importer's spellings and a human's both land.
  EXPECT_EQ(PlayingStyles::ParsePlayer("classic_no_10"), Style::ClassicNo10);
  EXPECT_EQ(PlayingStyles::ParsePlayer("Classic No. 10"), Style::ClassicNo10);
  EXPECT_EQ(PlayingStyles::ParsePlayer("none"), Style::None);
  EXPECT_EQ(PlayingStyles::ParsePlayer("teleporter"), Style::None);
  EXPECT_EQ(PlayingStyles::Serialize(Style::None), "none");
}

TEST(PlayingStylesParseTest, EveryComCardRoundTripsAndUnknownsAreIgnored) {
  for (int i = 0; i < PlayingStyles::comCount; i++) {
    const ComMask single = static_cast<ComMask>(PlayingStyles::GetComAt(i));
    EXPECT_EQ(PlayingStyles::ParseCom(PlayingStyles::SerializeCom(single)), single);
  }
  const ComMask all = AllCards();
  EXPECT_EQ(PlayingStyles::ParseCom(PlayingStyles::SerializeCom(all)), all);
  EXPECT_EQ(PlayingStyles::ParseCom("trickster, Mazing Run"),
            static_cast<ComMask>(Com::Trickster) | static_cast<ComMask>(Com::MazingRun));
  EXPECT_EQ(PlayingStyles::ParseCom("none"), PlayingStyles::comMaskNone);
  EXPECT_EQ(PlayingStyles::ParseCom(""), PlayingStyles::comMaskNone);
  EXPECT_EQ(PlayingStyles::SerializeCom(PlayingStyles::comMaskNone), "");
}

TEST(PlayingStylesInferTest, IsDeterministicLegalForTheRoleAndVaried) {
  PlayerData flat;  // every rating 0.6: the imported squads look like this
  for (e_PlayerRole role : allRoles) {
    std::set<Style> seen;
    for (int id = 1; id <= 80; id++) {
      const Style style = PlayingStyles::InferPlayer(id, role, flat);
      EXPECT_EQ(style, PlayingStyles::InferPlayer(id, role, flat));
      EXPECT_TRUE(PlayingStyles::SuitsRole(style, role))
          << PlayingStyles::Serialize(style) << " on role " << role;
      seen.insert(style);
    }
    // At least "none" plus two real styles per position over a squad's worth.
    EXPECT_GE(seen.size(), 3u) << "role " << role;
  }
}

TEST(PlayingStylesInferTest, GoalkeepersGetKeeperStylesAndNoCards) {
  PlayerData keeper;
  keeper.ToggleRole(e_PlayerRole_GK);
  bool sawKeeperStyle = false;
  for (int id = 1; id <= 40; id++) {
    const Style style = PlayingStyles::InferPlayer(id, e_PlayerRole_GK, keeper);
    sawKeeperStyle |= style != Style::None;
    // Whatever his style says, a listed keeper is dealt no cards.
    EXPECT_EQ(PlayingStyles::InferCom(id, style, keeper), PlayingStyles::comMaskNone);
    EXPECT_EQ(PlayingStyles::InferCom(id, Style::None, keeper), PlayingStyles::comMaskNone);
  }
  EXPECT_TRUE(sawKeeperStyle);
}

TEST(PlayingStylesInferTest, ComCardsAreDeterministicAndCappedAtFive) {
  PlayerData flat;
  int dealt = 0;
  for (int id = 1; id <= 80; id++) {
    const Style style = PlayingStyles::InferPlayer(id, e_PlayerRole_CM, flat);
    const ComMask cards = PlayingStyles::InferCom(id, style, flat);
    EXPECT_EQ(cards, PlayingStyles::InferCom(id, style, flat));
    int count = 0;
    for (int i = 0; i < PlayingStyles::comCount; i++)
      count += PlayingStyles::Has(cards, PlayingStyles::GetComAt(i)) ? 1 : 0;
    EXPECT_LE(count, 5);
    dealt += count;
  }
  EXPECT_GT(dealt, 0);
}

// --- Every style changes something a plain player does not ---

TEST(PlayingStylesEffectTest, EveryOutfieldStyleMovesADecision) {
  for (int i = 1; i < styleCount; i++) {
    const Style style = static_cast<Style>(i);
    const ComMask none = PlayingStyles::comMaskNone;
    const bool differs =
        PlayingStyles::GetDepthOffset(style, true) != 0.0f ||
        PlayingStyles::GetDepthOffset(style, false) != 0.0f ||
        PlayingStyles::GetWidthOffset(style, true) != 0.0f ||
        PlayingStyles::GetPoacherTargetX(style, 0.0f, -20.0f, 1) != 0.0f ||
        PlayingStyles::GetAttackingRunAffinity(style, none) != 0.5f ||
        PlayingStyles::GetPressingDistanceBias(style) != 0.0f ||
        PlayingStyles::GetShotAppetite(style, none) != 1.0f ||
        PlayingStyles::GetSpaceRatingWeight(style, 0.3f) != 0.3f ||
        PlayingStyles::GetDribbleDrive(style, none, 1.0f) != 1.0f ||
        PlayingStyles::GetKeeperComeOutBias(style) != 1.0f;
    EXPECT_TRUE(differs) << PlayingStyles::Serialize(style) << " is inert";
  }
}

TEST(PlayingStylesEffectTest, GoalPoacherSitsOnsideOfTheOffsideLine) {
  // Own goal is at side * pitchHalfW, so for side +1 "onside" is the +x side.
  const float line = -20.0f;
  const float target = PlayingStyles::GetPoacherTargetX(Style::GoalPoacher, -5.0f, line, 1);
  EXPECT_NEAR(target, line + PlayingStyles::poacherOffsideCushion, 1e-5f);
  EXPECT_NEAR(PlayingStyles::GetPoacherTargetX(Style::GoalPoacher, 5.0f, 20.0f, -1),
              20.0f - PlayingStyles::poacherOffsideCushion, 1e-5f);
  EXPECT_FLOAT_EQ(PlayingStyles::GetPoacherTargetX(Style::FoxInTheBox, -5.0f, line, 1), -5.0f);
}

TEST(PlayingStylesEffectTest, StylesPullPlayersWhereTheyBelong) {
  EXPECT_GT(PlayingStyles::GetDepthOffset(Style::HolePlayer, true), 0.0f);
  EXPECT_LT(PlayingStyles::GetDepthOffset(Style::AnchorMan, true), 0.0f);
  EXPECT_LT(PlayingStyles::GetWidthOffset(Style::RoamingFlank, true), 0.0f);
  EXPECT_GT(PlayingStyles::GetWidthOffset(Style::CrossSpecialist, true), 0.0f);
  // Box-to-box swings with possession, the destroyer steps up only to press.
  EXPECT_GT(PlayingStyles::GetDepthOffset(Style::BoxToBox, true), 0.0f);
  EXPECT_LT(PlayingStyles::GetDepthOffset(Style::BoxToBox, false), 0.0f);
  EXPECT_GT(PlayingStyles::GetDepthOffset(Style::TheDestroyer, false), 0.0f);
  EXPECT_FLOAT_EQ(PlayingStyles::GetDepthOffset(Style::TheDestroyer, true), 0.0f);
  EXPECT_LT(PlayingStyles::GetPressingDistanceBias(Style::TheDestroyer),
            PlayingStyles::GetPressingDistanceBias(Style::ClassicNo10));
  EXPECT_FLOAT_EQ(PlayingStyles::GetAttackingRunAffinity(Style::AnchorMan, 0), 0.0f);
  EXPECT_GT(PlayingStyles::GetAttackingRunAffinity(Style::HolePlayer, 0), 0.5f);
}

TEST(PlayingStylesEffectTest, KeeperStylesBendHowFarHeComes) {
  EXPECT_GT(PlayingStyles::GetKeeperComeOutBias(Style::OffensiveGoalkeeper), 1.0f);
  EXPECT_LT(PlayingStyles::GetKeeperComeOutBias(Style::DefensiveGoalkeeper), 1.0f);
  EXPECT_FLOAT_EQ(PlayingStyles::GetKeeperComeOutBias(Style::GoalPoacher), 1.0f);
}

// --- Every COM card moves a decision, card set vs not ---

TEST(ComStylesEffectTest, TricksterAndMazingRunTakeOpponentsOn) {
  const ComMask trickster = static_cast<ComMask>(Com::Trickster);
  const ComMask mazing = static_cast<ComMask>(Com::MazingRun);
  EXPECT_LT(PlayingStyles::GetOpponentRepel(Style::None, trickster, 2.0f), 2.0f);
  EXPECT_LT(PlayingStyles::GetOpponentRepel(Style::None, mazing, 2.0f), 2.0f);
  EXPECT_FLOAT_EQ(PlayingStyles::GetOpponentRepel(Style::None, 0, 2.0f), 2.0f);
  // Mazing Run keeps driving at goal; the trickster does not care where.
  EXPECT_GT(PlayingStyles::GetDribbleDrive(Style::None, mazing, 1.0f), 1.0f);
  EXPECT_FLOAT_EQ(PlayingStyles::GetDribbleDrive(Style::None, trickster, 1.0f), 1.0f);
}

TEST(ComStylesEffectTest, SpeedingBulletBurstsAndVolunteersForRuns) {
  const ComMask bullet = static_cast<ComMask>(Com::SpeedingBullet);
  EXPECT_GT(PlayingStyles::GetDribbleVelocity(Style::None, bullet, 5.0f), 5.0f);
  EXPECT_LE(PlayingStyles::GetDribbleVelocity(Style::None, bullet, 7.9f), sprintVelocity);
  EXPECT_FLOAT_EQ(PlayingStyles::GetDribbleVelocity(Style::None, 0, 5.0f), 5.0f);
  EXPECT_GT(PlayingStyles::GetAttackingRunAffinity(Style::None, bullet),
            PlayingStyles::GetAttackingRunAffinity(Style::None, 0));
  // A target man holds it up whatever the card says.
  EXPECT_LE(PlayingStyles::GetDribbleVelocity(Style::TargetMan, 0, 8.0f), dribbleVelocity);
}

TEST(ComStylesEffectTest, IncisiveRunCutsInsideForTheThroughBallAndTheShot) {
  const ComMask incisive = static_cast<ComMask>(Com::IncisiveRun);
  EXPECT_GT(PlayingStyles::GetDribbleCenterPull(Style::None, incisive, 0.5f), 0.5f);
  EXPECT_FLOAT_EQ(PlayingStyles::GetDribbleCenterPull(Style::None, 0, 0.5f), 0.5f);
  EXPECT_GT(PlayingStyles::GetPassTypeBias(Style::None, incisive, e_FunctionType_LongPass), 1.0f);
  EXPECT_GT(PlayingStyles::GetShotAppetite(Style::None, incisive), 1.0f);
  // The cross specialist stays wide instead.
  EXPECT_LT(PlayingStyles::GetDribbleCenterPull(Style::CrossSpecialist, 0, 0.5f), 0.5f);
}

TEST(ComStylesEffectTest, LongBallExpertAndEarlyCrossPreferTheirPass) {
  const ComMask longBall = static_cast<ComMask>(Com::LongBallExpert);
  const ComMask earlyCross = static_cast<ComMask>(Com::EarlyCross);
  EXPECT_GT(PlayingStyles::GetPassTypeBias(Style::None, longBall, e_FunctionType_LongPass), 1.0f);
  EXPECT_GT(PlayingStyles::GetPassTypeBias(Style::None, longBall, e_FunctionType_HighPass), 1.0f);
  EXPECT_LT(PlayingStyles::GetPassTypeBias(Style::None, longBall, e_FunctionType_ShortPass), 1.0f);
  EXPECT_GT(PlayingStyles::GetPassTypeBias(Style::None, earlyCross, e_FunctionType_HighPass), 1.0f);
  EXPECT_FLOAT_EQ(PlayingStyles::GetPassTypeBias(Style::None, earlyCross, e_FunctionType_ShortPass),
                  1.0f);
  for (e_FunctionType type : {e_FunctionType_ShortPass, e_FunctionType_LongPass, e_FunctionType_HighPass})
    EXPECT_FLOAT_EQ(PlayingStyles::GetPassTypeBias(Style::None, 0, type), 1.0f);
  // Stacked biases stay within a sane band.
  EXPECT_LE(PlayingStyles::GetPassTypeBias(Style::CrossSpecialist, AllCards(), e_FunctionType_HighPass),
            1.6f);
}

TEST(ComStylesEffectTest, LongRangerShootsFromFurtherOut) {
  const ComMask ranger = static_cast<ComMask>(Com::LongRanger);
  EXPECT_GT(PlayingStyles::GetShootingRangeBonus(Style::None, ranger), 0.0f);
  EXPECT_GT(PlayingStyles::GetShotAppetite(Style::None, ranger), 1.0f);
  EXPECT_FLOAT_EQ(PlayingStyles::GetShootingRangeBonus(Style::None, 0), 0.0f);
  // Appetite and range stay bounded with everything stacked on a poacher.
  EXPECT_LE(PlayingStyles::GetShotAppetite(Style::FoxInTheBox, AllCards()), 2.0f);
  EXPECT_LE(PlayingStyles::GetShootingRangeBonus(Style::None, AllCards()), 14.0f);
  EXPECT_GE(PlayingStyles::GetShotAppetite(Style::AnchorMan, 0), 0.55f);
}

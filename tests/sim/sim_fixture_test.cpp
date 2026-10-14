#include <gtest/gtest.h>

#include "data/teamdata.hpp"
#include "simcontext.hpp"
#include "utils.hpp"

// The synthetic fixture both sim tests play on: two squads of 16, a 4-4-2,
// every tactics slider at the engine's base value, flat 0.65 ratings.
namespace {
constexpr int SQUAD_SIZE = 16;
constexpr int STARTERS = 11;
constexpr float FIXTURE_STAT = 0.65f;      // every rating (and base_stat) in teams.sql
constexpr float OUTFIELD_GK_STAT = 0.20f;  // PES rates outfield goalkeeping at the floor
constexpr float FIXTURE_AGE = 25.0f;
const e_PlayerRole kStartingRoles[STARTERS] = {e_PlayerRole_GK, e_PlayerRole_LB, e_PlayerRole_CB, e_PlayerRole_CB,
                                               e_PlayerRole_RB, e_PlayerRole_LM, e_PlayerRole_CM, e_PlayerRole_AM,
                                               e_PlayerRole_RM, e_PlayerRole_CF, e_PlayerRole_CF};
// TeamAIController's baseTeamTactics: the 14 position sliders and the 2 dribble ones.
const std::pair<const char*, float> kBaseSliders[] = {
    {"position_offense_depth_factor", 0.9f}, {"position_defense_depth_factor", 0.75f},
    {"position_offense_width_factor", 0.9f}, {"position_defense_width_factor", 0.8f},
    {"position_offense_ownhalf_factor", 0.52f}, {"position_defense_ownhalf_factor", 0.54f},
    {"position_offense_midfieldfocus", 0.6f}, {"position_defense_midfieldfocus", 0.5f},
    {"position_offense_midfieldfocus_strength", 0.35f}, {"position_defense_midfieldfocus_strength", 0.35f},
    {"position_offense_sidefocus_strength", 0.1f}, {"position_defense_sidefocus_strength", 0.4f},
    {"position_offense_microfocus_strength", 0.7f}, {"position_defense_microfocus_strength", 0.8f},
    {"dribble_offensiveness", 0.5f}, {"dribble_centermagnet", 0.5f},
};

class Fixture : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(db.LoadSqlScript(GF_SOURCE_DIR "/assets/fixtures/teams.sql"));
    SetDB(&db);
  }
  void TearDown() override { SetDB(nullptr); }
  blunted::Database db;
};

}  // namespace

TEST_F(Fixture, BothSquadsLoad) {
  TeamData home(1), away(2);
  EXPECT_EQ(home.GetName(), "Home XI");
  EXPECT_EQ(away.GetName(), "Away XI");
  EXPECT_EQ(home.GetShortName(), "HOM");
  EXPECT_EQ(away.GetShortName(), "AWA");
  EXPECT_EQ(home.GetPlayerNum(), SQUAD_SIZE);
  EXPECT_EQ(away.GetPlayerNum(), SQUAD_SIZE);
}

TEST_F(Fixture, FourFourTwoInRoleOrder) {
  TeamData home(1);
  for (int i = 0; i < STARTERS; ++i) {
    EXPECT_EQ(home.GetFormationEntry(i).role, kStartingRoles[i]) << "slot " << i;
    EXPECT_EQ(home.GetPlayerData(i)->GetRoles().at(0), kStartingRoles[i]) << "player " << i;
  }
}

TEST_F(Fixture, TacticsAtEngineBase) {
  TeamData home(1);
  for (const auto& [key, value] : kBaseSliders)
    EXPECT_FLOAT_EQ(home.GetTactics().userProperties.GetReal(key, -1.0f), value) << key;
}

// Profile ratings pass through GF's development curve (CalculateStat) on load.
TEST_F(Fixture, KeepersKeepOutfieldersDoNot) {
  TeamData home(1);
  const float rated = CalculateStat(FIXTURE_STAT, FIXTURE_STAT, FIXTURE_AGE, e_DevelopmentCurveType_Normal);
  const float floor = CalculateStat(FIXTURE_STAT, OUTFIELD_GK_STAT, FIXTURE_AGE, e_DevelopmentCurveType_Normal);
  // The database holds "0.650000" as text, parsed in double: float-rounding apart.
  constexpr float PARSE_TOLERANCE = 1e-5f;
  EXPECT_NEAR(home.GetPlayerData(0)->GetStat("gk_reflexes"), rated, PARSE_TOLERANCE);
  EXPECT_NEAR(home.GetPlayerData(1)->GetStat("gk_reflexes"), floor, PARSE_TOLERANCE);
  EXPECT_NEAR(home.GetPlayerData(1)->GetStat("technical_weakfootaccuracy"), rated, PARSE_TOLERANCE);
  EXPECT_GT(home.GetPlayerData(0)->GetStat("gk_reflexes"), home.GetPlayerData(1)->GetStat("gk_reflexes"));
}

// Pass-failure breakdown: why do passes die?
//
// The deny-list counts THAT a pass went wrong; these counters learn WHY it went
// wrong, so tuning effort lands on the dominant failure mode instead of a guess:
//   - intercept: a pending pass met by the other team (already implied by the
//     deny-list, now counted separately),
//   - out: a pending pass still in flight when the ball left the pitch,
//   - trap: a trap/control touch by a player who did not have possession, i.e.
//     the pass arrived but the receiver failed to kill it.
//
// The engine guards the increments with #ifndef NDEBUG, and this test target
// undefines NDEBUG (see tests/CMakeLists.txt) so the guarded behaviour is what
// gets exercised here.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "base/properties.hpp"
#include "data/matchdata.hpp"
#include "sqlite3.h"
#include "utils/database.hpp"

using blunted::Database;
using blunted::Properties;

namespace {
Database g_testDatabase;
Properties g_testConfiguration;
}  // namespace

#include "main.hpp"

// The data layer reaches into main.hpp globals (GetDB, save dir); supply the
// same stubs the league integration test does.
Database* GetDB() {
  return &g_testDatabase;
}

Properties* GetConfiguration() {
  return &g_testConfiguration;
}

std::string GetActiveSaveDirectory() {
  return "";
}

void SetActiveSaveDirectory(const std::string& dir) {
  (void)dir;
}

// gamedefines.cpp (linked for GetRoleFromString) calls Verbose().
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

// Enough of a database for TeamData(1) and TeamData(2) to load a full
// 11-player squad each.
void EnsureTestDatabase() {
  static bool loaded = false;
  if (loaded) return;
  ASSERT_TRUE(GetDB()->Load(":memory:"));

  GetDB()->Query(
      "CREATE TABLE leagues(id INTEGER PRIMARY KEY, name TEXT);"
      "CREATE TABLE teams(id INTEGER PRIMARY KEY, league_id INTEGER, name TEXT, logo_url TEXT, "
      "kit_url TEXT, formation_xml TEXT, formation_factory_xml TEXT, tactics_xml TEXT, "
      "tactics_factory_xml TEXT, shortname TEXT, color1 TEXT, color2 TEXT);"
      "CREATE TABLE players(id INTEGER PRIMARY KEY, team_id INTEGER, nationalteam_id INTEGER, "
      "firstname TEXT, lastname TEXT, role TEXT, age INTEGER, base_stat INTEGER, profile_xml TEXT, "
      "skincolor INTEGER, hairstyle TEXT, haircolor TEXT, height REAL, weight REAL, "
      "formationorder INTEGER, nationalteamformationorder INTEGER);");

  GetDB()->Query("INSERT INTO leagues(id, name) VALUES (1, 'Premier Test League');");

  const char* formationXml =
      "<p1><position>-1.00,0.00</position><role>GK</role></p1>"
      "<p2><position>-0.60,0.30</position><role>CB</role></p2>"
      "<p3><position>-0.60,-0.30</position><role>CB</role></p3>"
      "<p4><position>-0.40,0.70</position><role>LB</role></p4>"
      "<p5><position>-0.40,-0.70</position><role>RB</role></p5>"
      "<p6><position>-0.25,0.00</position><role>DM</role></p6>"
      "<p7><position>0.05,0.25</position><role>CM</role></p7>"
      "<p8><position>0.05,-0.25</position><role>CM</role></p8>"
      "<p9><position>0.35,0.60</position><role>LM</role></p9>"
      "<p10><position>0.35,-0.60</position><role>RM</role></p10>"
      "<p11><position>0.75,0.00</position><role>CF</role></p11>";
  const char* tacticsXml =
      "<counter_attack>0.1</counter_attack>"
      "<support_distance>0.2</support_distance>"
      "<philosophy>tiki-taka</philosophy>";
  const char* factoryTacticsXml =
      "<counter_attack>0.5</counter_attack>"
      "<support_distance>0.5</support_distance>"
      "<team_pressure>0.5</team_pressure>";

  for (int teamId = 1; teamId <= 2; ++teamId) {
    GetDB()->Query(
        "INSERT INTO teams(id, league_id, name, logo_url, kit_url, formation_xml, "
        "formation_factory_xml, tactics_xml, tactics_factory_xml, shortname, color1, color2) "
        "VALUES (" +
        std::to_string(teamId) +
        ", 1, 'Test FC', 'images_teams/test/logo.png', 'images_teams/test/kit', '" +
        formationXml + "', '" + formationXml + "', '" + tacticsXml + "', '" + factoryTacticsXml +
        "', 'TST', '200,20,20', '255,255,255');");
  }

  const char* roles[] = {"GK", "CB", "CB", "LB", "RB", "DM", "CM", "CM", "LM", "RM", "CF"};
  const char* positionsXml[] = {"-1.00,0.00", "-0.60,0.30",  "-0.60,-0.30", "-0.40,0.70",
                                "-0.40,-0.70", "-0.25,0.00", "0.05,0.25",  "0.05,-0.25",
                                "0.35,0.60",   "0.35,-0.60", "0.75,0.00"};
  std::string profileXml =
      "<physical_balance>0.66</physical_balance>"
      "<technical_ballcontrol>0.7</technical_ballcontrol>"
      "<technical_shortpass>0.72</technical_shortpass>";

  for (int slot = 0; slot < 11; ++slot) {
    for (int teamId = 1; teamId <= 2; ++teamId) {
      const int playerId = teamId * 100 + slot;
      GetDB()->Query(
          "INSERT INTO players(id, team_id, nationalteam_id, firstname, lastname, role, age, "
          "base_stat, profile_xml, skincolor, hairstyle, haircolor, height, weight, "
          "formationorder, nationalteamformationorder) VALUES (" +
          std::to_string(playerId) + ", " + std::to_string(teamId) + ", NULL, 'Test', 'P" +
          std::to_string(slot) + "', '" + roles[slot] + "', 25, 55, '" + profileXml +
          "', 1, 'short01', 'darkblonde', 1.82, 76.0, " + std::to_string(slot) + ", " +
          std::to_string(slot) + ");");
    }
  }

  loaded = true;
}

class PassFailure : public ::testing::Test {
protected:
  void SetUp() override {
    EnsureTestDatabase();
    matchData = std::make_unique<MatchData>(1, 2);
  }

  std::unique_ptr<MatchData> matchData;
};

// A shot ends the passing sequence: the previous pass can no longer be met by
// the opposition and miscounted as a giveaway.
TEST_F(PassFailure, PendingPassClearedOnShotAndSetPiece) {
  matchData->AddPassAttempt(0);
  matchData->AddShot(0);
  matchData->RecordBallTouch(1);
  EXPECT_EQ(matchData->GetBadPassToOpponent(0), 0);

  // Same for a restart: PrepareSetPiece resets the pending pass (the referee
  // calls ResetPendingPass there), so a touch after it counts nothing either.
  matchData->AddPassAttempt(0);
  matchData->ResetPendingPass();
  matchData->RecordBallTouch(1);
  EXPECT_EQ(matchData->GetBadPassToOpponent(0), 0);
}

// A pending pass met by the other team is an interception by the passer;
// a pending pass that sails out of play is an out-of-bounds failure. The two
// must stay distinguishable, and neither may leak into the other.
TEST_F(PassFailure, InterceptVsOutOfBoundsDistinguished) {
  matchData->AddPassAttempt(0);
  matchData->RecordBallTouch(1);  // team 1 picks it off
  EXPECT_EQ(matchData->GetPassFailIntercept(0), 1);
  EXPECT_EQ(matchData->GetPassFailOutOfBounds(0), 0);
  EXPECT_EQ(matchData->GetPassFailIntercept(1), 0);
  EXPECT_EQ(matchData->GetPassFailOutOfBounds(1), 0);

  matchData->AddPassAttempt(1);
  matchData->FailPendingPassOutOfBounds();  // team 1's pass leaves the pitch
  EXPECT_EQ(matchData->GetPassFailOutOfBounds(1), 1);
  EXPECT_EQ(matchData->GetPassFailIntercept(1), 0);

  // Once the ball is out, the sequence is over: nothing further counts.
  matchData->FailPendingPassOutOfBounds();
  EXPECT_EQ(matchData->GetPassFailOutOfBounds(1), 1);
}

// A trap (or possession-less ball control) that fails to kill the pass counts
// against the receiving team, independent of the completion bookkeeping that
// RecordBallTouch performs right after it.
TEST_F(PassFailure, BadTrapCountsAgainstTheReceiver) {
  matchData->AddPassFailBadTrap(0);
  EXPECT_EQ(matchData->GetPassFailBadTrap(0), 1);
  EXPECT_EQ(matchData->GetPassFailBadTrap(1), 0);

  // A receiver's bobble does not consume the pending pass either: his next
  // touch still completes the pass, and no giveaway is recorded.
  matchData->AddPassAttempt(0);
  matchData->AddPassFailBadTrap(0);
  matchData->RecordBallTouch(0);
  EXPECT_EQ(matchData->GetPassesCompleted(0), 1);
  EXPECT_EQ(matchData->GetBadPassToOpponent(0), 0);
  EXPECT_EQ(matchData->GetPassFailIntercept(0), 0);

}

// A giveaway in the team's own third is only a "bad play" if it actually
// hurts: the opposition must turn it into a shot within 12 seconds. A
// giveaway that leads to nothing (or to the giver-away regaining the ball
// first) must not be counted at all.
TEST_F(PassFailure, OwnThirdGiveawayOnlyCountsIfShotFollows) {
  matchData->AddPassAttempt(0);
  matchData->SetPendingPassOwnThird();
  matchData->RecordBallTouch(1);  // team 0 gives it away in its own third
  EXPECT_EQ(matchData->GetOwnThirdGiveaway(0), 0);  // not yet - no shot

  // The intercepting team shoots inside the window: now the giveaway counts.
  matchData->AddShot(1);
  EXPECT_EQ(matchData->GetOwnThirdGiveaway(0), 1);
}

// Twelve seconds pass with no shot (the window is inclusive, so the shot must
// land strictly inside it): the chance came to nothing and stays uncounted.
TEST_F(PassFailure, OwnThirdGiveawayExpiresWithoutShot) {
  matchData->AddPassAttempt(0);
  matchData->SetPendingPassOwnThird();
  matchData->RecordBallTouch(1);
  EXPECT_EQ(matchData->GetOwnThirdGiveaway(0), 0);

  for (int tick = 0; tick < 1210; ++tick) matchData->AddPossessionTime_10ms(1);
  matchData->AddShot(1);
  EXPECT_EQ(matchData->GetOwnThirdGiveaway(0), 0);
}

TEST_F(PassFailure, OwnThirdGiveawayClearedWhenTeamRegainsBall) {
  matchData->AddPassAttempt(0);
  matchData->SetPendingPassOwnThird();
  matchData->RecordBallTouch(1);
  EXPECT_EQ(matchData->GetOwnThirdGiveaway(0), 0);

  // Team 0 wins the ball straight back: no danger ever materialised.
  matchData->RecordBallTouch(0);
  matchData->AddShot(1);
  EXPECT_EQ(matchData->GetOwnThirdGiveaway(0), 0);
}

// The pending giveaway must never leak across a restart either: a set piece
// wipes it just like AddShot or a regain does.
TEST_F(PassFailure, OwnThirdGiveawayClearedOnResetPendingPass) {
  matchData->AddPassAttempt(0);
  matchData->SetPendingPassOwnThird();
  matchData->RecordBallTouch(1);
  EXPECT_EQ(matchData->GetOwnThirdGiveaway(0), 0);

  matchData->ResetPendingPass();
  matchData->AddShot(1);
  EXPECT_EQ(matchData->GetOwnThirdGiveaway(0), 0);
}
TEST_F(PassFailure, GoalkeeperCatchClosesPendingPass) {
  matchData->AddPassAttempt(0);
  matchData->RecordPassGoalkeeperCatch();
  EXPECT_EQ(matchData->GetPassGoalkeeperCatch(0), 1);
  EXPECT_EQ(matchData->GetPassGoalkeeperCatch(1), 0);

  // The catch ends the pass, so a later opponent touch is not an intercept.
  matchData->RecordBallTouch(1);
  EXPECT_EQ(matchData->GetPassFailIntercept(0), 0);
}

TEST_F(PassFailure, RestartCountsAndClosesPendingPass) {
  matchData->AddPassAttempt(1);
  matchData->RecordPassRestart();
  EXPECT_EQ(matchData->GetPassRestart(0), 0);
  EXPECT_EQ(matchData->GetPassRestart(1), 1);

  matchData->RecordBallTouch(0);
  EXPECT_EQ(matchData->GetPassFailIntercept(1), 0);
}

TEST_F(PassFailure, AttributionIgnoresClosedPass) {
  matchData->RecordPassGoalkeeperCatch();
  matchData->RecordPassRestart();
  EXPECT_EQ(matchData->GetPassGoalkeeperCatch(0), 0);
  EXPECT_EQ(matchData->GetPassGoalkeeperCatch(1), 0);
  EXPECT_EQ(matchData->GetPassRestart(0), 0);
  EXPECT_EQ(matchData->GetPassRestart(1), 0);
}

// Clean-completion tracking: a pass that completes starts a 1.5s window
// (MatchData::cleanCompletionWindow_ms). Whichever team is still the ball's
// last-touch team once the window elapses settles the check - CLEAN for the
// completing team, SCRAPPY otherwise. Unlike the rest of this file these
// counters are not behind #ifndef NDEBUG: they feed the [balance-passing]
// accuracy line in release builds too.
TEST_F(PassFailure, CleanCompletionResolvesCleanWhenSameTeamHoldsPastWindow) {
  matchData->AddPassAttempt(0);
  matchData->RecordBallTouch(0);  // pass completes for team 0
  EXPECT_EQ(matchData->GetPendingCleanCheckCount(), 1);
  EXPECT_EQ(matchData->GetCleanCompletions(0), 0);

  // Advance past the 1.5s window with no other touches, then let team 0
  // touch the ball again - it is still the last team to have touched it.
  for (int tick = 0; tick < 160; ++tick) matchData->AddPossessionTime_10ms(0);
  matchData->RecordBallTouch(0);
  EXPECT_EQ(matchData->GetCleanCompletions(0), 1);
  EXPECT_EQ(matchData->GetScrappyCompletions(0), 0);
  EXPECT_EQ(matchData->GetPendingCleanCheckCount(), 0);
}

TEST_F(PassFailure, CleanCompletionResolvesScrappyWhenOpponentTouchesAfterWindow) {
  matchData->AddPassAttempt(0);
  matchData->RecordBallTouch(0);  // pass completes for team 0

  for (int tick = 0; tick < 160; ++tick) matchData->AddPossessionTime_10ms(0);
  matchData->RecordBallTouch(1);  // opponent is first to touch it after the window
  EXPECT_EQ(matchData->GetScrappyCompletions(0), 1);
  EXPECT_EQ(matchData->GetCleanCompletions(0), 0);
  EXPECT_EQ(matchData->GetPendingCleanCheckCount(), 0);
}

// An opponent touch inside the 1.5s window must not pre-resolve the check:
// the pass already completed, so only the state of play once the window has
// fully elapsed decides clean vs scrappy.
TEST_F(PassFailure, CleanCompletionWindowDoesNotResolveEarly) {
  matchData->AddPassAttempt(0);
  matchData->RecordBallTouch(0);  // pass completes for team 0

  // Well inside the window: the opponent gets a touch on it...
  for (int tick = 0; tick < 50; ++tick) matchData->AddPossessionTime_10ms(0);
  matchData->RecordBallTouch(1);
  EXPECT_EQ(matchData->GetPendingCleanCheckCount(), 1);
  EXPECT_EQ(matchData->GetCleanCompletions(0), 0);
  EXPECT_EQ(matchData->GetScrappyCompletions(0), 0);

  // ...but team 0 wins it straight back and still holds it once the window
  // elapses: the reception counts as clean, not scrappy.
  matchData->RecordBallTouch(0);
  for (int tick = 0; tick < 110; ++tick) matchData->AddPossessionTime_10ms(0);
  matchData->RecordBallTouch(0);
  EXPECT_EQ(matchData->GetCleanCompletions(0), 1);
  EXPECT_EQ(matchData->GetScrappyCompletions(0), 0);
}

// The ball going out of play is an out event too: it must resolve an
// expired window even when nobody else touches the ball.
TEST_F(PassFailure, CleanCompletionResolvesOnOutOfBoundsEvent) {
  matchData->AddPassAttempt(0);
  matchData->RecordBallTouch(0);  // pass completes for team 0

  for (int tick = 0; tick < 160; ++tick) matchData->AddPossessionTime_10ms(0);
  matchData->AddPassAttempt(0);
  matchData->FailPendingPassOutOfBounds();
  EXPECT_EQ(matchData->GetCleanCompletions(0), 1);
  EXPECT_EQ(matchData->GetPendingCleanCheckCount(), 0);
}

TEST_F(PassFailure, CleanCompletionCountersResetPerMatch) {
  matchData->AddPassAttempt(0);
  matchData->RecordBallTouch(0);
  for (int tick = 0; tick < 160; ++tick) matchData->AddPossessionTime_10ms(0);
  matchData->RecordBallTouch(0);
  EXPECT_EQ(matchData->GetCleanCompletions(0), 1);

  // A new match starts with a fresh MatchData: nothing carries over, and no
  // clean check is left pending from a previous game.
  MatchData fresh(1, 2);
  EXPECT_EQ(fresh.GetCleanCompletions(0), 0);
  EXPECT_EQ(fresh.GetCleanCompletions(1), 0);
  EXPECT_EQ(fresh.GetScrappyCompletions(0), 0);
  EXPECT_EQ(fresh.GetScrappyCompletions(1), 0);
  EXPECT_EQ(fresh.GetPendingCleanCheckCount(), 0);
}


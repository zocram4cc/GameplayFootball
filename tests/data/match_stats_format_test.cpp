// Golden tests for the end-of-match stats document's wikitext rendering
// (src/data/matchstatsexport.cpp): the Wikipedia {{footballbox}} shape, field
// set and order are a frozen interface shared with tools/rigbot-control-panel.

#include <gtest/gtest.h>

#include "data/matchstatsexport.hpp"

namespace {

MatchStatsDocument BaseDocument() {
  MatchStatsDocument document;
  document.matchId = "2026-09-16-11-13-193000";
  document.date = "2026-09-16";
  document.competition = "Friendly";
  document.stadium = "st017";
  document.durationMinutes = 25;

  MatchStatsTeam home;
  home.databaseID = 11;
  home.name = "HDG";
  MatchStatsTeam away;
  away.databaseID = 13;
  away.name = "OOO";

  document.teams = {home, away};
  return document;
}

}  // namespace

TEST(MatchStatsFormatTest, NoGoalsMatchLeavesGoalsParamsEmpty) {
  MatchStatsDocument document = BaseDocument();
  document.teams[0].score = 0;
  document.teams[1].score = 0;

  const std::string wiki = FormatFootballboxWikitext(document);
  EXPECT_EQ(wiki,
           "{{footballbox\n"
           "|date       = 16 September 2026\n"
           "|time       =\n"
           "|team1      = HDG\n"
           "|score      = 0 \xe2\x80\x93 0\n"
           "|team2      = OOO\n"
           "|goals1     =\n"
           "|goals2     =\n"
           "|stadium    = st017\n"
           "|attendance =\n"
           "|referee    =\n"
           "|report     =\n"
           "}}\n");
}

TEST(MatchStatsFormatTest, PenaltyGoalIsAnnotatedPen) {
  MatchStatsDocument document = BaseDocument();
  document.teams[0].score = 1;
  document.teams[1].score = 0;
  document.teams[0].goals.push_back({71, "A. Doe", /*ownGoal=*/false, /*penalty=*/true});

  const std::string wiki = FormatFootballboxWikitext(document);
  EXPECT_NE(wiki.find("|goals1     = A. Doe {{goal|71|pen}}\n"), std::string::npos);
  EXPECT_NE(wiki.find("|score      = 1 \xe2\x80\x93 0\n"), std::string::npos);
}

TEST(MatchStatsFormatTest, OwnGoalIsAnnotatedOg) {
  MatchStatsDocument document = BaseDocument();
  document.teams[0].score = 0;
  document.teams[1].score = 1;
  // Credited to the away team's score; the scorer is a home player.
  document.teams[1].goals.push_back({12, "R. Kelly", /*ownGoal=*/true, /*penalty=*/false});

  const std::string wiki = FormatFootballboxWikitext(document);
  EXPECT_NE(wiki.find("|goals2     = R. Kelly {{goal|12|o.g.}}\n"), std::string::npos);
}

TEST(MatchStatsFormatTest, GoalAndCardForSamePlayerMergeOnOneLine) {
  MatchStatsDocument document = BaseDocument();
  document.teams[0].score = 1;
  document.teams[0].goals.push_back({34, "J. Smith", false, false});
  document.teams[0].cards.push_back({60, "J. Smith", MatchCardEvent::Yellow});

  const std::string wiki = FormatFootballboxWikitext(document);
  EXPECT_NE(wiki.find("|goals1     = J. Smith {{goal|34}} {{yel|60}}\n"), std::string::npos);
}

TEST(MatchStatsFormatTest, SecondYellowRendersAsSentOffWithPriorCount) {
  MatchStatsDocument document = BaseDocument();
  document.teams[0].cards.push_back({40, "T. Owen", MatchCardEvent::Yellow});
  document.teams[0].cards.push_back({80, "T. Owen", MatchCardEvent::Red});

  const std::string wiki = FormatFootballboxWikitext(document);
  EXPECT_NE(wiki.find("|goals1     = T. Owen {{yel|40}} {{sent off|1|80}}\n"), std::string::npos);
}

TEST(MatchStatsFormatTest, JsonHasNullAttendanceRefereeAndPassAccuracyWhenUnrecorded) {
  MatchStatsDocument document = BaseDocument();
  // passes left at 0: accuracy is not computed, so it stays absent (null).
  const std::string json = SerialiseMatchStatsJson(document);
  EXPECT_NE(json.find("\"attendance\": null"), std::string::npos);
  EXPECT_NE(json.find("\"referee\": null"), std::string::npos);
  EXPECT_NE(json.find("\"passAccuracyPercent\":null"), std::string::npos);
  EXPECT_NE(json.find("\"format\": \"gf-match-stats\""), std::string::npos);
  EXPECT_NE(json.find("\"version\": 1"), std::string::npos);
}

TEST(MatchStatsFormatTest, JsonCarriesRecordedTeamStatsAndExpectedGoals) {
  MatchStatsDocument document = BaseDocument();
  document.teams[0].shots = 14;
  document.teams[0].shotsOnTarget = 8;
  document.teams[0].passes = 210;
  document.teams[0].passesCompleted = 149;
  document.teams[0].passAccuracyPercent = 71;
  document.teams[0].expectedGoals = 1.32f;

  const std::string json = SerialiseMatchStatsJson(document);
  EXPECT_NE(json.find("\"shots\":14"), std::string::npos);
  EXPECT_NE(json.find("\"passAccuracyPercent\":71"), std::string::npos);
  EXPECT_NE(json.find("\"expectedGoals\":1.32"), std::string::npos);
  EXPECT_NE(json.find("\"side\":\"home\""), std::string::npos);
  EXPECT_NE(json.find("\"side\":\"away\""), std::string::npos);
}

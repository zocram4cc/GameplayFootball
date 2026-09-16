// The end-of-match statistics document: one stable JSON document plus its
// Wikipedia footballbox rendering, written at full time (see GameOverPage).
// The field set, the JSON schema and the wikitext shape are a frozen
// interface shared with tools/rigbot-control-panel; change both together.
//
// Everything here is pure data and text so the formatter can be unit-tested
// without an engine. Event data is not derived here: the document is built
// from MatchData/MatchAnalytics by GameOverPage.

#ifndef _HPP_MATCHSTATSEXPORT
#define _HPP_MATCHSTATSEXPORT

#include <optional>
#include <string>
#include <vector>

struct MatchGoalEvent {
  int minute = 0;
  std::string player;
  bool ownGoal = false;
  // A direct penalty-kick conversion, sourced from the referee's penalty
  // award and the taker who struck it (Match::IsPendingPenaltyGoal).
  bool penalty = false;
};

struct MatchCardEvent {
  enum CardType { Yellow, Red };
  int minute = 0;
  std::string player;
  CardType type = Yellow;
};

struct MatchStatsTeam {
  int databaseID = 0;
  std::string name;
  int score = 0;
  std::vector<MatchGoalEvent> goals;
  std::vector<MatchCardEvent> cards;
  int shots = 0;
  int shotsOnTarget = 0;
  int saves = 0;
  int corners = 0;
  int fouls = 0;
  int offsides = 0;
  int possessionPercent = 0;
  int passes = 0;
  int passesCompleted = 0;
  // Derived; absent when the match recorded no pass attempts at all.
  std::optional<int> passAccuracyPercent;
  std::optional<float> expectedGoals;
};

struct MatchStatsDocument {
  std::string matchId;
  std::string date;  // ISO, e.g. 2026-09-16
  std::string competition;
  std::string stadium;  // engine stadium token, e.g. st017; empty when unknown
  // Neither is recorded anywhere in the engine; both stay null/empty rather
  // than carrying a guess.
  std::optional<std::string> attendance;
  std::optional<std::string> referee;
  int durationMinutes = 0;  // match-length setting; 0 means not recorded
  std::vector<MatchStatsTeam> teams;  // exactly two: home, away
};

// Wikipedia {{footballbox}} wikitext for the match, in the frozen field set
// and order. Goal annotations {{goal|<min>}}, {{goal|<min>|pen}} and
// {{goal|<min>|o.g.}}; cards ride along in the goals parameters as
// {{yel|<min>}} / {{sent off|<n>|<min>}}, the only slot the template has for
// them. Behavioural reference for the shape: Two-Scoops/SEN_P-AI
// (WikiFootballbox.h) - reimplemented from scratch here.
std::string FormatFootballboxWikitext(const MatchStatsDocument& document);

// Deterministic JSON serialisation of the frozen schema (version 1).
std::string SerialiseMatchStatsJson(const MatchStatsDocument& document);

// Writes <statsDir>/<matchId>.json and <statsDir>/<matchId>.wiki.txt.
// Returns false and fills errorOut when either file could not be written.
bool WriteMatchStatsFiles(const MatchStatsDocument& document,
                          const std::string& statsDir, std::string& errorOut);

#endif

#include "matchstatsexport.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace {

// Renders "16 September 2026" from an ISO "2026-09-16" date, the form the
// footballbox template expects. Falls back to the raw string if it doesn't
// parse cleanly - never crashes formatting over a malformed date.
std::string FormatLongDate(const std::string& isoDate) {
  static const char* const months[] = {"January",   "February", "March",    "April",
                                       "May",       "June",     "July",     "August",
                                       "September", "October",  "November", "December"};
  if (isoDate.size() != 10 || isoDate[4] != '-' || isoDate[7] != '-') return isoDate;
  const int month = std::atoi(isoDate.substr(5, 2).c_str());
  if (month < 1 || month > 12) return isoDate;
  const int day = std::atoi(isoDate.substr(8, 2).c_str());
  const std::string year = isoDate.substr(0, 4);
  return std::to_string(day) + " " + months[month - 1] + " " + year;
}

std::string GoalAnnotation(const MatchGoalEvent& goal) {
  std::string result = "{{goal|" + std::to_string(goal.minute);
  if (goal.ownGoal)
    result += "|o.g.";
  else if (goal.penalty)
    result += "|pen";
  return result + "}}";
}

// {{sent off|<n>|<minute>}}: n is the number of yellow cards this player
// already had before this one (0 for a straight red or a first booking that
// is itself red). Wikipedia's convention, followed by SEN_P-AI.
std::string CardAnnotation(const MatchCardEvent& card, int priorYellowsForPlayer) {
  if (card.type == MatchCardEvent::Yellow)
    return "{{yel|" + std::to_string(card.minute) + "}}";
  return "{{sent off|" + std::to_string(priorYellowsForPlayer) + "|" +
         std::to_string(card.minute) + "}}";
}

// One line per distinct player, in order of that player's first event,
// carrying every goal and card annotation of theirs - the only slot the
// footballbox template has for cards.
std::string FormatPlayerEvents(const MatchStatsTeam& team) {
  struct Entry {
    int minute;
    int sequence;
    std::string annotation;
  };
  std::map<std::string, std::vector<Entry>> byPlayer;
  std::map<std::string, int> firstMinute;
  std::vector<std::string> order;
  int sequence = 0;

  auto noteFirst = [&](const std::string& player, int minute) {
    auto it = firstMinute.find(player);
    if (it == firstMinute.end()) {
      firstMinute[player] = minute;
      order.push_back(player);
    } else if (minute < it->second) {
      it->second = minute;
    }
  };

  for (const MatchGoalEvent& goal : team.goals) {
    noteFirst(goal.player, goal.minute);
    byPlayer[goal.player].push_back({goal.minute, sequence++, GoalAnnotation(goal)});
  }

  // Cards need a running yellow count per player, resolved in match order.
  std::vector<const MatchCardEvent*> sortedCards;
  for (const MatchCardEvent& card : team.cards) sortedCards.push_back(&card);
  std::stable_sort(sortedCards.begin(), sortedCards.end(),
                   [](const MatchCardEvent* a, const MatchCardEvent* b) {
                     return a->minute < b->minute;
                   });
  std::map<std::string, int> yellowsSoFar;
  for (const MatchCardEvent* card : sortedCards) {
    noteFirst(card->player, card->minute);
    const int prior = yellowsSoFar[card->player];
    byPlayer[card->player].push_back({card->minute, sequence++, CardAnnotation(*card, prior)});
    if (card->type == MatchCardEvent::Yellow) yellowsSoFar[card->player]++;
  }

  // Players appear in order of their earliest event; goals and cards were
  // walked in two separate passes above, so this re-sort is what actually
  // orders a card-first player correctly relative to the rest.
  std::stable_sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
    return firstMinute[a] < firstMinute[b];
  });

  std::vector<std::string> parts;
  for (const std::string& player : order) {
    std::vector<Entry>& entries = byPlayer[player];
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
      return a.minute != b.minute ? a.minute < b.minute : a.sequence < b.sequence;
    });
    std::string line = player;
    for (const Entry& entry : entries) line += " " + entry.annotation;
    parts.push_back(line);
  }

  std::string joined;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) joined += " ";
    joined += parts[i];
  }
  return joined;
}

std::string PadParamName(const std::string& name) {
  std::string padded = name;
  while (padded.size() < 11) padded += ' ';
  return padded;
}

void AppendParam(std::ostringstream& out, const std::string& name, const std::string& value) {
  out << "|" << PadParamName(name) << "=";
  if (!value.empty()) out << " " << value;
  out << "\n";
}

std::string JsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string JsonString(const std::string& text) { return "\"" + JsonEscape(text) + "\""; }

std::string JsonOptionalString(const std::optional<std::string>& value) {
  return value ? JsonString(*value) : "null";
}

std::string JsonFloat2(float value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f", value);
  return buf;
}

std::string SerialiseGoal(const MatchGoalEvent& goal) {
  std::ostringstream out;
  out << "{\"minute\":" << goal.minute << ",\"player\":" << JsonString(goal.player)
      << ",\"own\":" << (goal.ownGoal ? "true" : "false")
      << ",\"penalty\":" << (goal.penalty ? "true" : "false") << "}";
  return out.str();
}

std::string SerialiseCard(const MatchCardEvent& card) {
  std::ostringstream out;
  out << "{\"minute\":" << card.minute << ",\"player\":" << JsonString(card.player)
      << ",\"type\":" << JsonString(card.type == MatchCardEvent::Yellow ? "yellow" : "red")
      << "}";
  return out.str();
}

std::string SerialiseTeam(const MatchStatsTeam& team, const char* side) {
  std::ostringstream out;
  out << "{\"id\":" << team.databaseID << ",\"name\":" << JsonString(team.name)
      << ",\"side\":" << JsonString(side) << ",\"score\":" << team.score;
  out << ",\"goals\":[";
  for (size_t i = 0; i < team.goals.size(); ++i) {
    if (i) out << ",";
    out << SerialiseGoal(team.goals[i]);
  }
  out << "],\"cards\":[";
  for (size_t i = 0; i < team.cards.size(); ++i) {
    if (i) out << ",";
    out << SerialiseCard(team.cards[i]);
  }
  out << "],\"shots\":" << team.shots << ",\"shotsOnTarget\":" << team.shotsOnTarget
      << ",\"saves\":" << team.saves << ",\"corners\":" << team.corners
      << ",\"fouls\":" << team.fouls << ",\"offsides\":" << team.offsides
      << ",\"possessionPercent\":" << team.possessionPercent << ",\"passes\":" << team.passes
      << ",\"passesCompleted\":" << team.passesCompleted << ",\"passAccuracyPercent\":"
      << (team.passAccuracyPercent ? std::to_string(*team.passAccuracyPercent) : "null")
      << ",\"expectedGoals\":"
      << (team.expectedGoals ? JsonFloat2(*team.expectedGoals) : "null") << "}";
  return out.str();
}

}  // namespace

std::string FormatFootballboxWikitext(const MatchStatsDocument& document) {
  if (document.teams.size() != 2) return std::string();
  const MatchStatsTeam* home = &document.teams[0];
  const MatchStatsTeam* away = &document.teams[1];

  std::ostringstream out;
  out << "{{footballbox\n";
  AppendParam(out, "date", FormatLongDate(document.date));
  AppendParam(out, "time", "");
  AppendParam(out, "team1", home->name);
  AppendParam(out, "score", std::to_string(home->score) + " \xe2\x80\x93 " +
                                std::to_string(away->score));
  AppendParam(out, "team2", away->name);
  AppendParam(out, "goals1", FormatPlayerEvents(*home));
  AppendParam(out, "goals2", FormatPlayerEvents(*away));
  AppendParam(out, "stadium", document.stadium);
  AppendParam(out, "attendance", document.attendance ? *document.attendance : "");
  AppendParam(out, "referee", document.referee ? *document.referee : "");
  AppendParam(out, "report", "");
  out << "}}\n";
  return out.str();
}

std::string SerialiseMatchStatsJson(const MatchStatsDocument& document) {
  std::ostringstream out;
  out << "{\n";
  out << "  \"format\": \"gf-match-stats\",\n";
  out << "  \"version\": 1,\n";
  out << "  \"matchId\": " << JsonString(document.matchId) << ",\n";
  out << "  \"date\": " << JsonString(document.date) << ",\n";
  out << "  \"competition\": " << JsonString(document.competition) << ",\n";
  out << "  \"stadium\": " << JsonString(document.stadium) << ",\n";
  out << "  \"attendance\": " << JsonOptionalString(document.attendance) << ",\n";
  out << "  \"referee\": " << JsonOptionalString(document.referee) << ",\n";
  out << "  \"durationMinutes\": " << document.durationMinutes << ",\n";
  out << "  \"teams\": [\n";
  for (size_t i = 0; i < document.teams.size(); ++i) {
    out << "    " << SerialiseTeam(document.teams[i], i == 0 ? "home" : "away");
    if (i + 1 < document.teams.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
  return out.str();
}

bool WriteMatchStatsFiles(const MatchStatsDocument& document, const std::string& statsDir,
                          std::string& errorOut) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(statsDir, ec);
  if (ec) {
    errorOut = "could not create stats directory " + statsDir + ": " + ec.message();
    return false;
  }

  const std::string base = statsDir + (statsDir.empty() || statsDir.back() == '/' ? "" : "/") +
                           document.matchId;

  std::ofstream jsonFile(base + ".json", std::ios::trunc);
  if (!jsonFile) {
    errorOut = "could not open " + base + ".json for writing";
    return false;
  }
  jsonFile << SerialiseMatchStatsJson(document);
  if (!jsonFile) {
    errorOut = "failed writing " + base + ".json";
    return false;
  }

  std::ofstream wikiFile(base + ".wiki.txt", std::ios::trunc);
  if (!wikiFile) {
    errorOut = "could not open " + base + ".wiki.txt for writing";
    return false;
  }
  wikiFile << FormatFootballboxWikitext(document);
  if (!wikiFile) {
    errorOut = "failed writing " + base + ".wiki.txt";
    return false;
  }

  return true;
}

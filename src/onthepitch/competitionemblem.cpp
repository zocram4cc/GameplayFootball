#include "onthepitch/competitionemblem.hpp"

#include <cctype>

namespace CompetitionEmblem {

namespace {

std::string Trimmed(const std::string& text) {
  std::string::size_type first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  std::string::size_type last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

// Long enough to be a name rather than a board: 4chan's longest are three or four
// letters (/vip/, /qst/), so a tag past that is somebody's team name in slashes.
const std::string::size_type kLongestBoardTag = 4;

}  // namespace

bool IsBoard(const std::string& teamName) {
  const std::string name = Trimmed(teamName);
  if (name.size() < 3) return false;
  if (name.front() != '/' || name.back() != '/') return false;
  const std::string tag = name.substr(1, name.size() - 2);
  if (tag.empty() || tag.size() > kLongestBoardTag) return false;
  for (char letter : tag)
    if (letter == '/') return false;
  return true;
}

std::string ForTeams(const std::string& homeName, const std::string& awayName) {
  return (IsBoard(homeName) && IsBoard(awayName)) ? "4cc" : "vgl";
}

std::string ObjectPath(const std::string& stadiumObjectPath, const std::string& emblem) {
  if (stadiumObjectPath.empty() || emblem.empty()) return "";
  // A name, never a path: this is read from a config file.
  for (char letter : emblem)
    if (!(std::isalnum(static_cast<unsigned char>(letter)) || letter == '_' || letter == '-'))
      return "";
  const std::string::size_type slash = stadiumObjectPath.find_last_of("/\\");
  const std::string directory =
      slash == std::string::npos ? "" : stadiumObjectPath.substr(0, slash + 1);
  return directory + "entrance/pennant_" + emblem + ".object";
}

}  // namespace CompetitionEmblem

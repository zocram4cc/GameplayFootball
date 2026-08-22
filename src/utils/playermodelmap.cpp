#include "playermodelmap.hpp"

#include <fstream>
#include <sstream>

namespace blunted {

std::map<int, std::string> ParsePlayerModelMap(std::istream& in) {
  std::map<int, std::string> map;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream tokens(line);
    int id;
    if (!(tokens >> id)) continue;
    // The rest of the line, not the next token: a 4cc export names its portraits for
    // the player ("XXX09 - Dante.png") and this file is meant to be edited by hand,
    // so a path with a space in it has to survive rather than be truncated.
    std::string path;
    std::getline(tokens, path);
    const size_t begin = path.find_first_not_of(" \t");
    const size_t end = path.find_last_not_of(" \t\r");
    if (begin == std::string::npos) continue;
    map[id] = path.substr(begin, end - begin + 1);
  }
  return map;
}

const std::string& GetPlayerModelDir(int databaseID) {
  static const std::string empty;
  static bool loaded = false;
  static std::map<int, std::string> map;
  if (!loaded) {
    loaded = true;
    std::ifstream file("media/players/playermodels.cfg");
    if (file.good()) map = ParsePlayerModelMap(file);
  }
  auto it = map.find(databaseID);
  return it == map.end() ? empty : it->second;
}

const std::string& GetPlayerCelebration(int databaseID) {
  static const std::string empty;
  static bool loaded = false;
  static std::map<int, std::string> map;
  if (!loaded) {
    loaded = true;
    std::ifstream file("media/players/playercelebrations.cfg");
    if (file.good()) map = ParsePlayerModelMap(file);
  }
  auto it = map.find(databaseID);
  return it == map.end() ? empty : it->second;
}

const std::string& GetPlayerPortrait(int databaseID) {
  static const std::string empty;
  static bool loaded = false;
  static std::map<int, std::string> map;
  if (!loaded) {
    loaded = true;
    std::ifstream file("media/players/playerportraits.cfg");
    if (file.good()) map = ParsePlayerModelMap(file);
  }
  auto it = map.find(databaseID);
  return it == map.end() ? empty : it->second;
}

}  // namespace blunted

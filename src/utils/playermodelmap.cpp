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
    std::string dir;
    if ((tokens >> id >> dir) && !dir.empty()) {
      map[id] = dir;
    }
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

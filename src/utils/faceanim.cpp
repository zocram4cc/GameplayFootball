#include "faceanim.hpp"

#include <cstdlib>
#include <sstream>

namespace blunted {

static bool ParseTrackLine(const std::string& line,
                           std::string& name,
                           std::vector<FaceKey>& keys,
                           int valuesPerKey) {
  std::stringstream tokens(line);
  std::string field;
  if (!std::getline(tokens, name, ',') || name.empty()) return false;

  std::vector<float> numbers;
  while (std::getline(tokens, field, ',')) {
    char* end = nullptr;
    float value = std::strtof(field.c_str(), &end);
    if (end == field.c_str()) return false;
    numbers.push_back(value);
  }
  int stride = valuesPerKey + 1;
  if (numbers.empty() || numbers.size() % stride != 0) return false;

  for (size_t i = 0; i < numbers.size(); i += stride) {
    FaceKey key;
    key.frame = (int)numbers[i];
    for (int v = 0; v < valuesPerKey; v++) key.values[v] = numbers[i + 1 + v];
    keys.push_back(key);
  }
  return true;
}

bool FaceAnim::Load(std::istream& in) {
  rotations.clear();
  translations.clear();
  frameCount = 0;

  std::string line;
  bool inFrames = false;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (line[0] == '<') {
      inFrames = (line.find("<frames>") == 0);
      continue;
    }
    if (inFrames) {
      frameCount = std::atoi(line.c_str());
      inFrames = false;
      continue;
    }

    std::string name;
    std::vector<FaceKey> keys;
    bool isTranslation = line.find("_pos,") != std::string::npos;
    if (!ParseTrackLine(line, name, keys, isTranslation ? 3 : 4)) continue;
    if (isTranslation) {
      name = name.substr(0, name.size() - 4);  // drop "_pos"
      translations[name] = keys;
    } else {
      rotations[name] = keys;
    }
  }
  return !rotations.empty() || !translations.empty();
}

}  // namespace blunted

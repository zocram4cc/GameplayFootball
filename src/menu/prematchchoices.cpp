#include "menu/prematchchoices.hpp"

#include <algorithm>
#include <set>

namespace PrematchChoices {

namespace {

// media/objects/stadiums/sky is the optional dome a stadium can add, not
// somewhere to play.
const char* const kNotAStadium = "sky";

std::string DirectoryName(const std::string& path) {
  const std::string::size_type last = path.find_last_of("/\\");
  if (last == std::string::npos) return "";
  const std::string::size_type previous = path.find_last_of("/\\", last - 1);
  if (previous == std::string::npos) return path.substr(0, last);
  return path.substr(previous + 1, last - previous - 1);
}

std::string FileName(const std::string& path) {
  const std::string::size_type slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool AllDigits(const std::string& text) {
  if (text.empty()) return false;
  for (char c : text)
    if (c < '0' || c > '9') return false;
  return true;
}

}  // namespace

std::vector<Choice> Stadiums(const std::vector<std::string>& objectPaths) {
  std::vector<Choice> choices;
  for (const std::string& path : objectPaths) {
    const std::string directory = DirectoryName(path);
    if (directory.empty() || directory == kNotAStadium) continue;
    choices.push_back({directory, path});
  }
  std::sort(choices.begin(), choices.end(),
            [](const Choice& a, const Choice& b) { return a.label < b.label; });
  return choices;
}

std::vector<Choice> Entrances(const std::vector<std::string>& familyNames) {
  std::vector<Choice> choices;
  // "" is what the engine already does: pick by competition, then by stadium.
  choices.push_back({"entrance_any", ""});
  std::vector<std::string> sorted = familyNames;
  std::sort(sorted.begin(), sorted.end());
  for (const std::string& family : sorted) choices.push_back({family, family});
  choices.push_back({"entrance_none", "none"});
  return choices;
}

std::vector<Choice> ResultCutscenes(const std::vector<std::string>& camtrackNames) {
  std::set<std::string> families;
  for (const std::string& name : camtrackNames) {
    const std::string family = FamilyFromCamtrackName(name);
    if (!family.empty()) families.insert(family);
  }
  std::vector<Choice> choices;
  choices.push_back({"cutscene_any", ""});
  for (const std::string& family : families) choices.push_back({family, family});
  return choices;
}

std::string FamilyFromCamtrackName(const std::string& filename) {
  // "<category>_<family>_<stadium>_<shot>.camtrack"
  const std::string name = FileName(filename);
  const std::string::size_type first = name.find('_');
  if (first == std::string::npos) return "";
  const std::string::size_type second = name.find('_', first + 1);
  if (second == std::string::npos) return "";
  const std::string family = name.substr(first + 1, second - first - 1);
  return AllDigits(family) ? family : "";
}

int IndexFromSlider(float value, int count) {
  if (count <= 1) return 0;
  if (value <= 0.0f) return 0;
  if (value >= 1.0f) return count - 1;
  const int index = static_cast<int>(value * (count - 1) + 0.5f);
  return std::max(0, std::min(index, count - 1));
}

float SliderFromIndex(int index, int count) {
  if (count <= 1) return 0.0f;
  const int clamped = std::max(0, std::min(index, count - 1));
  return static_cast<float>(clamped) / static_cast<float>(count - 1);
}

int IndexOfValue(const std::vector<Choice>& choices, const std::string& value) {
  for (unsigned int i = 0; i < choices.size(); i++)
    if (choices[i].value == value) return static_cast<int>(i);
  return 0;  // no longer installed: start at the beginning rather than past the end
}

}  // namespace PrematchChoices

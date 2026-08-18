#include "teamflag.hpp"

#include <algorithm>

namespace TeamFlag {

namespace {
bool Contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}
}  // namespace

Side SideOf(const std::string& texturePath) {
  std::string lower = texturePath;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (!Contains(lower, "teamflag_")) return e_NotAFlag;
  if (Contains(lower, "teamflag_away")) return e_Away;
  if (Contains(lower, "teamflag_home")) return e_Home;
  return e_NotAFlag;
}

std::string BadgeFor(const std::string& teamLogoPath) {
  const size_t first = teamLogoPath.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const size_t last = teamLogoPath.find_last_not_of(" \t\r\n");
  return teamLogoPath.substr(first, last - first + 1);
}

}  // namespace TeamFlag

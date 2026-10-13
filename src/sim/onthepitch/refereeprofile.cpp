#include "refereeprofile.hpp"

#include <cctype>

namespace RefereeProfile {

namespace {

std::string Normalize(const std::string& name) {
  std::string result;
  result.reserve(name.size());
  for (char character : name) {
    const unsigned char raw = static_cast<unsigned char>(character);
    if (std::isalnum(raw))
      result += static_cast<char>(std::tolower(raw));
  }
  return result;
}

}  // namespace

e_Profile Parse(const std::string& name) {
  const std::string key = Normalize(name);
  for (int i = 0; i < e_Profile_Count; i++) {
    const e_Profile profile = static_cast<e_Profile>(i);
    if (key == Normalize(GetName(profile)))
      return profile;
  }
  return e_Profile_Standard;
}

std::string GetName(e_Profile profile) {
  switch (profile) {
    case e_Profile_Lenient:
      return "lenient";
    case e_Profile_Strict:
      return "strict";
    default:
      return "standard";
  }
}

unsigned long GetAdvantageWindow_ms(e_Profile profile) {
  switch (profile) {
    case e_Profile_Lenient:
      return 4500;
    case e_Profile_Strict:
      return 1800;
    default:
      return 3000;  // historical value in Referee::CheckFoul
  }
}

}  // namespace RefereeProfile

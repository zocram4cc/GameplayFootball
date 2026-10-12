#include "refereeprofile.hpp"

#include <cctype>
#include <cmath>

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

// A strict referee whistles for contact a lenient one waves away.
const float strictThresholdScale = 0.85f;
const float lenientThresholdScale = 1.2f;

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

Thresholds GetThresholds(e_Profile profile) {
  Thresholds thresholds;  // historical values: 1.0 / 1.4 / 2.0

  float scale = 1.0f;
  if (profile == e_Profile_Strict)
    scale = strictThresholdScale;
  else if (profile == e_Profile_Lenient)
    scale = lenientThresholdScale;

  thresholds.foul *= scale;
  thresholds.yellow *= scale;
  thresholds.red *= scale;
  return thresholds;
}

int GetFoulType(e_Profile profile, float severity) {
  const Thresholds thresholds = GetThresholds(profile);
  if (severity >= thresholds.red)
    return 3;
  if (severity >= thresholds.yellow)
    return 2;
  if (severity > thresholds.foul)
    return 1;
  return 0;
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

bool ShouldReviewOffside(float offsideMargin, bool goalScored) {
  if (!goalScored)
    return false;
  return std::fabs(offsideMargin) <= varOffsideMargin;
}

bool ShouldReviewPenalty(e_Profile profile, float severity, bool insidePenaltyBox) {
  if (!insidePenaltyBox)
    return false;
  const float threshold = GetThresholds(profile).foul;
  return std::fabs(severity - threshold) <= varPenaltyMargin;
}

}  // namespace RefereeProfile

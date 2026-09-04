#include "teamphilosophy.hpp"

#include <algorithm>
#include <cctype>

#include "../gametypes.hpp"

namespace TeamPhilosophy {

namespace {

// Lowercases and drops every separator, so "Tiki-Taka", "tiki taka" and
// "tikitaka" all normalise to the same key.
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

e_Philosophy Parse(const std::string& name) {
  const std::string key = Normalize(name);
  for (int i = 0; i < e_Philosophy_Count; i++) {
    const e_Philosophy philosophy = static_cast<e_Philosophy>(i);
    if (key == Normalize(GetName(philosophy)))
      return philosophy;
  }
  return e_Philosophy_Balanced;
}

std::string GetName(e_Philosophy philosophy) {
  switch (philosophy) {
    case e_Philosophy_Gegenpressing:
      return "gegenpressing";
    case e_Philosophy_TikiTaka:
      return "tiki-taka";
    case e_Philosophy_ParkTheBus:
      return "park the bus";
    default:
      return "balanced";
  }
}

void ApplyPreset(e_Philosophy philosophy, blunted::Properties& tactics) {
  switch (philosophy) {
    case e_Philosophy_Gegenpressing:
      // Win the ball back high up the pitch, in numbers, immediately.
      tactics.Set("team_pressure", 1.0f);
      tactics.Set("position_defense_depth_factor", 0.9f);
      tactics.Set("position_defense_microfocus_strength", 1.0f);
      tactics.Set("position_defense_midfieldfocus", 0.65f);
      tactics.Set("counter_attack", 0.85f);
      tactics.Set("support_distance", 0.45f);
      break;

    case e_Philosophy_TikiTaka:
      // Keep the ball through the middle with short, low-risk links.
      tactics.Set("position_offense_midfieldfocus", 0.85f);
      tactics.Set("position_offense_microfocus_strength", 0.75f);
      tactics.Set("dribble_offensiveness", 0.15f);
      tactics.Set("dribble_centermagnet", 0.7f);
      tactics.Set("support_distance", 0.25f);
      tactics.Set("counter_attack", 0.3f);
      break;

    case e_Philosophy_ParkTheBus:
      // Two banks in front of the box; no pressing, no offside line to speak of.
      tactics.Set("position_defense_depth_factor", 0.0f);
      tactics.Set("position_defense_width_factor", 0.35f);
      tactics.Set("position_defense_microfocus_strength", 0.85f);
      tactics.Set("position_defense_midfieldfocus", 0.4f);
      tactics.Set("team_pressure", 0.15f);
      tactics.Set("counter_attack", 0.6f);
      break;

    default:
      break;
  }
}

unsigned long GetTeamPressureDurationBonus_ms(e_Philosophy philosophy) {
  return philosophy == e_Philosophy_Gegenpressing ? 5000UL : 0UL;
}

bool PressesOnPossessionLoss(e_Philosophy philosophy) {
  return philosophy == e_Philosophy_Gegenpressing;
}

float GetStaminaDrainMultiplier(e_Philosophy philosophy) {
  return philosophy == e_Philosophy_Gegenpressing ? 1.2f : 1.0f;
}

bool PrefersShortPassing(e_Philosophy philosophy) {
  return philosophy == e_Philosophy_TikiTaka;
}

float GetPassErrorMultiplier(e_Philosophy philosophy, float supportDistance) {
  // Longer support links mean longer, riskier balls.
  const float support = std::max(0.0f, std::min(supportDistance, 1.0f));
  float multiplier = 0.70f + support * 0.35f;  // 0.70 tight .. 1.05 stretched

  if (philosophy == e_Philosophy_TikiTaka)
    multiplier *= 0.62f;  // drilled short passing

  return std::max(0.5f, std::min(multiplier, 1.4f));
}

float AdaptOffsideTrapX(e_Philosophy philosophy, float trapX, int teamSide) {
  if (philosophy != e_Philosophy_ParkTheBus || teamSide == 0)
    return trapX;

  // Never hold a line further forward than the edge of the own penalty box.
  const float side = static_cast<float>(teamSide > 0 ? 1 : -1);
  const float boxEdgeX = side * (pitchHalfW - penaltyBoxDepth);
  return trapX * side > boxEdgeX * side ? trapX : boxEdgeX;
}

// The key philosophy is stored under, which has the philosophy menu instead.
// The tactics keys with an editor of their own, which are therefore not sliders:
// the philosophy menu, and the instructions the game plan and the touchline set.
// A tactic that is not a number on a scale has no business being drawn as one.
// "formation" is a shape - "4-4-2" - and appeared as an unlabelled bar at zero
// that overwrote the shape the moment it was touched (owner, 04-09: "no generic
// sliders like FORMATION that have no meaning as a slider"); it is chosen from
// the formation picker or by dragging players on the pitch.
static const char* kNonSliderKeys[] = {"philosophy", "mentality", "instructions", "formation"};

bool IsSliderTactic(const std::string& tacticName) {
  if (tacticName.empty())
    return false;
  for (const char* key : kNonSliderKeys) {
    if (tacticName == key)
      return false;
  }
  return true;
}

}  // namespace TeamPhilosophy

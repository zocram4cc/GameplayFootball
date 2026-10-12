#include "formstate.hpp"

#include <algorithm>
#include <cstring>

namespace FormState {

namespace {

// docs/21Research.md, "Condition Arrow Multipliers": rows worst to best,
// columns awareness / technical / physique.
constexpr float kArrowTable[(int)Arrow::Count][3] = {
    {0.84f, 0.88f, 0.88f},  // Terrible (purple)
    {0.92f, 0.94f, 0.94f},  // Poor (blue)
    {1.00f, 1.00f, 1.00f},  // Normal (green)
    {1.06f, 1.05f, 1.03f},  // Great (orange)
    {1.12f, 1.09f, 1.06f},  // Top (red)
};

// docs/21Research.md, "Position Familiarity Multipliers": rows C / B / A,
// columns technical / awareness / physique.
constexpr float kFamiliarityTable[3][3] = {
    {0.80f, 0.82f, 0.84f},  // C, unfamiliar
    {0.92f, 0.94f, 0.96f},  // B, partial
    {1.00f, 1.00f, 1.00f},  // A, natural
};

bool Is(const char* stat, const char* key) { return std::strcmp(stat, key) == 0; }
bool StartsWith(const char* stat, const char* prefix) {
  return std::strncmp(stat, prefix, std::strlen(prefix)) == 0;
}

// The Form attribute shapes the draw: the cumulative bands below are the five
// arrows' shares. A steady player (form 1.0) is Normal three matches in four
// and never Terrible; a volatile one (form 0.0) is Normal one in three and
// lands at either end one match in five.
void ArrowBands(float form01, float out[(int)Arrow::Count]) {
  const float f = std::clamp(form01, 0.0f, 1.0f);
  const float ends = 0.20f - 0.20f * f;        // Terrible and Top each
  const float nearEnds = 0.15f - 0.05f * f;    // Poor and Great each
  const float normal = 1.0f - 2.0f * (ends + nearEnds);
  out[0] = ends;
  out[1] = ends + nearEnds;
  out[2] = ends + nearEnds + normal;
  out[3] = ends + nearEnds + normal + nearEnds;
  out[4] = 1.0f;
}

}  // namespace

const char* ArrowName(Arrow arrow) {
  switch (arrow) {
    case Arrow::Terrible: return "terrible";
    case Arrow::Poor: return "poor";
    case Arrow::Normal: return "normal";
    case Arrow::Great: return "great";
    case Arrow::Top: return "top";
    case Arrow::Count: break;
  }
  return "normal";
}

const char* ArrowColour(Arrow arrow) {
  switch (arrow) {
    case Arrow::Terrible: return "purple";
    case Arrow::Poor: return "blue";
    case Arrow::Normal: return "green";
    case Arrow::Great: return "orange";
    case Arrow::Top: return "red";
    case Arrow::Count: break;
  }
  return "green";
}

const char* PolicyName(Policy policy) {
  switch (policy) {
    case Policy::RandomPerPlayer: return "random";
    case Policy::AllTerrible: return "purple";
    case Policy::AllPoor: return "blue";
    case Policy::AllNormal: return "green";
    case Policy::AllGreat: return "orange";
    case Policy::AllTop: return "red";
    case Policy::Count: break;
  }
  return "random";
}

Policy ParsePolicy(const std::string& text) {
  for (int i = 0; i < (int)Policy::Count; i++)
    if (text == PolicyName((Policy)i)) return (Policy)i;
  // The arrow names are accepted too, so a config can say "top" or "red".
  for (int i = 0; i < (int)Arrow::Count; i++)
    if (text == ArrowName((Arrow)i)) return (Policy)(i + 1);
  return Policy::RandomPerPlayer;
}

std::string SerializePolicy(Policy policy) { return PolicyName(policy); }

Arrow Draw(Policy policy, float form01, float roll) {
  if (policy != Policy::RandomPerPlayer) return (Arrow)((int)policy - 1);
  float bands[(int)Arrow::Count];
  ArrowBands(form01, bands);
  const float r = std::clamp(roll, 0.0f, 0.999999f);
  for (int i = 0; i < (int)Arrow::Count; i++)
    if (r < bands[i]) return (Arrow)i;
  return Arrow::Normal;
}

Arrow ArrowForPlayer(Policy policy, float form01, unsigned int seed, int teamID,
                     int playerIndex) {
  // splitmix32 on (seed, team, index): no state, uniform enough for a dice
  // roll, identical wherever it is computed.
  unsigned int x = seed ^ (0x9E3779B9u * (unsigned int)(teamID * 64 + playerIndex + 1));
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  x ^= x >> 16;
  return Draw(policy, form01, (x >> 8) / 16777216.0f);
}

ArrowTier ArrowTierOf(const char* stat) {
  // Tier 1: awareness, the keeper's five, kicking power.
  if (Is(stat, "mental_offensivepositioning") || Is(stat, "mental_defensivepositioning") ||
      StartsWith(stat, "gk_") || Is(stat, "physical_shotpower"))
    return ArrowTier::Awareness;
  // Tier 3: physique.
  if (Is(stat, "physical_contact") || Is(stat, "physical_balance") ||
      Is(stat, "physical_stamina"))
    return ArrowTier::Physique;
  // Tier 2: everything technical, plus acceleration, speed and jump.
  return ArrowTier::Technical;
}

float ArrowMultiplier(Arrow arrow, ArrowTier tier) {
  return kArrowTable[std::clamp((int)arrow, 0, (int)Arrow::Count - 1)][(int)tier];
}

float ArrowMultiplierFor(Arrow arrow, const char* stat) {
  return ArrowMultiplier(arrow, ArrowTierOf(stat));
}

FamiliarityTier FamiliarityTierOf(const char* stat) {
  // Tier 1: awareness, the keeper's five, kicking power, ACCELERATION.
  if (Is(stat, "mental_offensivepositioning") || Is(stat, "mental_defensivepositioning") ||
      StartsWith(stat, "gk_") || Is(stat, "physical_shotpower") ||
      Is(stat, "physical_acceleration"))
    return FamiliarityTier::Awareness;
  // Tier 2: physique.
  if (Is(stat, "physical_contact") || Is(stat, "physical_balance") ||
      Is(stat, "physical_stamina"))
    return FamiliarityTier::Physique;
  // Tier 0: technique - and here speed and jump are technical.
  return FamiliarityTier::Technical;
}

float FamiliarityMultiplier(Familiarity level, FamiliarityTier tier) {
  return kFamiliarityTable[std::clamp((int)level, 0, 2)][(int)tier];
}

float FamiliarityMultiplierFor(Familiarity level, const char* stat) {
  return FamiliarityMultiplier(level, FamiliarityTierOf(stat));
}

float Apply(float stat01, Arrow arrow, Familiarity level, const char* stat) {
  const float raised = stat01 * ArrowMultiplierFor(arrow, stat) * FamiliarityMultiplierFor(level, stat);
  return std::clamp(raised, 0.0f, 1.0f);
}

Slot SlotFor(e_PlayerRole role, int side) {
  switch (role) {
    case e_PlayerRole_GK: return Slot::GK;
    case e_PlayerRole_CB: return Slot::CB;
    case e_PlayerRole_LB: return Slot::LB;
    case e_PlayerRole_RB: return Slot::RB;
    case e_PlayerRole_DM: return Slot::DMF;
    case e_PlayerRole_CM: return Slot::CMF;
    case e_PlayerRole_LM: return Slot::LMF;
    case e_PlayerRole_RM: return Slot::RMF;
    case e_PlayerRole_AM: return Slot::AMF;
    case e_PlayerRole_LW: return Slot::LWF;
    case e_PlayerRole_RW: return Slot::RWF;
    case e_PlayerRole_SS: return Slot::SS;
    case e_PlayerRole_CF:
      // GF has one forward role; PES has three. A forward on a flank is a wing
      // forward on that side, one in the middle a centre forward.
      if (side < 0) return Slot::LWF;
      if (side > 0) return Slot::RWF;
      return Slot::CF;
  }
  return Slot::CMF;
}

const char* SlotName(Slot slot) {
  static const char* names[(int)Slot::Count] = {"GK",  "CB",  "LB",  "RB", "DMF", "CMF", "LMF",
                                                "RMF", "AMF", "LWF", "RWF", "SS", "CF"};
  const int i = (int)slot;
  return i >= 0 && i < (int)Slot::Count ? names[i] : "CMF";
}

Familiarity Rating(const std::string& ratings, Slot slot) {
  const size_t i = (size_t)slot;
  if (i >= ratings.size()) return Familiarity::Unfamiliar;
  switch (ratings[i]) {
    case 'A': case 'a': return Familiarity::Natural;
    case 'B': case 'b': return Familiarity::Partial;
    default: return Familiarity::Unfamiliar;
  }
}

std::string InferRatings(const std::vector<e_PlayerRole>& roles) {
  std::string out((size_t)Slot::Count, 'C');
  auto set = [&out](Slot slot, char letter) {
    char& at = out[(size_t)slot];
    if (letter == 'A' || at == 'C') at = letter;  // never downgrade an A
  };
  for (e_PlayerRole role : roles) {
    switch (role) {
      case e_PlayerRole_GK: set(Slot::GK, 'A'); break;
      case e_PlayerRole_CB: set(Slot::CB, 'A'); break;
      case e_PlayerRole_LB: set(Slot::LB, 'A'); set(Slot::RB, 'B'); set(Slot::LMF, 'B'); break;
      case e_PlayerRole_RB: set(Slot::RB, 'A'); set(Slot::LB, 'B'); set(Slot::RMF, 'B'); break;
      case e_PlayerRole_DM: set(Slot::DMF, 'A'); set(Slot::CMF, 'B'); break;
      case e_PlayerRole_CM: set(Slot::CMF, 'A'); set(Slot::DMF, 'B'); set(Slot::AMF, 'B'); break;
      case e_PlayerRole_LM: set(Slot::LMF, 'A'); set(Slot::LWF, 'B'); set(Slot::RMF, 'B'); break;
      case e_PlayerRole_RM: set(Slot::RMF, 'A'); set(Slot::RWF, 'B'); set(Slot::LMF, 'B'); break;
      case e_PlayerRole_AM: set(Slot::AMF, 'A'); set(Slot::SS, 'B'); set(Slot::CMF, 'B'); break;
      case e_PlayerRole_LW: set(Slot::LWF, 'A'); set(Slot::LMF, 'B'); set(Slot::CF, 'B'); break;
      case e_PlayerRole_RW: set(Slot::RWF, 'A'); set(Slot::RMF, 'B'); set(Slot::CF, 'B'); break;
      case e_PlayerRole_SS: set(Slot::SS, 'A'); set(Slot::AMF, 'B'); set(Slot::CF, 'B'); break;
      case e_PlayerRole_CF:
        set(Slot::CF, 'A'); set(Slot::SS, 'A'); set(Slot::LWF, 'B'); set(Slot::RWF, 'B');
        break;
    }
  }
  return out;
}

}  // namespace FormState

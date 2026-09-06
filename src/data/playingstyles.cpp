#include "playingstyles.hpp"

#include <algorithm>
#include <cctype>

#include "playerdata.hpp"

namespace PlayingStyles {

namespace {

const int styleCount = static_cast<int>(Player::Count);

// PES EDIT index order; the importer emits exactly these tokens.
const char* const styleNames[styleCount] = {
    "none",           "goal_poacher",        "dummy_runner",       "fox_in_the_box",
    "target_man",     "creative_playmaker",  "prolific_winger",    "roaming_flank",
    "cross_specialist", "classic_no_10",     "hole_player",        "box_to_box",
    "the_destroyer",  "orchestrator",        "anchor_man",         "build_up",
    "offensive_full_back", "full_back_finisher", "defensive_full_back", "extra_frontman",
    "offensive_goalkeeper", "defensive_goalkeeper",
};

const char* const comNames[comCount] = {
    "trickster",        "mazing_run",  "speeding_bullet", "incisive_run",
    "long_ball_expert", "early_cross", "long_ranger",
};

// The rating a style leans on, so inference hands it to the players who can
// actually play it. None is never rated.
const char* const styleAffinityStat[styleCount] = {
    nullptr,
    "technical_shot",                // goal poacher
    "physical_velocity",             // dummy runner
    "mental_offensivepositioning",   // fox in the box
    "technical_header",              // target man
    "mental_vision",                 // creative playmaker
    "technical_dribble",             // prolific winger
    "technical_ballcontrol",         // roaming flank
    "technical_highpass",            // cross specialist
    "technical_shortpass",           // classic no. 10
    "mental_offensivepositioning",   // hole player
    "physical_stamina",              // box-to-box
    "technical_standingtackle",      // the destroyer
    "mental_vision",                 // orchestrator
    "mental_defensivepositioning",   // anchor man
    "technical_shortpass",           // build up
    "physical_velocity",             // offensive full-back
    "technical_shot",                // full-back finisher
    "mental_defensivepositioning",   // defensive full-back
    "technical_header",              // extra frontman
    "physical_velocity",             // offensive goalkeeper
    "physical_reaction",             // defensive goalkeeper
};

const char* const comAffinityStat[comCount] = {
    "technical_dribble",      // trickster
    "technical_ballcontrol",  // mazing run
    "physical_velocity",      // speeding bullet
    "technical_shot",         // incisive run
    "technical_highpass",     // long ball expert
    "technical_highpass",     // early cross
    "physical_shotpower",     // long ranger
};

// Lowercases and drops separators so "Classic No. 10", "classic_no_10" and
// "classicno10" all resolve to the same style.
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

// A small deterministic hash, so a player's style never changes between matches
// and no two neighbouring ids get the same one.
unsigned int Hash(int seed, int salt) {
  unsigned int hash = static_cast<unsigned int>(seed) * 2654435761u;
  hash ^= static_cast<unsigned int>(salt) * 2246822519u;
  hash ^= hash >> 13;
  hash *= 3266489917u;
  return hash ^ (hash >> 16);
}

float Unit(int seed, int salt) {
  return static_cast<float>(Hash(seed, salt) % 10000u) / 10000.0f;
}

bool IsWide(e_PlayerRole role) {
  return role == e_PlayerRole_LM || role == e_PlayerRole_RM;
}
bool IsFullBack(e_PlayerRole role) {
  return role == e_PlayerRole_LB || role == e_PlayerRole_RB;
}

// Which cards a style naturally comes with in PES squads.
float ComStyleBonus(Com card, Player style) {
  switch (card) {
    case Com::Trickster:
      return style == Player::ClassicNo10 || style == Player::CreativePlaymaker ||
                     style == Player::RoamingFlank
                 ? 0.2f
                 : 0.0f;
    case Com::MazingRun:
      return style == Player::ProlificWinger || style == Player::RoamingFlank ||
                     style == Player::HolePlayer
                 ? 0.2f
                 : 0.0f;
    case Com::SpeedingBullet:
      return style == Player::GoalPoacher || style == Player::DummyRunner ||
                     style == Player::HolePlayer || style == Player::OffensiveFullBack ||
                     style == Player::FullBackFinisher
                 ? 0.2f
                 : 0.0f;
    case Com::IncisiveRun:
      return style == Player::ProlificWinger || style == Player::RoamingFlank ? 0.3f : 0.0f;
    case Com::LongBallExpert:
      return style == Player::Orchestrator || style == Player::BuildUp ||
                     style == Player::AnchorMan
                 ? 0.3f
                 : 0.0f;
    case Com::EarlyCross:
      return style == Player::CrossSpecialist || style == Player::OffensiveFullBack ? 0.3f : 0.0f;
    case Com::LongRanger:
      return style == Player::ClassicNo10 || style == Player::BoxToBox ||
                     style == Player::FullBackFinisher
                 ? 0.2f
                 : 0.0f;
  }
  return 0.0f;
}

}  // namespace

Com GetComAt(int index) {
  return static_cast<Com>(1u << std::max(0, std::min(index, comCount - 1)));
}

bool Has(ComMask mask, Com card) {
  return (mask & static_cast<ComMask>(card)) != 0;
}

Player ParsePlayer(const std::string& token) {
  const std::string key = Normalize(token);
  for (int i = 1; i < styleCount; i++) {
    if (key == Normalize(styleNames[i]))
      return static_cast<Player>(i);
  }
  return Player::None;
}

std::string Serialize(Player style) {
  const int index = static_cast<int>(style);
  return index >= 0 && index < styleCount ? styleNames[index] : styleNames[0];
}

ComMask ParseCom(const std::string& list) {
  ComMask mask = comMaskNone;
  size_t start = 0;
  while (start <= list.size()) {
    const size_t separator = list.find(',', start);
    const size_t end = separator == std::string::npos ? list.size() : separator;
    const std::string key = Normalize(list.substr(start, end - start));
    for (int i = 0; i < comCount; i++) {
      if (!key.empty() && key == Normalize(comNames[i]))
        mask |= static_cast<ComMask>(GetComAt(i));
    }
    if (separator == std::string::npos)
      break;
    start = separator + 1;
  }
  return mask;
}

std::string SerializeCom(ComMask mask) {
  std::string result;
  for (int i = 0; i < comCount; i++) {
    if (!Has(mask, GetComAt(i)))
      continue;
    if (!result.empty())
      result += ",";
    result += comNames[i];
  }
  return result;
}

bool SuitsRole(Player style, e_PlayerRole role) {
  switch (style) {
    case Player::None:
      return true;
    case Player::GoalPoacher:
    case Player::FoxInTheBox:
    case Player::TargetMan:
      return role == e_PlayerRole_CF;
    case Player::DummyRunner:
      return role == e_PlayerRole_CF || role == e_PlayerRole_AM;
    case Player::CreativePlaymaker:
      return role == e_PlayerRole_AM || IsWide(role);
    case Player::ProlificWinger:
    case Player::RoamingFlank:
    case Player::CrossSpecialist:
      return IsWide(role);
    case Player::ClassicNo10:
      return role == e_PlayerRole_AM || role == e_PlayerRole_CM;
    case Player::HolePlayer:
      return role == e_PlayerRole_AM || role == e_PlayerRole_CM || IsWide(role);
    case Player::BoxToBox:
      return role == e_PlayerRole_AM || role == e_PlayerRole_CM || role == e_PlayerRole_DM ||
             IsWide(role);
    case Player::TheDestroyer:
      return role == e_PlayerRole_CM || role == e_PlayerRole_DM || role == e_PlayerRole_CB;
    case Player::Orchestrator:
      return role == e_PlayerRole_CM || role == e_PlayerRole_DM;
    case Player::AnchorMan:
      return role == e_PlayerRole_DM;
    case Player::BuildUp:
    case Player::ExtraFrontman:
      return role == e_PlayerRole_CB;
    case Player::OffensiveFullBack:
    case Player::FullBackFinisher:
    case Player::DefensiveFullBack:
      return IsFullBack(role);
    case Player::OffensiveGoalkeeper:
    case Player::DefensiveGoalkeeper:
      return role == e_PlayerRole_GK;
    case Player::Count:
      break;
  }
  return false;
}

Player InferPlayer(int seed, e_PlayerRole role, const PlayerData& data) {
  float weights[styleCount] = {0.0f};
  float styleTotal = 0.0f;
  for (int i = 1; i < styleCount; i++) {
    const Player style = static_cast<Player>(i);
    if (!SuitsRole(style, role))
      continue;
    weights[i] = 1.0f + 3.0f * std::max(0.0f, std::min(data.GetStat(styleAffinityStat[i]), 1.0f));
    styleTotal += weights[i];
  }
  if (styleTotal <= 0.0f)
    return Player::None;

  // About one player in four has no style at all.
  weights[0] = styleTotal / 3.0f;
  float roll = Unit(seed, 1) * (styleTotal + weights[0]);
  for (int i = 0; i < styleCount; i++) {
    roll -= weights[i];
    if (roll < 0.0f)
      return static_cast<Player>(i);
  }
  return Player::None;
}

ComMask InferCom(int seed, Player style, const PlayerData& data) {
  // Cards steer outfield decisions; a keeper (by style or by listing) has none.
  if (style == Player::OffensiveGoalkeeper || style == Player::DefensiveGoalkeeper ||
      (!data.GetRoles().empty() && data.GetRoles().front() == e_PlayerRole_GK))
    return comMaskNone;

  ComMask mask = comMaskNone;
  int given = 0;
  for (int i = 0; i < comCount && given < 5; i++) {
    const Com card = GetComAt(i);
    const float rating = std::max(0.0f, std::min(data.GetStat(comAffinityStat[i]), 1.0f));
    const float chance = std::min(0.85f, std::max(0.0f, rating - 0.4f) + ComStyleBonus(card, style));
    if (Unit(seed, 100 + i) < chance) {
      mask |= static_cast<ComMask>(card);
      given++;
    }
  }
  return mask;
}

// --- Off the ball ---

float GetDepthOffset(Player style, bool teamHasPossession) {
  switch (style) {
    case Player::DummyRunner:
    case Player::FoxInTheBox:
      return teamHasPossession ? 3.0f : 0.0f;
    case Player::TargetMan:
    case Player::ProlificWinger:
    case Player::CrossSpecialist:
      return teamHasPossession ? 2.0f : 0.0f;
    case Player::ClassicNo10:
      return teamHasPossession ? -2.0f : 0.0f;
    case Player::HolePlayer:
    case Player::OffensiveFullBack:
      return teamHasPossession ? 5.0f : 0.0f;
    case Player::BoxToBox:
      return teamHasPossession ? 4.0f : -4.0f;
    case Player::TheDestroyer:
      return teamHasPossession ? 0.0f : 3.0f;  // steps up to press
    case Player::Orchestrator:
      return teamHasPossession ? -4.0f : 0.0f;
    case Player::AnchorMan:
      return teamHasPossession ? -5.0f : -2.0f;
    case Player::BuildUp:
      return teamHasPossession ? -3.0f : 0.0f;
    case Player::FullBackFinisher:
      return teamHasPossession ? 6.0f : 0.0f;
    case Player::DefensiveFullBack:
      return teamHasPossession ? -4.0f : -2.0f;
    case Player::ExtraFrontman:
      return teamHasPossession ? 8.0f : 0.0f;
    default:
      return 0.0f;
  }
}

float GetWidthOffset(Player style, bool teamHasPossession) {
  if (!teamHasPossession)
    return 0.0f;
  switch (style) {
    case Player::DummyRunner:
    case Player::ProlificWinger:
      return 4.0f;
    case Player::CrossSpecialist:
      return 5.0f;
    case Player::FoxInTheBox:
    case Player::FullBackFinisher:
      return -4.0f;
    case Player::RoamingFlank:
      return -5.0f;
    case Player::ExtraFrontman:
      return -3.0f;
    case Player::TargetMan:
    case Player::HolePlayer:
      return -2.0f;
    default:
      return 0.0f;
  }
}

float GetPoacherTargetX(Player style, float defaultX, float opponentOffsideLineX, int teamSide) {
  if (style != Player::GoalPoacher || teamSide == 0)
    return defaultX;
  // Sit a stride on the own-goal side of the line, whatever the formation says.
  const float side = static_cast<float>(teamSide > 0 ? 1 : -1);
  return opponentOffsideLineX + side * poacherOffsideCushion;
}

float GetAttackingRunAffinity(Player style, ComMask com) {
  float affinity = 0.5f;
  switch (style) {
    case Player::HolePlayer:
      affinity = 0.9f;
      break;
    case Player::FullBackFinisher:
      affinity = 0.8f;
      break;
    case Player::GoalPoacher:
    case Player::DummyRunner:
    case Player::OffensiveFullBack:
    case Player::ExtraFrontman:
      affinity = 0.7f;
      break;
    case Player::BoxToBox:
    case Player::RoamingFlank:
      affinity = 0.6f;
      break;
    case Player::ClassicNo10:
    case Player::TheDestroyer:
      affinity = 0.3f;
      break;
    case Player::Orchestrator:
    case Player::BuildUp:
      affinity = 0.25f;
      break;
    case Player::DefensiveFullBack:
      affinity = 0.15f;
      break;
    case Player::AnchorMan:
    case Player::OffensiveGoalkeeper:
    case Player::DefensiveGoalkeeper:
      affinity = 0.0f;
      break;
    default:
      break;
  }
  if (Has(com, Com::SpeedingBullet))
    affinity += 0.2f;
  if (Has(com, Com::MazingRun))
    affinity += 0.1f;
  return std::max(0.0f, std::min(affinity, 1.0f));
}

float GetPressingDistanceBias(Player style) {
  switch (style) {
    case Player::TheDestroyer:
      return -4.0f;
    case Player::BoxToBox:
      return -2.0f;
    case Player::AnchorMan:
      return 1.0f;  // protects the line rather than hunting
    case Player::Orchestrator:
    case Player::CreativePlaymaker:
    case Player::TargetMan:
      return 2.0f;
    case Player::ClassicNo10:
    case Player::FoxInTheBox:
    case Player::GoalPoacher:
      return 3.0f;
    default:
      return 0.0f;
  }
}

// --- On the ball ---

float GetShotAppetite(Player style, ComMask com) {
  float appetite = 1.0f;
  switch (style) {
    case Player::FoxInTheBox:
      appetite += 0.3f;
      break;
    case Player::GoalPoacher:
      appetite += 0.25f;
      break;
    case Player::ProlificWinger:
      appetite += 0.15f;
      break;
    case Player::HolePlayer:
    case Player::FullBackFinisher:
      appetite += 0.1f;
      break;
    case Player::CreativePlaymaker:
      appetite -= 0.2f;  // looks for the pass before the shot
      break;
    case Player::AnchorMan:
      appetite -= 0.25f;
      break;
    case Player::Orchestrator:
    case Player::BuildUp:
    case Player::DefensiveFullBack:
      appetite -= 0.15f;
      break;
    case Player::ClassicNo10:
    case Player::TheDestroyer:
      appetite -= 0.1f;
      break;
    default:
      break;
  }
  if (Has(com, Com::LongRanger))
    appetite += 0.35f;
  if (Has(com, Com::IncisiveRun))
    appetite += 0.15f;
  return std::max(0.55f, std::min(appetite, 2.0f));
}

float GetShootingRangeBonus(Player style, ComMask com) {
  float bonus = 0.0f;
  if (Has(com, Com::LongRanger))
    bonus += 9.0f;
  if (Has(com, Com::IncisiveRun))
    bonus += 2.0f;
  if (style == Player::FoxInTheBox)
    bonus -= 2.0f;  // he wants it inside the six-yard box
  return std::max(0.0f, std::min(bonus, 14.0f));
}

float GetPassTypeBias(Player style, ComMask com, e_FunctionType passType) {
  float bias = 1.0f;
  switch (passType) {
    case e_FunctionType_ShortPass:
      if (style == Player::ClassicNo10)
        bias *= 1.15f;
      if (style == Player::CreativePlaymaker)
        bias *= 1.05f;
      if (Has(com, Com::LongBallExpert))
        bias *= 0.9f;
      break;
    case e_FunctionType_LongPass:  // the through ball
      if (style == Player::Orchestrator || style == Player::BuildUp)
        bias *= 1.15f;
      if (style == Player::CreativePlaymaker)
        bias *= 1.1f;
      if (Has(com, Com::IncisiveRun))
        bias *= 1.25f;
      if (Has(com, Com::LongBallExpert))
        bias *= 1.3f;
      break;
    case e_FunctionType_HighPass:  // the cross / the long ball
      if (style == Player::CrossSpecialist)
        bias *= 1.25f;
      if (style == Player::ProlificWinger)
        bias *= 1.1f;
      if (Has(com, Com::EarlyCross))
        bias *= 1.3f;
      if (Has(com, Com::LongBallExpert))
        bias *= 1.3f;
      break;
    default:
      break;
  }
  return std::max(0.5f, std::min(bias, 1.6f));
}

float GetSpaceRatingWeight(Player style, float baseWeight) {
  const float base = std::max(0.0f, std::min(baseWeight, 1.0f));
  if (style != Player::CreativePlaymaker && style != Player::ClassicNo10 &&
      style != Player::Orchestrator)
    return base;
  // Shift the weight towards "find the pocket of space" without ever exceeding 1.
  return base + (1.0f - base) * 0.35f;
}

// --- Dribbling ---

float GetDribbleDrive(Player style, ComMask com, float baseDrive) {
  float drive = baseDrive;
  if (style == Player::ClassicNo10 || style == Player::TargetMan)
    drive *= 0.7f;
  if (style == Player::Orchestrator)
    drive *= 0.8f;
  if (style == Player::ProlificWinger)
    drive *= 1.1f;
  if (Has(com, Com::MazingRun))
    drive *= 1.3f;
  if (Has(com, Com::IncisiveRun) || Has(com, Com::SpeedingBullet))
    drive *= 1.15f;
  return drive;
}

float GetOpponentRepel(Player style, ComMask com, float baseRepel) {
  float repel = baseRepel;
  if (style == Player::TargetMan)
    repel *= 0.85f;  // holds it up under contact
  if (Has(com, Com::Trickster))
    repel *= 0.6f;
  if (Has(com, Com::MazingRun))
    repel *= 0.75f;
  return repel;
}

float GetDribbleCenterPull(Player style, ComMask com, float baseCenterMagnet) {
  const float base = std::max(0.0f, std::min(baseCenterMagnet, 1.0f));
  if (style == Player::CrossSpecialist)
    return base * 0.5f;  // stays wide for the cross
  if (style == Player::RoamingFlank || style == Player::ProlificWinger || Has(com, Com::IncisiveRun))
    return base + (1.0f - base) * 0.5f;  // cuts inside
  return base;
}

float GetDribbleVelocity(Player style, ComMask com, float desiredVelocity) {
  float velocity = desiredVelocity;
  if (style == Player::TargetMan || style == Player::ClassicNo10)
    velocity = std::min(velocity, dribbleVelocity);
  if (Has(com, Com::SpeedingBullet))
    velocity = std::min(velocity * 1.25f, sprintVelocity);
  return velocity;
}

// --- Keeper ---

float GetKeeperComeOutBias(Player style) {
  switch (style) {
    case Player::OffensiveGoalkeeper:
      return 1.35f;
    case Player::DefensiveGoalkeeper:
      return 0.7f;
    default:
      return 1.0f;
  }
}

}  // namespace PlayingStyles

#include "playerskills.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "playerdata.hpp"

namespace PlayerSkills {

namespace {

const char* const skillNames[skillCount] = {
    "scissors_feint",     "double_touch",       "flip_flap",         "marseille_turn",
    "sombrero",           "cross_over_turn",    "cut_behind_turn",   "scotch_move",
    "step_on_skill_control", "heading",         "long_range_drive",  "chip_shot_control",
    "long_range_shooting", "knuckle_shot",      "dipping_shots",     "rising_shots",
    "acrobatic_finishing", "heel_trick",        "first_time_shot",   "one_touch_pass",
    "through_passing",    "weighted_pass",      "pinpoint_crossing", "outside_curler",
    "rabona",             "no_look_pass",       "low_lofted_pass",   "gk_low_punt",
    "gk_high_punt",       "long_throw",         "gk_long_throw",     "penalty_specialist",
    "gk_penalty_saver",   "gamesmanship",       "man_marking",       "track_back",
    "interception",       "acrobatic_clear",    "captaincy",         "super_sub",
    "fighting_spirit",    "speed_merchant",     "target_man",
};

// The rating a skill leans on, so inference hands it to the players who can
// actually use it.
const char* const skillAffinityStat[skillCount] = {
    "technical_dribble",          // scissors feint
    "technical_dribble",          // double touch
    "technical_dribble",          // flip flap
    "technical_tightpossession",  // marseille turn
    "technical_ballcontrol",      // sombrero
    "technical_dribble",          // cross over turn
    "technical_tightpossession",  // cut behind & turn
    "technical_ballcontrol",      // scotch move
    "technical_tightpossession",  // step on skill control
    "technical_header",           // heading
    "physical_shotpower",         // long range drive
    "technical_shot",             // chip shot control
    "physical_shotpower",         // long range shooting
    "physical_shotpower",         // knuckle shot
    "technical_curl",             // dipping shots
    "technical_shot",             // rising shots
    "technical_volley",           // acrobatic finishing
    "technical_ballcontrol",      // heel trick
    "technical_volley",           // first-time shot
    "technical_shortpass",        // one-touch pass
    "mental_vision",              // through passing
    "technical_shortpass",        // weighted pass
    "technical_highpass",         // pinpoint crossing
    "technical_curl",             // outside curler
    "technical_dribble",          // rabona
    "mental_vision",              // no look pass
    "technical_highpass",         // low lofted pass
    "gk_clearing",                // gk low punt
    "gk_clearing",                // gk high punt
    "physical_contact",           // long throw
    "gk_clearing",                // gk long throw
    "technical_setpiece",         // penalty specialist
    "gk_reflexes",                // gk penalty saver
    "mental_aggression",          // gamesmanship
    "technical_ballwinning",      // man marking
    "mental_workrate",            // track back
    "technical_interceptions",    // interception
    "technical_ballwinning",      // acrobatic clear
    "mental_calmness",            // captaincy
    "physical_stamina",           // super-sub
    "mental_resilience",          // fighting spirit
    "physical_acceleration",      // speed merchant
    "technical_header",           // target man
};

// Lowercases and drops separators so "Cut Behind & Turn", "cut_behind_turn" and
// "cutbehindturn" all resolve to the same skill.
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

float Clamp01(float value) {
  return std::max(0.0f, std::min(value, 1.0f));
}

bool IsKeeper(e_PlayerRole role) {
  return role == e_PlayerRole_GK;
}
bool IsBack(e_PlayerRole role) {
  return role == e_PlayerRole_CB || role == e_PlayerRole_LB || role == e_PlayerRole_RB;
}
bool IsWide(e_PlayerRole role) {
  return role == e_PlayerRole_LB || role == e_PlayerRole_RB || role == e_PlayerRole_LM ||
         role == e_PlayerRole_RM;
}
bool IsAttacker(e_PlayerRole role) {
  return role == e_PlayerRole_AM || role == e_PlayerRole_CF || role == e_PlayerRole_LM ||
         role == e_PlayerRole_RM;
}

const float speedMerchantAccelerationBonus = 0.08f;
const float speedMerchantCalmnessPenalty = 0.25f;
const float headerBonus = 0.12f;
const float targetManShieldingRadius = 0.08f;
const float rollingBallSpeed = 1.0f;
const float firstTimeShotFullSpeed = 12.0f;
const float firstTimeShotPowerBonus = 0.15f;
const float shotSpin = 6.0f;

struct FeintClipRule {
  const char* token;
  Feint feint;
};
// First match wins: "cruijff" before the plain kick feint, "kick" before the
// generic feintrun family.
const FeintClipRule feintClipRules[] = {
    {"cruijff", Feint::CutBehindTurn},  {"rabona", Feint::Rabona},
    {"roulette", Feint::MarseilleTurn}, {"chapeau", Feint::Sombrero},
    {"chapeu", Feint::Sombrero},        {"meialua", Feint::MarseilleTurn},
    {"lift", Feint::Sombrero},
    {"accordion", Feint::StepOn},       {"doubletouch", Feint::DoubleTouch},
    {"outout", Feint::CrossOverTurn},   {"scissors", Feint::Scissors},
    {"sciin", Feint::Scissors},         {"sciout", Feint::Scissors},
    {"stepover", Feint::Scissors},      {"nutmeg", Feint::Nutmeg},
    {"through", Feint::Nutmeg},         {"bodyfake", Feint::BodyFake},
    {"kick", Feint::KickFeint},         {"feintrun_", Feint::BodyFake},
};

const char* const feintNames[static_cast<int>(Feint::Count)] = {
    "none",   "kick_feint",  "cut_behind_turn", "sombrero",       "scissors",        "nutmeg",
    "double_touch", "body_fake", "rabona",      "marseille_turn", "cross_over_turn", "step_on",
};

std::string ClipStem(const std::string& clipName) {
  const size_t slash = clipName.find_last_of("/\\");
  std::string stem = slash == std::string::npos ? clipName : clipName.substr(slash + 1);
  const size_t dot = stem.rfind(".anim");
  if (dot != std::string::npos)
    stem.erase(dot);
  return stem;
}

}  // namespace

Skill GetAt(int index) {
  return static_cast<Skill>(std::max(0, std::min(index, skillCount - 1)));
}

std::string GetName(Skill skill) {
  const int index = static_cast<int>(skill);
  return index >= 0 && index < skillCount ? skillNames[index] : "";
}

Mask Parse(const std::string& list) {
  Mask mask = maskNone;
  size_t start = 0;
  while (start <= list.size()) {
    const size_t separator = list.find(',', start);
    const size_t end = separator == std::string::npos ? list.size() : separator;
    const std::string key = Normalize(list.substr(start, end - start));

    if (!key.empty()) {
      // Names from before the PES list: the same cards under their old labels.
      if (key == "knuckleballer")
        mask |= Bit(Skill::KnuckleShot);
      for (int i = 0; i < skillCount; i++) {
        if (key == Normalize(skillNames[i]))
          mask |= Bit(GetAt(i));
      }
    }

    if (separator == std::string::npos)
      break;
    start = separator + 1;
  }
  return mask;
}

std::string Serialize(Mask mask) {
  std::string result;
  for (int i = 0; i < skillCount; i++) {
    if (!Has(mask, GetAt(i)))
      continue;
    if (!result.empty())
      result += ",";
    result += skillNames[i];
  }
  return result;
}

bool SuitsRole(Skill skill, e_PlayerRole role) {
  switch (skill) {
    case Skill::GkLowPunt:
    case Skill::GkHighPunt:
    case Skill::GkLongThrow:
    case Skill::GkPenaltySaver:
      return IsKeeper(role);
    case Skill::Captaincy:
    case Skill::FightingSpirit:
    case Skill::SuperSub:
    case Skill::Gamesmanship:
      return true;
    case Skill::ManMarking:
    case Skill::Interception:
    case Skill::AcrobaticClear:
      return IsBack(role) || role == e_PlayerRole_DM || role == e_PlayerRole_CM;
    case Skill::TrackBack:
      return !IsKeeper(role) && !IsBack(role);
    case Skill::PinpointCrossing:
    case Skill::LongThrow:
      return IsWide(role);
    case Skill::TargetMan:
      return role == e_PlayerRole_CF || role == e_PlayerRole_CB;
    case Skill::LongRangeDrive:
    case Skill::LongRangeShooting:
    case Skill::KnuckleShot:
    case Skill::DippingShots:
    case Skill::RisingShots:
      return !IsKeeper(role) && !IsBack(role);
    case Skill::ChipShotControl:
    case Skill::AcrobaticFinishing:
    case Skill::FirstTimeShot:
      return IsAttacker(role) || role == e_PlayerRole_CM;
    case Skill::ScissorsFeint:
    case Skill::DoubleTouch:
    case Skill::FlipFlap:
    case Skill::MarseilleTurn:
    case Skill::Sombrero:
    case Skill::CrossOverTurn:
    case Skill::CutBehindTurn:
    case Skill::ScotchMove:
    case Skill::StepOnSkillControl:
    case Skill::HeelTrick:
    case Skill::Rabona:
      return IsAttacker(role) || role == e_PlayerRole_CM || role == e_PlayerRole_LB ||
             role == e_PlayerRole_RB;
    default:
      return !IsKeeper(role);
  }
}

Mask Infer(int seed, e_PlayerRole role, const PlayerData& data) {
  Mask mask = maskNone;
  int given = 0;
  // PES's cap of ten, visited in a seeded order so the cap does not always
  // favour the low bits.
  const int offset = static_cast<int>(Hash(seed, 3) % static_cast<unsigned int>(skillCount));
  for (int step = 0; step < skillCount && given < 10; step++) {
    const int i = (offset + step) % skillCount;
    const Skill skill = GetAt(i);
    if (!SuitsRole(skill, role))
      continue;
    const float rating = Clamp01(data.GetStat(skillAffinityStat[i]));
    // A 0.9 rating gives a 30% chance per skill, a 0.5 rating 10%: a mid-table
    // squad averages three or four cards a man, as PES's do.
    const float chance = std::min(0.6f, std::max(0.0f, (rating - 0.3f) * 0.5f));
    if (Unit(seed, 200 + i) < chance) {
      mask |= Bit(skill);
      given++;
    }
  }
  // Nobody outfield is left completely without character.
  if (mask == maskNone && !IsKeeper(role))
    mask = Bit(Skill::OneTouchPass);
  return mask;
}

// --- Trick moves ---

const char* GetFeintName(Feint feint) {
  const int index = static_cast<int>(feint);
  return index >= 0 && index < static_cast<int>(Feint::Count) ? feintNames[index] : "";
}

Feint FeintFromClipName(const std::string& clipName) {
  const std::string stem = ClipStem(clipName);
  if (stem.find("pes_feint") != 0)
    return Feint::None;
  for (const FeintClipRule& rule : feintClipRules) {
    if (stem.find(rule.token) != std::string::npos)
      return rule.feint;
  }
  return Feint::None;
}

int FeintAngleFromClipName(const std::string& clipName) {
  // The angle is the first three-digit token after the velocity pair:
  // "..._3_3_f090_out", "..._0_3_045_in_...", "..._3_3_s_000_y4".
  const std::string stem = ClipStem(clipName);
  for (size_t i = 0; i + 3 <= stem.size(); i++) {
    if (stem[i] != '_')
      continue;
    size_t start = i + 1;
    if (start < stem.size() && stem[start] == 'f')
      start++;
    if (start + 3 > stem.size())
      break;
    if (!std::isdigit(static_cast<unsigned char>(stem[start])) ||
        !std::isdigit(static_cast<unsigned char>(stem[start + 1])) ||
        !std::isdigit(static_cast<unsigned char>(stem[start + 2])))
      continue;
    if (start + 3 < stem.size() && stem[start + 3] != '_' && stem[start + 3] != '.')
      continue;
    return std::atoi(stem.substr(start, 3).c_str());
  }
  return -1;
}

bool Unlocks(Mask skills, PlayingStyles::ComMask com, Feint feint) {
  switch (feint) {
    case Feint::CutBehindTurn:
      return Has(skills, Skill::CutBehindTurn);
    case Feint::Sombrero:
      return Has(skills, Skill::Sombrero);
    case Feint::Scissors:
      return Has(skills, Skill::ScissorsFeint);
    case Feint::DoubleTouch:
      return Has(skills, Skill::DoubleTouch);
    case Feint::Rabona:
      return Has(skills, Skill::Rabona);
    case Feint::MarseilleTurn:
      return Has(skills, Skill::MarseilleTurn);
    case Feint::CrossOverTurn:
      return Has(skills, Skill::CrossOverTurn);
    case Feint::StepOn:
      return Has(skills, Skill::StepOnSkillControl);
    case Feint::KickFeint:
    case Feint::Nutmeg:
    case Feint::BodyFake:
      // PES lets anybody play these; only the Trickster bothers.
      return PlayingStyles::Has(com, PlayingStyles::Com::Trickster);
    default:
      return false;
  }
}

Feint PickFeint(Mask skills, PlayingStyles::ComMask com, const FeintSituation& situation,
                float roll, float pick) {
  if (situation.opponentDistance < feintMinOpponentDistance ||
      situation.opponentDistance > feintMaxOpponentDistance ||
      std::fabs(situation.opponentAngle) > feintMaxOpponentAngle)
    return Feint::None;

  const bool trickster = PlayingStyles::Has(com, PlayingStyles::Com::Trickster);
  if (roll >= (trickster ? tricksterFeintAttemptChance : feintAttemptChance))
    return Feint::None;

  // The moves he can play, filtered by geometry: a turn-type move wants a real
  // change of direction, a beat-the-man move wants to carry on past him.
  const bool turning = std::fabs(situation.turnAngle) > 0.35f * 3.14159265f;
  Feint candidates[static_cast<int>(Feint::Count)];
  int count = 0;
  for (int i = 1; i < static_cast<int>(Feint::Count); i++) {
    const Feint feint = static_cast<Feint>(i);
    if (!Unlocks(skills, com, feint))
      continue;
    const bool turnMove = feint == Feint::CutBehindTurn || feint == Feint::MarseilleTurn ||
                          feint == Feint::StepOn || feint == Feint::CrossOverTurn;
    if (turnMove != turning)
      continue;
    candidates[count++] = feint;
  }
  if (count == 0)
    return Feint::None;
  const int index = std::min(count - 1, static_cast<int>(Clamp01(pick) * count));
  return candidates[index];
}

bool FeintFumbled(float tightPossession, float dribble, float roll) {
  const float control = Clamp01(0.5f * tightPossession + 0.5f * dribble);
  // A 0.5-rated dribbler fumbles nearly half his tricks, a 0.9 one in eight;
  // even the best loses one in twenty.
  return roll >= 0.4f + 0.55f * control * control;
}

blunted::Vector3 FumbleTouch(const blunted::Vector3& touch, float noiseSample) {
  const float sample = std::max(-1.0f, std::min(noiseSample, 1.0f));
  return touch.GetRotated2D(sample * 0.35f * 3.14159265f) * 1.6f;
}

// --- Shooting ---

float GetShotAppetite(Mask mask) {
  float appetite = 1.0f;
  if (Has(mask, Skill::FirstTimeShot))
    appetite += 0.1f;
  if (Has(mask, Skill::LongRangeDrive))
    appetite += 0.15f;
  if (Has(mask, Skill::LongRangeShooting))
    appetite += 0.1f;
  return std::max(0.55f, std::min(appetite, 2.0f));
}

float GetShootingRangeBonus(Mask mask) {
  float bonus = 0.0f;
  if (Has(mask, Skill::LongRangeDrive))
    bonus += 3.0f;
  if (Has(mask, Skill::LongRangeShooting))
    bonus += 2.0f;
  if (Has(mask, Skill::KnuckleShot))
    bonus += 1.0f;
  return bonus;
}

blunted::Vector3 ApplyShotSpin(Mask mask, const blunted::Vector3& rotVec,
                               const blunted::Vector3& shotDirection, float shotDistance,
                               float noiseSample) {
  blunted::Vector3 result = rotVec;
  if (Has(mask, Skill::KnuckleShot) && shotDistance >= knuckleballMinDistance) {
    const float sample = std::max(-1.0f, std::min(noiseSample, 1.0f));
    // Longer shots have more time to wobble, up to the hard bound.
    const float distanceFactor =
        std::min((shotDistance - knuckleballMinDistance) / knuckleballMinDistance, 1.0f);
    result.coords[1] += sample * knuckleballMaxSpin * (0.4f + 0.6f * distanceFactor);
  }
  // Topspin rolls the ball forward over its direction of travel (the pass
  // touch's "forwardness"); backspin is its negative.
  const blunted::Vector3 direction = shotDirection.Get2D().GetNormalized(blunted::Vector3(0));
  float spin = 0.0f;
  if (Has(mask, Skill::DippingShots))
    spin += shotSpin;
  if (Has(mask, Skill::RisingShots))
    spin -= shotSpin;
  result.coords[0] += direction.coords[1] * spin;
  result.coords[1] += direction.coords[0] * spin;
  return result;
}

bool WantsChip(Mask mask, float keeperDistanceFromShooter, float keeperDistanceOffLine) {
  return Has(mask, Skill::ChipShotControl) && keeperDistanceFromShooter < 12.0f &&
         keeperDistanceOffLine > 4.0f;
}

float GetVolleyEase(Mask mask, float baseEase) {
  if (!Has(mask, Skill::AcrobaticFinishing))
    return baseEase;
  return std::min(1.0f, baseEase + 0.5f * (1.0f - baseEase));
}

float GetFirstTimeShotPowerMultiplier(Mask mask, bool isFirstTimeShot, float ballSpeed) {
  if (!isFirstTimeShot || !Has(mask, Skill::FirstTimeShot) || ballSpeed < rollingBallSpeed)
    return 1.0f;
  // The quicker the ball is travelling, the more there is to time well.
  const float speedFactor = std::min(ballSpeed / firstTimeShotFullSpeed, 1.0f);
  return 1.0f + firstTimeShotPowerBonus * speedFactor;
}

float GetHeaderMultiplier(Mask mask) {
  return Has(mask, Skill::Heading) || Has(mask, Skill::TargetMan) ? 1.0f + headerBonus : 1.0f;
}

// --- Passing ---

float GetQuickReleaseAccuracyPenalty(Mask mask, unsigned long timeInPossession_ms,
                                     float basePenalty) {
  if (Has(mask, Skill::OneTouchPass) && timeInPossession_ms <= oneTouchWindow_ms)
    return 0.0f;
  return basePenalty;
}

float GetBodyDirectionPassPenalty(Mask mask, float basePenalty) {
  if (!Has(mask, Skill::NoLookPass))
    return basePenalty;
  return std::min(1.0f, basePenalty + 0.5f * (1.0f - basePenalty));
}

float GetThroughBallBonus(Mask mask, float baseBonus) {
  return Has(mask, Skill::ThroughPassing) ? baseBonus + 0.2f : baseBonus;
}

float GetPassDifficultyMultiplier(Mask mask, e_FunctionType passType, bool isCross) {
  if (passType == e_FunctionType_ShortPass || passType == e_FunctionType_LongPass)
    return Has(mask, Skill::WeightedPass) ? 0.7f : 1.0f;
  if (passType == e_FunctionType_HighPass) {
    if (isCross)
      return Has(mask, Skill::PinpointCrossing) ? 0.6f : 1.0f;
    return Has(mask, Skill::LowLoftedPass) ? 0.8f : 1.0f;
  }
  return 1.0f;
}

float GetPassCurve(Mask mask, float baseAmount) {
  return Has(mask, Skill::OutsideCurler) ? baseAmount * 1.6f : baseAmount;
}

blunted::Vector3 ShapePassTouch(Mask mask, e_FunctionType passType, bool isClearance,
                                const blunted::Vector3& touch) {
  blunted::Vector3 result = touch;
  if (passType == e_FunctionType_HighPass && !isClearance && Has(mask, Skill::LowLoftedPass)) {
    // A flatter, quicker lofted ball: the height goes into pace.
    result.coords[0] *= 1.08f;
    result.coords[1] *= 1.08f;
    result.coords[2] *= 0.85f;
  }
  if (isClearance && Has(mask, Skill::AcrobaticClear))
    result *= 1.15f;
  return result;
}

// --- Defending ---

float GetMarkingQualityBonus(Mask mask) {
  return Has(mask, Skill::ManMarking) ? 0.15f : 0.0f;
}

float GetTrackBackDepth(Mask mask, bool teamHasPossession) {
  return !teamHasPossession && Has(mask, Skill::TrackBack) ? -3.0f : 0.0f;
}

float GetBallDuelLikeliness(Mask mask, float baseLikeliness) {
  if (!Has(mask, Skill::Interception))
    return baseLikeliness;
  return std::min(1.0f, baseLikeliness + 0.15f);
}

float GetFoulScoreBonus(Mask mask) {
  return Has(mask, Skill::Gamesmanship) ? 0.1f : 0.0f;
}

// --- Keeper distribution ---

Distribution PickDistribution(Mask mask, float roll) {
  // A keeper with several cards varies his game; a plain one has none of them.
  Distribution options[3];
  int count = 0;
  if (Has(mask, Skill::GkLongThrow))
    options[count++] = Distribution::LongThrow;
  if (Has(mask, Skill::GkLowPunt))
    options[count++] = Distribution::LowPunt;
  if (Has(mask, Skill::GkHighPunt))
    options[count++] = Distribution::HighPunt;
  if (count == 0)
    return Distribution::Default;
  // Three times in four he plays to his card; the rest stays the plain hoof.
  if (roll >= 0.75f)
    return Distribution::Default;
  return options[std::min(count - 1, static_cast<int>(Clamp01(roll / 0.75f) * count))];
}

float GetPenaltyReflexes(Mask mask, float reflexes) {
  return Has(mask, Skill::GkPenaltySaver) ? std::min(1.0f, reflexes + 0.15f) : reflexes;
}

float GetPenaltyRating(Mask mask, float baseRating) {
  return Has(mask, Skill::PenaltySpecialist) ? std::min(1.0f, baseRating + 0.2f) : baseRating;
}

// --- Mentality ---

float GetStumbleChanceMultiplier(Mask ownMask, bool captainOnPitch) {
  float multiplier = 1.0f;
  if (captainOnPitch)
    multiplier *= 0.8f;
  if (Has(ownMask, Skill::FightingSpirit))
    multiplier *= 0.7f;
  return multiplier;
}

float GetFatigueDrainMultiplier(Mask mask) {
  return Has(mask, Skill::FightingSpirit) ? 0.85f : 1.0f;
}

float GetSubstituteBonus(Mask mask, bool isOnPitch) {
  return !isOnPitch && Has(mask, Skill::SuperSub) ? 0.1f : 0.0f;
}

// --- Speed Merchant / Target Man ---

float GetAccelerationMultiplier(Mask mask) {
  return Has(mask, Skill::SpeedMerchant) ? 1.0f + speedMerchantAccelerationBonus : 1.0f;
}

float GetCalmnessAtSpeed(Mask mask, float baseCalmness, float speedFactor) {
  if (!Has(mask, Skill::SpeedMerchant))
    return baseCalmness;
  return std::max(0.0f, baseCalmness - speedMerchantCalmnessPenalty * Clamp01(speedFactor));
}

float GetShieldingRadiusBonus(Mask mask, bool isStationary) {
  if (!isStationary || !Has(mask, Skill::TargetMan))
    return 0.0f;
  return targetManShieldingRadius;
}

}  // namespace PlayerSkills

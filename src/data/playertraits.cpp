#include "playertraits.hpp"

#include <algorithm>
#include <cctype>

namespace PlayerTraits {

namespace {

const e_Trait allTraits[traitCount] = {
    e_Trait_SpeedMerchant,     e_Trait_TargetMan,     e_Trait_Knuckleballer,
    e_Trait_OneTouchPass,      e_Trait_FirstTimeShot, e_Trait_GoalPoacher,
    e_Trait_CreativePlaymaker, e_Trait_FoxInTheBox,   e_Trait_LongRangeShooter,
    e_Trait_ProlificWinger,    e_Trait_BoxToBox,      e_Trait_Anchorman,
};

// Lowercases and drops separators so "Target Man", "target_man" and "targetman"
// all resolve to the same trait.
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

const float speedMerchantAccelerationBonus = 0.08f;
const float speedMerchantCalmnessPenalty = 0.25f;
const float targetManHeaderBonus = 0.12f;
const float targetManShieldingRadius = 0.08f;
// A ball slower than this counts as dead, not rolling.
const float rollingBallSpeed = 1.0f;
const float firstTimeShotFullSpeed = 12.0f;
const float firstTimeShotPowerBonus = 0.15f;
const float playmakerSpaceEmphasis = 0.35f;

}  // namespace

e_Trait GetTraitAt(int index) {
  return allTraits[std::max(0, std::min(index, traitCount - 1))];
}

std::string GetName(e_Trait trait) {
  switch (trait) {
    case e_Trait_SpeedMerchant:
      return "speed_merchant";
    case e_Trait_TargetMan:
      return "target_man";
    case e_Trait_Knuckleballer:
      return "knuckleballer";
    case e_Trait_OneTouchPass:
      return "one_touch_pass";
    case e_Trait_FirstTimeShot:
      return "first_time_shot";
    case e_Trait_GoalPoacher:
      return "goal_poacher";
    case e_Trait_CreativePlaymaker:
      return "creative_playmaker";
    case e_Trait_FoxInTheBox:
      return "fox_in_the_box";
    case e_Trait_LongRangeShooter:
      return "long_range_shooter";
    case e_Trait_ProlificWinger:
      return "prolific_winger";
    case e_Trait_BoxToBox:
      return "box_to_box";
    case e_Trait_Anchorman:
      return "anchorman";
    default:
      return "";
  }
}

bool Has(TraitMask mask, e_Trait trait) {
  return (mask & static_cast<TraitMask>(trait)) != 0;
}

TraitMask Parse(const std::string& list) {
  TraitMask mask = traitMaskNone;

  size_t start = 0;
  while (start <= list.size()) {
    const size_t separator = list.find(',', start);
    const size_t end = separator == std::string::npos ? list.size() : separator;
    const std::string key = Normalize(list.substr(start, end - start));

    if (!key.empty()) {
      for (int i = 0; i < traitCount; i++) {
        const e_Trait trait = allTraits[i];
        if (key == Normalize(GetName(trait)))
          mask |= static_cast<TraitMask>(trait);
      }
    }

    if (separator == std::string::npos)
      break;
    start = separator + 1;
  }

  return mask;
}

std::string Serialize(TraitMask mask) {
  std::string result;
  for (int i = 0; i < traitCount; i++) {
    const e_Trait trait = allTraits[i];
    if (!Has(mask, trait))
      continue;
    if (!result.empty())
      result += ",";
    result += GetName(trait);
  }
  return result;
}

namespace {

// A small deterministic hash, so a player's flair never changes between matches
// and no two neighbouring ids get the same set.
unsigned int PlayerHash(int playerDatabaseID, int salt) {
  unsigned int hash = static_cast<unsigned int>(playerDatabaseID) * 2654435761u;
  hash ^= static_cast<unsigned int>(salt) * 2246822519u;
  hash ^= hash >> 13;
  hash *= 3266489917u;
  return hash ^ (hash >> 16);
}

// Whether a style makes sense for the position a player is fielded in.
bool SuitsRole(e_Trait trait, e_PlayerRole role) {
  const bool isForward = role == e_PlayerRole_CF;
  const bool isWide = role == e_PlayerRole_LM || role == e_PlayerRole_RM ||
                      role == e_PlayerRole_LB || role == e_PlayerRole_RB;
  const bool isMidfield = role == e_PlayerRole_CM || role == e_PlayerRole_AM ||
                          role == e_PlayerRole_DM || role == e_PlayerRole_LM ||
                          role == e_PlayerRole_RM;
  const bool isDefender = role == e_PlayerRole_CB || role == e_PlayerRole_LB ||
                          role == e_PlayerRole_RB || role == e_PlayerRole_DM;

  switch (trait) {
    case e_Trait_GoalPoacher:
    case e_Trait_FoxInTheBox:
      return isForward;
    case e_Trait_TargetMan:
      return isForward || role == e_PlayerRole_CB;
    case e_Trait_ProlificWinger:
      return isWide;
    case e_Trait_CreativePlaymaker:
      return role == e_PlayerRole_AM || role == e_PlayerRole_CM;
    case e_Trait_Anchorman:
      return role == e_PlayerRole_DM || role == e_PlayerRole_CB;
    case e_Trait_BoxToBox:
      return isMidfield;
    case e_Trait_LongRangeShooter:
    case e_Trait_Knuckleballer:
      return !isDefender || role == e_PlayerRole_DM;
    case e_Trait_SpeedMerchant:
      return role != e_PlayerRole_GK;
    default:
      // One-touch passing and first-time shooting suit anybody outfield.
      return role != e_PlayerRole_GK;
  }
}

}  // namespace

TraitMask AssignForPlayer(int playerDatabaseID, e_PlayerRole role, float shotStat) {
  if (role == e_PlayerRole_GK)
    return traitMaskNone;

  // One signature style, plus a chance of one or two more, so a squad has a few
  // stand-out players rather than eleven identical ones.
  const unsigned int roll = PlayerHash(playerDatabaseID, 1);
  const int wanted = 1 + static_cast<int>(roll % 3u);

  TraitMask mask = traitMaskNone;
  int given = 0;
  for (int attempt = 0; attempt < traitCount * 2 && given < wanted; attempt++) {
    const e_Trait candidate = allTraits[PlayerHash(playerDatabaseID, 7 + attempt) %
                                        static_cast<unsigned int>(traitCount)];
    if (Has(mask, candidate) || !SuitsRole(candidate, role))
      continue;

    // Shooting from distance belongs to the players who can actually finish.
    if (candidate == e_Trait_LongRangeShooter) {
      const float threshold = 0.35f + (1.0f - std::max(0.0f, std::min(shotStat, 1.0f))) * 0.6f;
      if (static_cast<float>(PlayerHash(playerDatabaseID, 31 + attempt) % 1000u) / 1000.0f <
          threshold)
        continue;
    }

    mask |= static_cast<TraitMask>(candidate);
    given++;
  }

  // Nobody should be left completely without character.
  if (mask == traitMaskNone)
    mask = static_cast<TraitMask>(e_Trait_FirstTimeShot);
  return mask;
}

float GetShotAppetite(TraitMask mask) {
  float appetite = 1.0f;
  if (Has(mask, e_Trait_LongRangeShooter))
    appetite += 0.35f;
  if (Has(mask, e_Trait_FoxInTheBox))
    appetite += 0.3f;
  if (Has(mask, e_Trait_GoalPoacher))
    appetite += 0.25f;
  if (Has(mask, e_Trait_FirstTimeShot))
    appetite += 0.1f;
  // A playmaker looks for the pass before the shot.
  if (Has(mask, e_Trait_CreativePlaymaker))
    appetite -= 0.2f;
  if (Has(mask, e_Trait_Anchorman))
    appetite -= 0.25f;
  return std::max(0.55f, std::min(appetite, 2.0f));
}

float GetShootingRangeBonus(TraitMask mask) {
  float bonus = 0.0f;
  if (Has(mask, e_Trait_LongRangeShooter))
    bonus += 9.0f;
  if (Has(mask, e_Trait_Knuckleballer))
    bonus += 3.0f;
  if (Has(mask, e_Trait_FoxInTheBox))
    bonus -= 2.0f;  // he wants it inside the six-yard box
  return std::max(0.0f, std::min(bonus, 14.0f));
}

float GetAccelerationMultiplier(TraitMask mask) {
  return Has(mask, e_Trait_SpeedMerchant) ? 1.0f + speedMerchantAccelerationBonus : 1.0f;
}

float GetCalmnessAtSpeed(TraitMask mask, float baseCalmness, float speedFactor) {
  if (!Has(mask, e_Trait_SpeedMerchant))
    return baseCalmness;

  const float speed = std::max(0.0f, std::min(speedFactor, 1.0f));
  return std::max(0.0f, baseCalmness - speedMerchantCalmnessPenalty * speed);
}

float GetHeaderMultiplier(TraitMask mask) {
  return Has(mask, e_Trait_TargetMan) ? 1.0f + targetManHeaderBonus : 1.0f;
}

float GetShieldingRadiusBonus(TraitMask mask, bool isStationary) {
  if (!isStationary || !Has(mask, e_Trait_TargetMan))
    return 0.0f;
  return targetManShieldingRadius;
}

blunted::Vector3 ApplyKnuckleballSpin(TraitMask mask, const blunted::Vector3& rotVec,
                                      float shotDistance, float noiseSample) {
  if (!Has(mask, e_Trait_Knuckleballer) || shotDistance < knuckleballMinDistance)
    return rotVec;

  const float sample = std::max(-1.0f, std::min(noiseSample, 1.0f));
  // Longer shots have more time to wobble, up to the hard bound.
  const float distanceFactor =
      std::min((shotDistance - knuckleballMinDistance) / knuckleballMinDistance, 1.0f);
  const float spin = sample * knuckleballMaxSpin * (0.4f + 0.6f * distanceFactor);

  blunted::Vector3 result = rotVec;
  result.coords[1] += spin;
  return result;
}

float GetQuickReleaseAccuracyPenalty(TraitMask mask, unsigned long timeInPossession_ms,
                                     float basePenalty) {
  if (Has(mask, e_Trait_OneTouchPass) && timeInPossession_ms <= oneTouchWindow_ms)
    return 0.0f;
  return basePenalty;
}

float GetFirstTimeShotPowerMultiplier(TraitMask mask, bool isFirstTimeShot, float ballSpeed) {
  if (!isFirstTimeShot || !Has(mask, e_Trait_FirstTimeShot) || ballSpeed < rollingBallSpeed)
    return 1.0f;

  // The quicker the ball is travelling, the more there is to time well.
  const float speedFactor = std::min(ballSpeed / firstTimeShotFullSpeed, 1.0f);
  return 1.0f + firstTimeShotPowerBonus * speedFactor;
}

float GetPoacherTargetX(TraitMask mask, float defaultX, float opponentOffsideLineX, int teamSide) {
  if (!Has(mask, e_Trait_GoalPoacher) || teamSide == 0)
    return defaultX;

  // Sit a stride on the own-goal side of the line, whatever the formation says.
  const float side = static_cast<float>(teamSide > 0 ? 1 : -1);
  return opponentOffsideLineX + side * poacherOffsideCushion;
}

float GetSpaceRatingWeight(TraitMask mask, float baseWeight) {
  if (!Has(mask, e_Trait_CreativePlaymaker))
    return baseWeight;

  const float base = std::max(0.0f, std::min(baseWeight, 1.0f));
  // Shift the weight towards "find the pocket of space" without ever exceeding 1.
  return base + (1.0f - base) * playmakerSpaceEmphasis;
}

}  // namespace PlayerTraits

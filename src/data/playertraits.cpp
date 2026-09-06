#include "playertraits.hpp"

#include <algorithm>
#include <cctype>

namespace PlayerTraits {

namespace {

const e_Trait allTraits[traitCount] = {
    e_Trait_SpeedMerchant, e_Trait_TargetMan,     e_Trait_Knuckleballer,
    e_Trait_OneTouchPass,  e_Trait_FirstTimeShot,
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

// Whether a skill makes sense for the position a player is fielded in.
bool SuitsRole(e_Trait trait, e_PlayerRole role) {
  const bool isDefender = role == e_PlayerRole_CB || role == e_PlayerRole_LB ||
                          role == e_PlayerRole_RB || role == e_PlayerRole_DM;

  switch (trait) {
    case e_Trait_TargetMan:
      return role == e_PlayerRole_CF || role == e_PlayerRole_CB;
    case e_Trait_Knuckleballer:
      return !isDefender || role == e_PlayerRole_DM;
    default:
      // Pace, one-touch passing and first-time shooting suit anybody outfield.
      return role != e_PlayerRole_GK;
  }
}

}  // namespace

TraitMask AssignForPlayer(int playerDatabaseID, e_PlayerRole role, float shotStat) {
  if (role == e_PlayerRole_GK)
    return traitMaskNone;

  // One signature skill, plus a chance of one or two more, so a squad has a few
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

    // Knuckleballs from distance belong to the players who can actually strike one.
    if (candidate == e_Trait_Knuckleballer) {
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
  if (Has(mask, e_Trait_FirstTimeShot))
    appetite += 0.1f;
  return std::max(0.55f, std::min(appetite, 2.0f));
}

float GetShootingRangeBonus(TraitMask mask) {
  return Has(mask, e_Trait_Knuckleballer) ? 3.0f : 0.0f;
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

}  // namespace PlayerTraits

#include "playertraits.hpp"

#include <algorithm>
#include <cctype>

namespace PlayerTraits {

namespace {

const e_Trait allTraits[traitCount] = {
    e_Trait_SpeedMerchant, e_Trait_TargetMan,   e_Trait_Knuckleballer,     e_Trait_OneTouchPass,
    e_Trait_FirstTimeShot, e_Trait_GoalPoacher, e_Trait_CreativePlaymaker,
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

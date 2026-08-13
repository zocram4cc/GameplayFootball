// Player traits ("specialties" / PES-style cards): conditional stat modifiers
// and logic overrides that give individual players a recognisable signature.
// See SIMULATION_IMPROVEMENT_PROPOSAL.md section 3A and TECHNICAL_ROADMAP.md
// section 4D.

#ifndef _HPP_PLAYERTRAITS
#define _HPP_PLAYERTRAITS

#include <string>

#include "base/math/vector3.hpp"

namespace PlayerTraits {

enum e_Trait {
  e_Trait_SpeedMerchant = 1 << 0,
  e_Trait_TargetMan = 1 << 1,
  e_Trait_Knuckleballer = 1 << 2,
  e_Trait_OneTouchPass = 1 << 3,
  e_Trait_FirstTimeShot = 1 << 4,
  e_Trait_GoalPoacher = 1 << 5,
  e_Trait_CreativePlaymaker = 1 << 6,
};

using TraitMask = unsigned int;

const TraitMask traitMaskNone = 0;
const int traitCount = 7;

// Long shots only start knuckling from this distance, in metres.
const float knuckleballMinDistance = 25.0f;
// Hard bound on the extra spin a knuckleball can pick up.
const float knuckleballMaxSpin = 12.0f;
// A pass released within this window counts as a one-touch pass.
const unsigned long oneTouchWindow_ms = 200;
// How far behind the opponent offside line a poacher lurks, in metres.
const float poacherOffsideCushion = 0.5f;

e_Trait GetTraitAt(int index);
std::string GetName(e_Trait trait);
bool Has(TraitMask mask, e_Trait trait);

// Comma-separated, case- and separator-insensitive; unknown entries are ignored.
TraitMask Parse(const std::string& list);
std::string Serialize(TraitMask mask);

// Speed Merchant.
float GetAccelerationMultiplier(TraitMask mask);
// `speedFactor` is 0 when standing still and 1 at top speed.
float GetCalmnessAtSpeed(TraitMask mask, float baseCalmness, float speedFactor);

// Target Man.
float GetHeaderMultiplier(TraitMask mask);
float GetShieldingRadiusBonus(TraitMask mask, bool isStationary);

// Knuckleballer. `noiseSample` is expected in [-1, 1] and is supplied by the
// caller, which keeps the physics deterministic and testable.
blunted::Vector3 ApplyKnuckleballSpin(TraitMask mask, const blunted::Vector3& rotVec,
                                      float shotDistance, float noiseSample);

// One-Touch Pass.
float GetQuickReleaseAccuracyPenalty(TraitMask mask, unsigned long timeInPossession_ms,
                                     float basePenalty);

// First-Time Shot. `ballSpeed` is the incoming ball speed in m/s.
float GetFirstTimeShotPowerMultiplier(TraitMask mask, bool isFirstTimeShot, float ballSpeed);

// Goal Poacher. `teamSide` is -1 or 1 and points towards the team's own goal.
float GetPoacherTargetX(TraitMask mask, float defaultX, float opponentOffsideLineX, int teamSide);

// Creative Playmaker: how heavily "distance from opponent" counts when rating a
// spot to move into.
float GetSpaceRatingWeight(TraitMask mask, float baseWeight);

}  // namespace PlayerTraits

#endif

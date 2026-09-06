// Player traits ("specialties" / PES-style skill cards): conditional stat
// modifiers and logic overrides that give individual players a recognisable
// signature. Playing Styles (how a player reads the game) live in
// playingstyles.hpp. See SIMULATION_IMPROVEMENT_PROPOSAL.md section 3A and
// TECHNICAL_ROADMAP.md section 4D.

#ifndef _HPP_PLAYERTRAITS
#define _HPP_PLAYERTRAITS

#include <string>

#include "../gametypes.hpp"
#include "base/math/vector3.hpp"

namespace PlayerTraits {

enum e_Trait {
  e_Trait_SpeedMerchant = 1 << 0,
  e_Trait_TargetMan = 1 << 1,
  e_Trait_Knuckleballer = 1 << 2,
  e_Trait_OneTouchPass = 1 << 3,
  e_Trait_FirstTimeShot = 1 << 4,
};

using TraitMask = unsigned int;

const TraitMask traitMaskNone = 0;
const int traitCount = 5;

// Long shots only start knuckling from this distance, in metres.
const float knuckleballMinDistance = 25.0f;
// Hard bound on the extra spin a knuckleball can pick up.
const float knuckleballMaxSpin = 12.0f;
// A pass released within this window counts as a one-touch pass.
const unsigned long oneTouchWindow_ms = 200;

e_Trait GetTraitAt(int index);
std::string GetName(e_Trait trait);
bool Has(TraitMask mask, e_Trait trait);

// Gives a player his flair when the database says nothing: deterministic from his
// id, suited to the position he plays and to how well he finishes (long-range
// knuckleballs belong to the players who can strike a ball), so every squad is
// varied and the same player behaves the same way in every match.
TraitMask AssignForPlayer(int playerDatabaseID, e_PlayerRole role, float shotStat);

// How readily this player has a go, and from how much further out (in metres).
float GetShotAppetite(TraitMask mask);
float GetShootingRangeBonus(TraitMask mask);

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

}  // namespace PlayerTraits

#endif

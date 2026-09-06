// PES 2021 Player Skills: the 41 skill cards a player carries (PES bit order),
// plus two engine-only specialties kept from the original traits module. Each
// skill bends a decision that already exists; a skill that is stored and never
// read is a defect. Playing Styles live in playingstyles.hpp.
// See docs/PES21_IMPORT.md.

#ifndef _HPP_PLAYERSKILLS
#define _HPP_PLAYERSKILLS

#include <string>

#include "../gametypes.hpp"
#include "base/math/vector3.hpp"
#include "playingstyles.hpp"

class PlayerData;

namespace PlayerSkills {

// PES EDIT bit order (Player Skills bitmask at 0x30:6), then the engine's own.
enum class Skill : unsigned int {
  ScissorsFeint = 0,
  DoubleTouch,
  FlipFlap,
  MarseilleTurn,
  Sombrero,
  CrossOverTurn,
  CutBehindTurn,
  ScotchMove,
  StepOnSkillControl,
  Heading,
  LongRangeDrive,
  ChipShotControl,
  LongRangeShooting,
  KnuckleShot,
  DippingShots,
  RisingShots,
  AcrobaticFinishing,
  HeelTrick,
  FirstTimeShot,
  OneTouchPass,
  ThroughPassing,
  WeightedPass,
  PinpointCrossing,
  OutsideCurler,
  Rabona,
  NoLookPass,
  LowLoftedPass,
  GkLowPunt,
  GkHighPunt,
  LongThrow,
  GkLongThrow,
  PenaltySpecialist,
  GkPenaltySaver,
  Gamesmanship,
  ManMarking,
  TrackBack,
  Interception,
  AcrobaticClear,
  Captaincy,
  SuperSub,
  FightingSpirit,
  // Engine-only, not on a PES card.
  SpeedMerchant,
  TargetMan,
  Count
};

using Mask = unsigned long long;
const Mask maskNone = 0;
const int skillCount = static_cast<int>(Skill::Count);
const int pesSkillCount = 41;

inline Mask Bit(Skill skill) {
  return 1ull << static_cast<unsigned int>(skill);
}
inline bool Has(Mask mask, Skill skill) {
  return (mask & Bit(skill)) != 0;
}
Skill GetAt(int index);
std::string GetName(Skill skill);

// Comma-separated snake_case tokens ("cut_behind_turn"); parsing is case- and
// separator-insensitive, accepts the pre-PES names ("knuckleballer") and
// ignores unknown entries.
Mask Parse(const std::string& list);
std::string Serialize(Mask mask);

// PES only hands a skill to a player whose position can use it.
bool SuitsRole(Skill skill, e_PlayerRole role);
// When the source names none: deterministic from the seed, legal for the role,
// each skill more likely the better the rating behind it. PES caps at ten.
Mask Infer(int seed, e_PlayerRole role, const PlayerData& data);

// --- Trick moves (PlayerController::_BallControlCommand) ---

// Families of the imported feint clips; the numeric value is the clip's
// specialvar1, so a ballcontrol query carrying it selects that move alone.
enum class Feint : int {
  None = 0,
  KickFeint,
  CutBehindTurn,
  Sombrero,
  Scissors,
  Nutmeg,
  DoubleTouch,
  BodyFake,
  Rabona,
  MarseilleTurn,
  CrossOverTurn,
  StepOn,
  Count
};
const char* GetFeintName(Feint feint);
// From the imported clip's file name (any path prefix, ".anim" optional).
Feint FeintFromClipName(const std::string& clipName);
// The entry angle in degrees the clip name encodes ("_f090_", "_045_"), or -1.
int FeintAngleFromClipName(const std::string& clipName);
// Whether this player may attempt the move: the skill it needs, or - for the
// moves PES lets anybody play - only when the COM Trickster card is on.
bool Unlocks(Mask skills, PlayingStyles::ComMask com, Feint feint);

struct FeintSituation {
  float opponentDistance = 100.0f;  // metres to the nearest opponent
  float opponentAngle = 0.0f;       // radians between facing and that opponent, absolute
  float turnAngle = 0.0f;           // radians between facing and where he wants to go, absolute
};
const float feintMinOpponentDistance = 1.0f;
const float feintMaxOpponentDistance = 4.5f;
const float feintMaxOpponentAngle = 0.5f * 3.14159265f;
// Per-decision odds of trying a move; the Trickster tries far more often.
const float feintAttemptChance = 0.12f;
const float tricksterFeintAttemptChance = 0.35f;
// The move to try, or None. `roll` and `pick` are uniform in [0, 1).
Feint PickFeint(Mask skills, PlayingStyles::ComMask com, const FeintSituation& situation,
                float roll, float pick);
// Whether the attempt goes wrong (heavy touch, ball runs loose); the better his
// close control and dribbling, the rarer.
bool FeintFumbled(float tightPossession, float dribble, float roll);
// The loose touch a fumbled move produces.
blunted::Vector3 FumbleTouch(const blunted::Vector3& touch, float noiseSample);

// --- Shooting (ElizaController, Humanoid shot touch) ---

// How readily he has a go (1 = neutral) and from how much further out (metres).
float GetShotAppetite(Mask mask);
float GetShootingRangeBonus(Mask mask);
const float knuckleballMinDistance = 25.0f;
const float knuckleballMaxSpin = 12.0f;
// Knuckle Shot wobbles long efforts, Dipping Shots add topspin, Rising Shots
// backspin. `shotDirection` is the 2D direction of travel, `noiseSample` in [-1, 1].
blunted::Vector3 ApplyShotSpin(Mask mask, const blunted::Vector3& rotVec,
                               const blunted::Vector3& shotDirection, float shotDistance,
                               float noiseSample);
// Chip Shot Control: whether to lift a finish over a keeper who has come out.
bool WantsChip(Mask mask, float keeperDistanceFromShooter, float keeperDistanceOffLine);
const float chipLoft = 0.5f;
// Acrobatic Finishing: easier connection with a ball moving relative to the body.
float GetVolleyEase(Mask mask, float baseEase);
// First-Time Shot. `ballSpeed` is the incoming ball speed in m/s.
float GetFirstTimeShotPowerMultiplier(Mask mask, bool isFirstTimeShot, float ballSpeed);
// Heading and Target Man.
float GetHeaderMultiplier(Mask mask);

// --- Passing ---

const unsigned long oneTouchWindow_ms = 200;
float GetQuickReleaseAccuracyPenalty(Mask mask, unsigned long timeInPossession_ms,
                                     float basePenalty);
// No Look Pass: the odds penalty for passing where he is not facing (0.3 .. 1).
float GetBodyDirectionPassPenalty(Mask mask, float basePenalty);
// Through Passing: the upside of the ball into a runner's path.
float GetThroughBallBonus(Mask mask, float baseBonus);
// Weighted Pass (ground), Pinpoint Crossing (a cross), Low Lofted Pass (other
// lofted balls): multiplier on the pass difficulty factor.
float GetPassDifficultyMultiplier(Mask mask, e_FunctionType passType, bool isCross);
// Outside Curler: how much a pass bends.
float GetPassCurve(Mask mask, float baseAmount);
// Low Lofted Pass flattens and quickens the lofted ball; Acrobatic Clear
// powers a clearance.
blunted::Vector3 ShapePassTouch(Mask mask, e_FunctionType passType, bool isClearance,
                                const blunted::Vector3& touch);

// --- Defending (TeamAIController, PlayerController) ---

// Man Marking: added to the marking-quality rating when assigning markers.
float GetMarkingQualityBonus(Mask mask);
// Track Back: metres deeper to sit when the team has lost the ball (negative).
float GetTrackBackDepth(Mask mask, bool teamHasPossession);
// Interception: how likely he thinks he wins a ball duel.
float GetBallDuelLikeliness(Mask mask, float baseLikeliness);
// Gamesmanship: bonus on the referee's foul score when he is the one brought down.
float GetFoulScoreBonus(Mask mask);

// --- Keeper distribution (ElizaController set piece / retainer) ---

enum class Distribution { Default, LongThrow, LowPunt, HighPunt };
Distribution PickDistribution(Mask mask, float roll);
// GK Penalty Saver: reflexes as seen from the spot.
float GetPenaltyReflexes(Mask mask, float reflexes);
// Penalty Specialist: rating and composure from twelve yards.
float GetPenaltyRating(Mask mask, float baseRating);

// --- Mentality ---

// Captaincy: a captain on the pitch steadies everybody; Fighting Spirit
// steadies himself. Multiplier on the chance of losing composure.
float GetStumbleChanceMultiplier(Mask ownMask, bool captainOnPitch);
// Fighting Spirit: slower to tire.
float GetFatigueDrainMultiplier(Mask mask);
// Super-sub: added to the rating when picking who comes off the bench.
float GetSubstituteBonus(Mask mask, bool isOnPitch);

// --- Speed Merchant / Target Man (engine specialties) ---

float GetAccelerationMultiplier(Mask mask);
// `speedFactor` is 0 when standing still and 1 at top speed.
float GetCalmnessAtSpeed(Mask mask, float baseCalmness, float speedFactor);
float GetShieldingRadiusBonus(Mask mask, bool isStationary);

}  // namespace PlayerSkills

#endif

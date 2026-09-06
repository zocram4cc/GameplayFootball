// PES 2021 Playing Styles: the one style a player has (how he reads the game
// off the ball and what he looks for on it) and the COM Playing Styles - the
// "playing cards" that only steer AI-controlled players. Stats stay untouched;
// styles only bend decisions that already exist (docs/PES21_IMPORT.md).

#ifndef _HPP_PLAYINGSTYLES
#define _HPP_PLAYINGSTYLES

#include <string>

#include "../gamedefines.hpp"

class PlayerData;

namespace PlayingStyles {

// PES EDIT index order; None is what the editor shows as "-".
enum class Player {
  None,
  GoalPoacher,
  DummyRunner,
  FoxInTheBox,
  TargetMan,
  CreativePlaymaker,
  ProlificWinger,
  RoamingFlank,
  CrossSpecialist,
  ClassicNo10,
  HolePlayer,
  BoxToBox,
  TheDestroyer,
  Orchestrator,
  AnchorMan,
  BuildUp,
  OffensiveFullBack,
  FullBackFinisher,
  DefensiveFullBack,
  ExtraFrontman,
  OffensiveGoalkeeper,
  DefensiveGoalkeeper,
  Count
};

// PES bit order.
enum class Com : unsigned int {
  Trickster = 1u << 0,
  MazingRun = 1u << 1,
  SpeedingBullet = 1u << 2,
  IncisiveRun = 1u << 3,
  LongBallExpert = 1u << 4,
  EarlyCross = 1u << 5,
  LongRanger = 1u << 6,
};
using ComMask = unsigned int;
const ComMask comMaskNone = 0;
const int comCount = 7;
Com GetComAt(int index);
bool Has(ComMask mask, Com card);

// Tokens are snake_case ("classic_no_10"); parsing is case- and
// separator-insensitive and ignores unknown entries.
Player ParsePlayer(const std::string& token);
std::string Serialize(Player style);
ComMask ParseCom(const std::string& list);
std::string SerializeCom(ComMask mask);

// PES only activates a style on a compatible registered position.
bool SuitsRole(Player style, e_PlayerRole role);

// When the source names none: deterministic from the seed, legal for the role,
// leaning towards what the player's own ratings are good at. Roughly one in
// four comes out with no style at all, as in PES squads.
Player InferPlayer(int seed, e_PlayerRole role, const PlayerData& data);
// Zero to five cards, each more likely the better the rating behind it.
ComMask InferCom(int seed, Player style, const PlayerData& data);

// --- Off the ball (TeamAIController::GetAdaptedFormationPosition) ---

// Metres to shift the formation spot towards the opponent goal (negative =
// drop deeper), depending on who has the ball.
float GetDepthOffset(Player style, bool teamHasPossession);
// Metres to shift towards the nearer touchline (negative = tuck inside).
float GetWidthOffset(Player style, bool teamHasPossession);
// Goal Poacher: sits a stride onside of the opponent offside line while his
// team attacks. `teamSide` is -1 or 1 and points towards the team's own goal.
const float poacherOffsideCushion = 0.5f;
float GetPoacherTargetX(Player style, float defaultX, float opponentOffsideLineX, int teamSide);
// How keen this player is to be the one sent on the attacking run: 0.5 is
// neutral, 0 never runs, 1 always volunteers.
float GetAttackingRunAffinity(Player style, ComMask com);
// Metres knocked off (negative) or added to his distance when picking the
// second presser, so a destroyer hunts and a playmaker does not.
float GetPressingDistanceBias(Player style);

// --- On the ball (ElizaController::GetOnTheBallCommands) ---

// How readily he has a go (1 = neutral) and from how much further out (metres).
float GetShotAppetite(Player style, ComMask com);
float GetShootingRangeBonus(Player style, ComMask com);
// Multiplier on the odds of a pass type, so the long-ball expert plays the
// long ball and the cross specialist the cross even when the short option is
// marginally safer.
float GetPassTypeBias(Player style, ComMask com, e_FunctionType passType);
// Creative Playmaker: how heavily "distance from opponent" counts when rating a
// spot to move into.
float GetSpaceRatingWeight(Player style, float baseWeight);

// --- Dribbling (AI_GetBestDribbleMovement) ---

// Attraction towards the opponent goal.
float GetDribbleDrive(Player style, ComMask com, float baseDrive);
// Repulsion from opponents; a trickster or mazing runner takes them on instead.
float GetOpponentRepel(Player style, ComMask com, float baseRepel);
// Pull towards the centre line while carrying the ball (0 = hold the lane).
float GetDribbleCenterPull(Player style, ComMask com, float baseCenterMagnet);
// The pace he carries it at: a target man holds it up, a speeding bullet bursts.
float GetDribbleVelocity(Player style, ComMask com, float desiredVelocity);

// --- Keeper (GoalieDefaultStrategy::RequestInput) ---

// Multiplier on how far out the keeper is willing to come.
float GetKeeperComeOutBias(Player style);

}  // namespace PlayingStyles

#endif

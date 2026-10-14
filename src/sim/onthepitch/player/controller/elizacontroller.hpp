// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_FOOTBALL_ONTHEPITCH_ELIZACONTROLLER
#define _HPP_FOOTBALL_ONTHEPITCH_ELIZACONTROLLER

#include "../../../gamedefines.hpp"
#include "onthepitch/pesdefenderdecisions.hpp"
#include "onthepitch/pesdodge.hpp"
#include "playercontroller.hpp"

struct Prerequisites;
class Strategy;
class MentalImage;
class Team;
class Player;

struct PreRating {  // for optimization: first calculate everything but passrating, since that one
                    // is slow to calculate. cull results before rating pass freedom

  int candidateID;

  float offenseRating;    // how close to goal are we?
  float distanceRating;   // how far away from possession player are we?
  float movementRating;   // prefer continuing in our currect direction
  float formationRating;  // prefer movement to our formation position
  float offsideRating;    // 0 == offside, 1 == not offside

  float totalRating;
};

bool PreRatingSortFunc(const PreRating& a, const PreRating& b);
bool SortPlayersDeepestFirst(Player* a, Player* b);

class ElizaController : public PlayerController {
public:
  ElizaController(Match* match);
  virtual ~ElizaController();

  virtual void RequestCommand(PlayerCommandQueue& commandQueue);
  virtual void Process();
  virtual Vector3 GetDirection();
  virtual float GetFloatVelocity();

  void LoadStrategies();

  float GetLazyVelocity(float desiredVelocityFloat);
  Vector3 GetSupportPosition(const MentalImage* mentalImage, const Vector3& basePosition);
  Vector3 GetSupportPosition_ForceField(const MentalImage* mentalImage, const Vector3& basePosition);

  virtual void Reset();

protected:
  void GetOnTheBallCommands(std::vector<PlayerCommand>& commandQueue, Vector3& rawInputDirection,
                            float& rawInputVelocityFloat);

  // PES 2021's engaging-defender decisions (docs/re/pes-re-defender-decisions.md):
  // the one ordered reaction table and the gates its entries read. Every
  // predicate is a member because it reads the controller's own state; every
  // number that comes from the game is in
  // pes::DefenderDecisionConstants, read from the install.
  void _ReactionCommands(PlayerCommandQueue& commandQueue);
  Player* _Carrier();
  // The five predicates GF can express, in the chooser's order.
  bool _TackleCandidate(const pes::DefenderDecisionConstants& k);
  bool _SlideCandidate(const pes::DefenderDecisionConstants& k);
  bool _BlockCandidate(const pes::DefenderDecisionConstants& k);
  bool _PassDuelCandidate(const pes::DefenderDecisionConstants& k);
  // The dodge predicates (entries 4-7). Each returns the dodge's clip fit:
  // the predicate's geometry fires only when the Dodge action's own clip
  // search finds a clip that solves it (FUN_1406b4580). `kind` is the
  // entry's dodge kind (cmd+0x1094); on a fit it is filled in.
  bool _DodgeBallCandidate(int* kind);
  bool _DodgeNetCandidate(int* kind);
  bool _DodgePostCandidate(int* kind);
  bool _DodgeHumanCandidate(int* kind);
  // The human dodge kind 0..7 (FUN_14073a3d0) for one opponent.
  int _HumanDodgeKind(Player* opp);
  // The dodge command: PES's Special path with the family and the selected
  // motion number (FUN_14073bc70 + FUN_1406ecbb0), so the clip plays and the
  // player carries the DODGE action for the contact reaction.
  void _DodgeCommand(PlayerCommandQueue& commandQueue, pes::DefenderReaction reaction, int kind);
  // PES's aggression level A for the tackle's from-behind tolerance and touch
  // lead: the ENFORCER style and the team's defensive mode.
  int _TackleAggressionLevel();
  // The three team terms of the slide's aggression ladder.
  pes::SlideAggressionInput _SlideAggression();
  // Whether the defender is the last line of his team's shape, and whether a
  // team mate covers him - the two safety terms.
  bool _IsLastLine();
  bool _HasCover(float towardsDeg);
  // Whether any team mate is already sliding.
  bool _AnyTeammateSliding();
  void _PassGetApproach(PlayerCommandQueue& commandQueue);
  void _PushChallenge(PlayerCommandQueue& commandQueue, const Vector3& contact,
                      e_FunctionType type);

  // PES 2021's ball-carrier decision layer (docs/re/pes-re-carrier-ai.md):
  // the plan the carrier is running and the frame stamps the think cadence
  // keeps. The plan is the running plan of report 4.4, re-chosen in the
  // priority order when its own end condition holds; the stamps are PES logic
  // frames, converted from this engine's ticks the way pesballphysics.hpp
  // converts the ball's per-tick factors.
  int carrierPlan = 0;        // pes::CarrierPlan
  int carrierPlanStamp = 0;   // the frame the plan was chosen
  int carrierBusyStamp = 0;   // the last frame inside a committed action

  void _AddPass(std::vector<PlayerCommand>& commandQueue, Player* target, e_FunctionType passType);
  void _AddPanicPass(std::vector<PlayerCommand>& commandQueue,
                          const std::vector<PlayerImage>& opponentPlayerImages);
  float _GetPassingOdds(Player* targetPlayer, e_FunctionType passType,
                        const std::vector<PlayerImage>& opponentPlayerImages,
                        float ballVelocityMultiplier = 1.0f);
  float _GetPassingOdds(const Vector3& target, e_FunctionType passType,
                        const std::vector<PlayerImage>& opponentPlayerImages,
                        float ballVelocityMultiplier = 1.0f);
  void _AddCelebration(std::vector<PlayerCommand>& commandQueue);

  Strategy* defenseStrategy;
  Strategy* midfieldStrategy;
  Strategy* offenseStrategy;
  Strategy* goalieStrategy;

  // PES 2021's keeper rush hysteresis (docs/re/pes-re-goalkeeper.md 10.3): a
  // keeper already rushing keeps going through a tighter race. Reset with the
  // rest of the controller, so a keeper who was chasing when play stopped does
  // not carry it into the next passage.
  bool keeperRushing = false;

  Vector3 lastDesiredDirection;
  float lastDesiredVelocity;
};

#endif

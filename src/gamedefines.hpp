// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_GAMEDEFINES
#define _HPP_GAMEDEFINES

#include <SDL2/SDL.h>  // for key ids

#include "base/math/vector3.hpp"
#include "defines.hpp"
#include "gametypes.hpp"
#include "onthepitch/velocitystate.hpp"
#include "onthepitch/player/humanoid/skinweights.hpp"

using namespace blunted;

extern unsigned long time_ms;
// PES6 digital control mode, quantizes some input to x degree angles
const bool quantizeDirection = true;

const float analogStickDeadzone = 0.75f;

// PES's main broadcast camera, solved back through this rig from the reference
// frames (docs/VGL26_REFERENCE.md, and the measurement in docs/PICTURE.md):
// the ad-board ring sits on the top edge of frame and a player at mid-pitch is
// about 6.8% of the frame's height. That is a LONG lens from far back and low -
// 55 m out, 18 m up, 18 degrees of elevation, 28 degrees vertical - not the
// wide, steep shot GF opened with (35 m out, 19 m up, 28 degrees down), which
// is what "the camera is 1.4x too close and shows no touchline" was: too steep
// to see the far side, too near to hold the width. Each of these four is the
// game's own knob, so a user can still shoot it however he likes.
const float _default_CameraZoom = 0.62f;    // 55 m back
const float _default_CameraHeight = 0.0f;   // 18 m up: 18 degrees, not 28
const float _default_CameraFOV = 0.9f;      // 28 degrees vertical / 48 across
const float _default_CameraAngleFactor = 0.4f;  // pans with play, as PES does

const float _default_Difficulty = 0.8f;
// Legacy normalized 5-25 minute match-duration setting. New code stores minutes explicitly.
const float _default_MatchDuration = 1.0f;

const float _default_QuantizedDirectionBias = 0.0f;

const float _default_AgilityFactor = 0.5f;
const float _default_AccelerationFactor = 0.5f;

const float _default_ShortPass_AutoDirection = 0.4f;
const float _default_ShortPass_AutoPower = 0.7f;
const float _default_ThroughPass_AutoDirection = 0.2f;
const float _default_ThroughPass_AutoPower = 0.7f;
const float _default_HighPass_AutoDirection = 0.2f;
const float _default_HighPass_AutoPower = 0.5f;
const float _default_Shot_AutoDirection = 0.2f;

const float distanceToVelocityMultiplier =
    2.6f;  // for example: when we need to travel 4 meters, we need to go at velo 4 *
           // distanceToVelocityMultiplier

const unsigned int ballPredictionSize_ms = 3000;
const unsigned int ballHistorySize_ms = 4000;

const float ballDistanceOptimizeThreshold = 10.0f;

const int playerNum = 11;

// how far into an animation the ball is usually touched
const unsigned int defaultTouchOffset_ms = 80;

const float defaultPlayerHeight = 1.92f;

const int temporalSmoother_history_ms = 20;

// #define dataSetSortable 1
#ifdef dataSetSortable
using DataSet = std::list<int>;
#else
using DataSet = std::deque<int>;
#endif

const SDL_Keycode defaultKeyIDs[18] = {
    SDLK_UP, SDLK_RIGHT, SDLK_DOWN, SDLK_LEFT, SDLK_w, SDLK_a, SDLK_s, SDLK_d,  SDLK_w,
    SDLK_a,  SDLK_s,     SDLK_d,    SDLK_q,    SDLK_z, SDLK_e, SDLK_c, SDLK_F1, SDLK_RETURN};

class Player;

enum e_Side { e_Side_Left, e_Side_Right };

enum e_FunctionType {
  e_FunctionType_None,
  e_FunctionType_Movement,
  e_FunctionType_BallControl,
  e_FunctionType_Trap,
  e_FunctionType_ShortPass,
  e_FunctionType_LongPass,
  e_FunctionType_HighPass,
  e_FunctionType_Header,
  e_FunctionType_Shot,
  e_FunctionType_Deflect,
  e_FunctionType_Catch,
  e_FunctionType_Interfere,
  e_FunctionType_Trip,
  e_FunctionType_Sliding,
  e_FunctionType_Special
};

// e_SetPiece and e_TouchType live in gametypes.hpp so the SDL-free rule
// modules can see them.

enum e_PlayerCommandModifier {
  e_PlayerCommandModifier_None = 0,
  e_PlayerCommandModifier_KnockOn = 1,
  // A trick move (specialVar1 = PlayerSkills::Feint) that goes wrong: the touch
  // runs loose instead of past the man.
  e_PlayerCommandModifier_Fumble = 2
};

class IController;

struct TouchInfo {
  TouchInfo() {
    inputPower = 0;
    autoDirectionBias = 0;
    autoPowerBias = 0;
    targetPlayer = 0;
    forcedTargetPlayer = 0;
    desiredPower = 0;
    isClearance = false;
  }

  Vector3 inputDirection;
  float inputPower;

  float autoDirectionBias;
  float autoPowerBias;

  Vector3 desiredDirection;  // inputdirection after pass function
  float desiredPower;
  Player* targetPlayer;        // null == do not use
  Player* forcedTargetPlayer;  // null == do not use
  // A panic clearance: hoofed into a lane to get rid of it, with no intended
  // recipient. Football statistics count a clearance separately from a pass,
  // and so does the engine - see MatchData::AddClearance. Without this, every
  // desperate hoof landed in the pass column as an attempt that could never be
  // completed, which held measured passing accuracy around 50%.
  bool isClearance;
};

enum e_StrictMovement { e_StrictMovement_False, e_StrictMovement_True, e_StrictMovement_Dynamic };

struct PlayerCommand {
  /* specialVar1:

    1: happy celebration
    2: inverse celebration (feeling bad)
    3: referee showing card
  */

  PlayerCommand() {
    desiredFunctionType = e_FunctionType_Movement;
    useDesiredMovement = false;
    desiredVelocityFloat = idleVelocity;
    strictMovement = e_StrictMovement_Dynamic;
    useDesiredLookAt = false;
    useTripType = false;
    useDesiredTripDirection = false;
    onlyDeflectAnimsThatPickupBall = false;
    tripType = 1;
    useSpecialVar1 = false;
    specialVar1 = 0;
    useSpecialVar2 = false;
    specialVar2 = 0;
    modifier = 0;
  }

  e_FunctionType desiredFunctionType;

  bool useDesiredMovement;
  Vector3 desiredDirection;
  e_StrictMovement strictMovement;

  float desiredVelocityFloat;

  bool useDesiredLookAt;
  Vector3 desiredLookAt;  // absolute 'look at' position on pitch

  bool useTouchInfo;
  TouchInfo touchInfo;

  bool onlyDeflectAnimsThatPickupBall;

  bool useTripType;
  int tripType;  // only applicable for trip anims

  bool useDesiredTripDirection;
  Vector3 desiredTripDirection;

  bool useSpecialVar1;
  int specialVar1;
  bool useSpecialVar2;
  int specialVar2;

  int modifier;
};

using PlayerCommandQueue = std::vector<PlayerCommand>;

std::string GetRoleName(e_PlayerRole playerRole);
e_PlayerRole GetRoleFromString(const std::string& roleString);

struct FormationEntry {
  e_PlayerRole role;
  Vector3 databasePosition;
  Vector3 position;  // adapted to player role (combination of databasePosition and hardcoded role
                     // position)
};

struct PlayerImage {
  int teamID;
  signed int side;
  int playerID;
  Player* player;
  Vector3 position;
  Vector3 directionVec;
  Vector3 bodyDirectionVec;
  float velocity;
  Vector3 movement;
  FormationEntry formationEntry;
  FormationEntry dynamicFormationEntry;
};

bool PlayerImageDepthSortFunc(const PlayerImage& a, const PlayerImage& b);

enum e_DecayType { e_DecayType_Constant, e_DecayType_Variable };

enum e_MagnetType { e_MagnetType_Attract, e_MagnetType_Repel };

// forcefields consist of forcespots, representing a repelling or attracting force from a position,
// including linearity/etc parameters
struct ForceSpot {
  ForceSpot() { exp = 1.0f; }
  Vector3 origin;
  e_MagnetType magnetType;
  e_DecayType decayType;
  float exp;
  float power;
  float scale;  // scaled #meters until effect is almost decimated
};

class PassRating {
public:
  PassRating(int playerID, float odds, float pos, float sit)
      : playerID(playerID), odds(odds), pos(pos), sit(sit), rating(0) {}
  virtual ~PassRating() {}

  void CalculateRating(float opportunism) {
    rating = (sit * 1.0f + odds * 1.0f) * 0.5f * (1 - opportunism) + pos * opportunism;
  }

  bool operator<(const PassRating& otherPassRating) const {
    return rating < otherPassRating.rating;
  }

  int playerID;

  // 0 .. 1 == worst .. best
  float odds;    // what are the odds a pass to this player will complete?
  float pos;     // is this player in a good position?
  float sit;     // target's situational rating
  float rating;  // resulting rating
};

using PassRatings = std::vector<PassRating>;

// Every skin weight a model carries: the ASE's vertex colours, and the sidecar
// weight file beside it when there is one (skinweights.hpp).
void LoadSkinWeights(blunted::SkinWeights& weights,
                     const std::string& aseFilename =
                         "media/objects/players/models/fullbody.ase");

e_FunctionType StringToFunctionType(const std::string& fun);
std::string FunctionTypeToString(e_FunctionType functionType);

float GetGlobalVelocityMultiplier();

#endif

// SDL-free game types and pitch geometry.
//
// gamedefines.hpp pulls in SDL for key ids, which makes it unusable from the
// pure-logic simulation modules that the headless test job builds without SDL
// installed. Everything here is plain data: the roles, the match phases and the
// dimensions of the pitch. gamedefines.hpp includes this header, so game code
// sees the same definitions it always did.

#ifndef _HPP_GAMETYPES
#define _HPP_GAMETYPES

enum e_PlayerRole {
  e_PlayerRole_GK,
  e_PlayerRole_CB,
  e_PlayerRole_LB,
  e_PlayerRole_RB,
  e_PlayerRole_DM,
  e_PlayerRole_CM,
  e_PlayerRole_LM,
  e_PlayerRole_RM,
  e_PlayerRole_AM,
  e_PlayerRole_CF,
};

enum e_SetPiece {
  e_SetPiece_None,
  e_SetPiece_KickOff,
  e_SetPiece_GoalKick,
  e_SetPiece_FreeKick,
  e_SetPiece_Corner,
  e_SetPiece_ThrowIn,
  e_SetPiece_Penalty,
};

enum e_TouchType {
  e_TouchType_Intentional_Kicked,     // goalies can't touch this
  e_TouchType_Intentional_Nonkicked,  // headers and such
  e_TouchType_Accidental,             // collisions
  e_TouchType_None,
  e_TouchType_SIZE
};

enum e_MatchPhase {
  e_MatchPhase_PreMatch,
  e_MatchPhase_1stHalf,
  e_MatchPhase_2ndHalf,
  e_MatchPhase_1stExtraTime,
  e_MatchPhase_2ndExtraTime,
  e_MatchPhase_Penalties,
};

const float pitchHalfW = 55;  // only inside side- and backlines
const float pitchHalfH = 36;
const float pitchFullHalfW = 60;  // including 'rim'
const float pitchFullHalfH = 40;
const float lineHalfW = 0.06f;

const float goalDepth = 2.55f;
const float goalHeight = 2.5f;
const float goalHalfWidth = 3.7f;

#endif

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "gamedefines.hpp"

#include "base/log.hpp"
#include "base/utils.hpp"
#include "simcontext.hpp"

e_FunctionType StringToFunctionType(const std::string& fun) {
  if (fun == "movement")
    return e_FunctionType_Movement;
  if (fun == "ballcontrol")
    return e_FunctionType_BallControl;
  if (fun == "trap")
    return e_FunctionType_Trap;
  if (fun == "shortpass")
    return e_FunctionType_ShortPass;
  if (fun == "longpass")
    return e_FunctionType_LongPass;
  if (fun == "highpass")
    return e_FunctionType_HighPass;
  if (fun == "shot")
    return e_FunctionType_Shot;
  if (fun == "deflect")
    return e_FunctionType_Deflect;
  if (fun == "catch")
    return e_FunctionType_Catch;
  if (fun == "interfere")
    return e_FunctionType_Interfere;
  if (fun == "trip")
    return e_FunctionType_Trip;
  if (fun == "sliding")
    return e_FunctionType_Sliding;
  if (fun == "special")
    return e_FunctionType_Special;
  return e_FunctionType_None;
}

std::string FunctionTypeToString(e_FunctionType functionType) {
  // the inverse of StringToFunctionType, so config can name a family the same
  // way an .anim's <type> does
  switch (functionType) {
    case e_FunctionType_Movement:
      return "movement";
    case e_FunctionType_BallControl:
      return "ballcontrol";
    case e_FunctionType_Trap:
      return "trap";
    case e_FunctionType_ShortPass:
      return "shortpass";
    case e_FunctionType_LongPass:
      return "longpass";
    case e_FunctionType_HighPass:
      return "highpass";
    case e_FunctionType_Shot:
      return "shot";
    case e_FunctionType_Deflect:
      return "deflect";
    case e_FunctionType_Catch:
      return "catch";
    case e_FunctionType_Interfere:
      return "interfere";
    case e_FunctionType_Trip:
      return "trip";
    case e_FunctionType_Sliding:
      return "sliding";
    case e_FunctionType_Special:
      return "special";
    default:
      return "";
  }
}

std::string GetRoleName(e_PlayerRole playerRole) {
  switch (playerRole) {
    case e_PlayerRole_GK:
      return "GK";
      break;

    case e_PlayerRole_CB:
      return "CB";
      break;
    case e_PlayerRole_LB:
      return "LB";
      break;
    case e_PlayerRole_RB:
      return "RB";
      break;

    case e_PlayerRole_DM:
      return "DM";
      break;
    case e_PlayerRole_CM:
      return "CM";
      break;
    case e_PlayerRole_LM:
      return "LM";
      break;
    case e_PlayerRole_RM:
      return "RM";
      break;
    case e_PlayerRole_AM:
      return "AM";
      break;

    case e_PlayerRole_CF:
      return "CF";
      break;
    case e_PlayerRole_LW:
      return "LW";
      break;
    case e_PlayerRole_RW:
      return "RW";
      break;
    case e_PlayerRole_SS:
      return "SS";
      break;

    default:
      return "undefined";
      break;
  }
}

e_PlayerRole GetRoleFromString(const std::string& roleString) {
  if (roleString == "GK")
    return e_PlayerRole_GK;
  if (roleString == "CB")
    return e_PlayerRole_CB;
  if (roleString == "LB")
    return e_PlayerRole_LB;
  if (roleString == "RB")
    return e_PlayerRole_RB;
  if (roleString == "DM")
    return e_PlayerRole_DM;
  if (roleString == "CM")
    return e_PlayerRole_CM;
  if (roleString == "LM")
    return e_PlayerRole_LM;
  if (roleString == "RM")
    return e_PlayerRole_RM;
  if (roleString == "AM")
    return e_PlayerRole_AM;
  if (roleString == "CF")
    return e_PlayerRole_CF;
  if (roleString == "LW")
    return e_PlayerRole_LW;
  if (roleString == "RW")
    return e_PlayerRole_RW;
  if (roleString == "SS")
    return e_PlayerRole_SS;
  return e_PlayerRole_CM;  // default
}

bool PlayerImageDepthSortFunc(const PlayerImage& a, const PlayerImage& b) {
  return a.position.coords[0] * a.side < b.position.coords[0] * b.side;
}

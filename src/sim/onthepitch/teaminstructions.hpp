// The team's mentality: PES's attack level T+0xb8a4 (docs/re/pes-re-tactics.md
// 3), five rungs. The team sheet's toggles and sliders are pes::TeamSheet
// (pesteamconcepts.hpp); PES's advanced instructions are switched on in match
// by a writer the reports have not found, so GF carries none.

#ifndef _HPP_TEAM_INSTRUCTIONS
#define _HPP_TEAM_INSTRUCTIONS

#include "base/properties.hpp"

namespace TeamInstructions {

// Five rungs, from a team camped on its own box to one throwing everyone up.
enum e_Mentality {
  e_Mentality_AllOutDefence = 0,
  e_Mentality_Defensive,
  e_Mentality_Balanced,
  e_Mentality_Attacking,
  e_Mentality_AllOutAttack,
  e_Mentality_Count,
};

struct State {
  e_Mentality mentality = e_Mentality_Balanced;
};

// The mentality a team carries into a match, from its tactics ("mentality").
State Load(const blunted::Properties& properties);

}  // namespace TeamInstructions

#endif

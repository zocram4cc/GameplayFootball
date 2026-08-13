// Manager instructions: the mentality a coach sets from the touchline and the
// advanced instructions he toggles, in the style of PES team tactics. They are
// applied as offsets on top of the team's philosophy and its tactic sliders, so
// a manager's own settings still show through.

#ifndef _HPP_TEAM_INSTRUCTIONS
#define _HPP_TEAM_INSTRUCTIONS

#include <string>

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

enum e_Instruction {
  e_Instruction_FrontlinePressure = 1 << 0,
  e_Instruction_DeepDefensiveLine = 1 << 1,
  e_Instruction_AggressiveDefence = 1 << 2,
  e_Instruction_HugTheTouchline = 1 << 3,
  e_Instruction_CentreShading = 1 << 4,
  e_Instruction_TikiTaka = 1 << 5,
  e_Instruction_LongBallCounter = 1 << 6,
};

using InstructionMask = unsigned int;

const InstructionMask instructionsNone = 0;
const int instructionCount = 7;

struct State {
  e_Mentality mentality = e_Mentality_Balanced;
  InstructionMask instructions = instructionsNone;
};

e_Instruction GetInstructionAt(int index);
std::string GetMentalityName(e_Mentality mentality);
std::string GetInstructionName(e_Instruction instruction);

// One rung up or down the mentality ladder; both clamp at the ends.
void PushUp(State& state);
void DropBack(State& state);

bool Has(const State& state, e_Instruction instruction);
void Toggle(State& state, e_Instruction instruction);

// Folds the instructions into a set of live tactics, keeping every value inside
// the [0, 1] slider range.
void Apply(const State& state, blunted::Properties& tactics);

// One line for the on-screen feedback when a hotkey is pressed.
std::string Describe(const State& state);

}  // namespace TeamInstructions

#endif

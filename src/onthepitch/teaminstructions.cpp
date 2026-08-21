#include "teaminstructions.hpp"

#include <algorithm>

namespace TeamInstructions {

namespace {

const e_Instruction allInstructions[instructionCount] = {
    e_Instruction_FrontlinePressure, e_Instruction_DeepDefensiveLine,
    e_Instruction_AggressiveDefence, e_Instruction_HugTheTouchline,
    e_Instruction_CentreShading,     e_Instruction_TikiTaka,
    e_Instruction_LongBallCounter,
};

// How far each rung of the ladder shifts the team up or down the pitch.
const float mentalityDepthStep = 0.18f;
// The rungs also change how hard the team works to win the ball back.
const float mentalityPressureStep = 0.1f;

float Clamp01(float value) {
  return std::max(0.0f, std::min(value, 1.0f));
}

// Nudges a tactic, keeping it inside the slider range.
void Nudge(blunted::Properties& tactics, const char* name, float offset) {
  tactics.Set(name, Clamp01(tactics.GetReal(name, 0.5f) + offset));
}

// -2 for all-out defence, +2 for all-out attack.
int GetMentalityStep(e_Mentality mentality) {
  return static_cast<int>(mentality) - static_cast<int>(e_Mentality_Balanced);
}

}  // namespace

e_Mentality GetPresetForDirection(e_PresetDirection direction) {
  switch (direction) {
    case e_PresetDirection_Up:
      return e_Mentality_AllOutAttack;
    case e_PresetDirection_Right:
      return e_Mentality_Attacking;
    case e_PresetDirection_Down:
      return e_Mentality_AllOutDefence;
    default:
      return e_Mentality_Defensive;
  }
}

e_Instruction GetQuickInstructionAt(int index) {
  // The four a manager reaches for most often, on the pad's face buttons.
  const e_Instruction quick[quickInstructionCount] = {
      e_Instruction_FrontlinePressure, e_Instruction_DeepDefensiveLine,
      e_Instruction_HugTheTouchline, e_Instruction_TikiTaka};
  return quick[std::max(0, std::min(index, quickInstructionCount - 1))];
}

void SelectMentality(State& state, e_Mentality mentality) {
  // Pressing the preset that is already set steps back to Balanced.
  state.mentality = state.mentality == mentality ? e_Mentality_Balanced : mentality;
}

e_Instruction GetInstructionAt(int index) {
  return allInstructions[std::max(0, std::min(index, instructionCount - 1))];
}

std::string GetMentalityName(e_Mentality mentality) {
  switch (mentality) {
    case e_Mentality_AllOutDefence:
      return "All-Out Defence";
    case e_Mentality_Defensive:
      return "Defensive";
    case e_Mentality_Attacking:
      return "Attacking";
    case e_Mentality_AllOutAttack:
      return "All-Out Attack";
    default:
      return "Balanced";
  }
}

std::string GetInstructionName(e_Instruction instruction) {
  switch (instruction) {
    case e_Instruction_FrontlinePressure:
      return "Frontline Pressure";
    case e_Instruction_DeepDefensiveLine:
      return "Deep Defensive Line";
    case e_Instruction_AggressiveDefence:
      return "Aggressive Defence";
    case e_Instruction_HugTheTouchline:
      return "Hug The Touchline";
    case e_Instruction_CentreShading:
      return "Centre Shading";
    case e_Instruction_TikiTaka:
      return "Tiki-Taka";
    case e_Instruction_LongBallCounter:
      return "Long Ball Counter";
    default:
      return "";
  }
}

void PushUp(State& state) {
  if (state.mentality < e_Mentality_AllOutAttack)
    state.mentality = static_cast<e_Mentality>(static_cast<int>(state.mentality) + 1);
}

void DropBack(State& state) {
  if (state.mentality > e_Mentality_AllOutDefence)
    state.mentality = static_cast<e_Mentality>(static_cast<int>(state.mentality) - 1);
}

bool Has(const State& state, e_Instruction instruction) {
  return (state.instructions & static_cast<InstructionMask>(instruction)) != 0;
}

void Toggle(State& state, e_Instruction instruction) {
  state.instructions ^= static_cast<InstructionMask>(instruction);
}

void Apply(const State& state, blunted::Properties& tactics) {
  // Mentality: the whole team moves up or down the pitch, and works harder or
  // less hard off the ball.
  const float step = static_cast<float>(GetMentalityStep(state.mentality));
  if (step != 0.0f) {
    Nudge(tactics, "position_offense_depth_factor", step * mentalityDepthStep);
    Nudge(tactics, "position_defense_depth_factor", step * mentalityDepthStep);
    Nudge(tactics, "team_pressure", step * mentalityPressureStep);
    Nudge(tactics, "dribble_offensiveness", step * mentalityPressureStep);
  }

  // Advanced instructions, each a small deliberate shift.
  if (Has(state, e_Instruction_FrontlinePressure)) {
    Nudge(tactics, "team_pressure", 0.3f);
    Nudge(tactics, "position_defense_depth_factor", 0.2f);
  }
  if (Has(state, e_Instruction_DeepDefensiveLine)) {
    Nudge(tactics, "position_defense_depth_factor", -0.3f);
    Nudge(tactics, "position_defense_midfieldfocus", -0.1f);
  }
  if (Has(state, e_Instruction_AggressiveDefence)) {
    Nudge(tactics, "team_pressure", 0.2f);
    Nudge(tactics, "position_defense_microfocus_strength", 0.15f);
  }
  if (Has(state, e_Instruction_HugTheTouchline)) {
    Nudge(tactics, "position_offense_width_factor", 0.25f);
    Nudge(tactics, "dribble_centermagnet", -0.2f);
  }
  if (Has(state, e_Instruction_CentreShading)) {
    Nudge(tactics, "position_offense_width_factor", -0.25f);
    Nudge(tactics, "dribble_centermagnet", 0.2f);
  }
  if (Has(state, e_Instruction_TikiTaka)) {
    Nudge(tactics, "support_distance", -0.3f);
    Nudge(tactics, "counter_attack", -0.2f);
    Nudge(tactics, "position_offense_midfieldfocus", 0.2f);
  }
  if (Has(state, e_Instruction_LongBallCounter)) {
    Nudge(tactics, "support_distance", 0.3f);
    Nudge(tactics, "counter_attack", 0.3f);
  }
}

std::string Describe(const State& state) {
  std::string description = GetMentalityName(state.mentality);

  for (int i = 0; i < instructionCount; i++) {
    const e_Instruction instruction = GetInstructionAt(i);
    if (Has(state, instruction))
      description += " | " + GetInstructionName(instruction);
  }
  return description;
}

// The tactics keys. Named for what they are, and refused by
// TeamPhilosophy::IsSliderTactic so they do not join the numeric sliders.
static const char* kMentalityKey = "mentality";
static const char* kInstructionsKey = "instructions";

void Save(const State& state, blunted::Properties& properties) {
  properties.Set(kMentalityKey, static_cast<float>(state.mentality));
  properties.Set(kInstructionsKey, static_cast<float>(state.instructions));
}

State Load(const blunted::Properties& properties) {
  State state;
  // -1 for absent, because 0 is a mentality (balanced is 2) and reading a missing
  // key as 0 would silently set one.
  const int mentality = static_cast<int>(properties.GetReal(kMentalityKey, -1.0f));
  if (mentality >= 0 && mentality < e_Mentality_Count)
    state.mentality = static_cast<e_Mentality>(mentality);
  const float saved = properties.GetReal(kInstructionsKey, 0.0f);
  if (saved > 0.0f) {
    // Only the bits that name an instruction; anything else in there is not ours.
    InstructionMask mask = static_cast<InstructionMask>(saved);
    InstructionMask known = instructionsNone;
    for (int i = 0; i < instructionCount; i++) known |= GetInstructionAt(i);
    state.instructions = mask & known;
  }
  return state;
}

}  // namespace TeamInstructions

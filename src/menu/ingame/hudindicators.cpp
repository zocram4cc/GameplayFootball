#include "menu/ingame/hudindicators.hpp"

#include <algorithm>
#include <cmath>

namespace HudIndicators {

namespace {

float Clamp01(float value) { return std::max(0.0f, std::min(value, 1.0f)); }

}  // namespace

float LevelBandPosition(int mentality, int mentalityCount) {
  // A ladder needs at least two rungs to have a top and a bottom; anything less
  // is drawn centred rather than dividing by zero.
  if (mentalityCount < 2) return 0.5f;
  const int clamped = std::max(0, std::min(mentality, mentalityCount - 1));
  return static_cast<float>(clamped) / static_cast<float>(mentalityCount - 1);
}

std::string PlateText(int shirtNumber, const std::string& name, bool numberFirst) {
  const std::string number = shirtNumber > 0 ? std::to_string(shirtNumber) : "";
  if (number.empty()) return name;
  if (name.empty()) return number;
  return numberFirst ? number + " " + name : name + " " + number;
}

float StaminaFraction(float condition) {
  // Quantised to hundredths. Resizing a Gui2Image rescales its surface, and
  // fatigue drifts continuously, so an exact fraction would have the bar
  // rescaling on almost every frame for a change of well under a pixel.
  return std::round(Clamp01(condition) * 100.0f) / 100.0f;
}

float PhilosophyDialSplit(int philosophy) {
  // The dial's two tones are the attacking and the defending halves of a style.
  // Order matches TeamPhilosophy::e_Philosophy.
  switch (philosophy) {
    case 1:  // Gegenpressing: wins the ball high and goes again
      return 0.75f;
    case 2:  // TikiTaka: keeps the ball, tilted attacking but patient
      return 0.6f;
    case 3:  // ParkTheBus
      return 0.3f;
    case 0:  // Balanced
    default:
      return 0.5f;
  }
}

void FitKeepingAspect(float boxWidth, float boxHeight, float aspect, float* width,
                      float* height) {
  if (!width || !height) return;
  if (aspect <= 0.0f || boxWidth <= 0.0f || boxHeight <= 0.0f) {
    *width = boxWidth;
    *height = boxHeight;
    return;
  }
  const float widthFromHeight = boxHeight * aspect;
  if (widthFromHeight <= boxWidth) {
    *width = widthFromHeight;
    *height = boxHeight;
  } else {
    *width = boxWidth;
    *height = boxWidth / aspect;
  }
}

// Short tags rather than names: the strip is one line under the plate, and seven of
// them have to fit. Order follows TeamInstructions::GetInstructionAt so the readout
// is stable whatever order they were switched on in.
static const char* kInstructionTags[TeamInstructions::instructionCount] = {
    "PRESS", "DEEP", "AGGR", "WIDE", "NARROW", "TIKI", "LONG",
};

// The instruction each tag stands for, in the same order, so the readout is stable
// whatever order they were switched on in.
static const TeamInstructions::e_Instruction kTagOrder[TeamInstructions::instructionCount] = {
    TeamInstructions::e_Instruction_FrontlinePressure,
    TeamInstructions::e_Instruction_DeepDefensiveLine,
    TeamInstructions::e_Instruction_AggressiveDefence,
    TeamInstructions::e_Instruction_HugTheTouchline,
    TeamInstructions::e_Instruction_CentreShading,
    TeamInstructions::e_Instruction_TikiTaka,
    TeamInstructions::e_Instruction_LongBallCounter,
};

std::string InstructionsText(TeamInstructions::InstructionMask instructions) {
  std::string out;
  for (int i = 0; i < TeamInstructions::instructionCount; i++) {
    if ((instructions & kTagOrder[i]) == 0)
      continue;
    if (!out.empty())
      out += " ";
    out += kInstructionTags[i];
  }
  return out;
}

}  // namespace HudIndicators

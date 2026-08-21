// What the in-play player indicator says, separately from how it is drawn.
//
// PES's indicator, measured off the VGL26 broadcast (docs/VGL26_REFERENCE.md):
// the team badge at the outer edge, a dark rounded plate with the shirt number
// and the name, a thin green stamina bar along the plate's top edge, a small
// vertical box whose white band gives the attack/defence level, and a two-tone
// circular dial for the tactical style. The number leads the name on the user's
// side and follows it on the other.
//
// Everything here reads state the match already has: the philosophy the manager
// chose (TeamPhilosophy) and the mentality he set from the touchline
// (TeamInstructions - five rungs from AllOutDefence to AllOutAttack, the same
// ladder PES's box shows). Nothing here derives a level of its own; an indicator
// showing a number nobody set would be worse than no indicator.

#ifndef _HPP_MENU_INGAME_HUDINDICATORS
#define _HPP_MENU_INGAME_HUDINDICATORS

#include <string>

#include "onthepitch/teaminstructions.hpp"

namespace HudIndicators {

// Where the white band sits in its box, 0 at the bottom. `mentality` is a
// TeamInstructions::e_Mentality and `mentalityCount` its e_Mentality_Count, so the
// box follows the ladder rather than a copy of it that could drift out of step.
float LevelBandPosition(int mentality, int mentalityCount);

// The plate's text. numberFirst is true for the side the user controls.
std::string PlateText(int shirtNumber, const std::string& name, bool numberFirst);

// The filled part of the stamina bar, quantised so the bar is not rescaled for
// a change nobody can see.
float StaminaFraction(float condition);

// The advanced instructions in force, as a short line for the HUD strip, or empty
// when none are.
//
// The indicators were wired to the mentality band and the philosophy dial and to
// nothing else, so toggling one of the seven advanced instructions moved the
// transient banner and left the HUD unchanged - they never appeared to do anything.
std::string InstructionsText(TeamInstructions::InstructionMask instructions);

// The attacking tone's share of the philosophy dial. `philosophy` is a
// TeamPhilosophy::e_Philosophy; anything unrecognised splits the dial evenly.
float PhilosophyDialSplit(int philosophy);

// Fits content of a given aspect (width / height, in the same units as the box)
// inside the box, without cropping and without stretching. An aspect of zero -
// nothing known about the content - fills the box, which is what the badge used
// to do to a square crest.
void FitKeepingAspect(float boxWidth, float boxHeight, float aspect, float* width,
                      float* height);

}  // namespace HudIndicators

#endif

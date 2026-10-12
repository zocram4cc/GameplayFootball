// Tactical archetypes ("team philosophies").
//
// A philosophy bundles the individual tactic sliders into a recognisable style
// of play and adds a few behavioural switches that plain slider values cannot
// express (counter-pressing on possession loss, stamina cost, trap depth).
// See SIMULATION_IMPROVEMENT_PROPOSAL.md section 2A.

#ifndef _HPP_TEAM_PHILOSOPHY
#define _HPP_TEAM_PHILOSOPHY

#include <string>

#include "base/properties.hpp"

namespace TeamPhilosophy {

enum e_Philosophy {
  e_Philosophy_Balanced = 0,
  e_Philosophy_Gegenpressing,
  e_Philosophy_TikiTaka,
  e_Philosophy_ParkTheBus,
  e_Philosophy_Count,
};

// Distance from the goal line to the edge of the penalty box, in metres.
const float penaltyBoxDepth = 16.5f;

// Case- and separator-insensitive lookup; unknown names yield Balanced.
e_Philosophy Parse(const std::string& name);

// Canonical identifier, suitable for storing in team data and for Parse().
std::string GetName(e_Philosophy philosophy);

// Overwrites the tactic sliders that define the archetype, leaving the rest of
// the property set alone. Balanced is a no-op.
void ApplyPreset(e_Philosophy philosophy, blunted::Properties& tactics);

// Extra time the team keeps hunting the ball after losing it.
unsigned long GetTeamPressureDurationBonus_ms(e_Philosophy philosophy);

// Whether losing possession alone should trigger team pressure.
bool PressesOnPossessionLoss(e_Philosophy philosophy);

// Multiplier applied to fatigue accumulation.
float GetStaminaDrainMultiplier(e_Philosophy philosophy);

// Whether the passing game should favour short, safe links.
bool PrefersShortPassing(e_Philosophy philosophy);

// Scales the execution error of a pass. Short-link styles knock the ball around
// more precisely; a long-ball game accepts more risk per pass. 1 is neutral.
float GetPassErrorMultiplier(e_Philosophy philosophy, float supportDistance);

// Adapts a computed offside-trap X coordinate to the philosophy. `teamSide` is
// -1 or 1 and points from the pitch centre towards the team's own goal.
float AdaptOffsideTrapX(e_Philosophy philosophy, float trapX, int teamSide);

// Whether a tactics key belongs on a slider in the game plan.
//
// The slider list is built from every key in a team's tactics, which works only
// while they are all numbers. Philosophy is not: the philosophy menu writes the
// string "balanced" into the same map, so choosing one grew an extra slider with no
// readable name, sitting at zero because GetReal of "balanced" is zero, and dragging
// it wrote a number over the philosophy that menu had just set. Keys with an editor
// of their own are refused; anything else is a number and belongs on a slider,
// including tactics this build has never heard of.
bool IsSliderTactic(const std::string& tacticName);

}  // namespace TeamPhilosophy

#endif

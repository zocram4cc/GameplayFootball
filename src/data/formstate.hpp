// PES's per-match stat modifiers: the condition arrow and position familiarity.
//
// PES multiplies every live attribute by a product of per-array factors
// (docs/21Research.md, "StatModifierPipeline": Array 2 is the condition arrow
// drawn at kick-off, Array 3 the player's familiarity with the position he is
// DEPLOYED at - not where he happens to be standing). Both tables below are
// the engine's own, read off live breakpoints and the binary lookup table, and
// both put the attributes into tiers so a poor arrow costs awareness more than
// physique and playing out of position costs technique most of all.
//
// GF's live pipeline was one blanket multiplier (Player::GetStat). This is the
// layered product it becomes; it is pure so the tables can be tested without a
// match.

#ifndef _HPP_ONTHEPITCH_FORMSTATE
#define _HPP_ONTHEPITCH_FORMSTATE

#include <string>
#include <vector>

#include "../gametypes.hpp"

namespace FormState {

// The five arrows, worst to best, in PES's colours: purple, blue, green,
// orange, red. Green is the neutral state - no writes at all in PES.
enum class Arrow { Terrible = 0, Poor, Normal, Great, Top, Count };

const char* ArrowName(Arrow arrow);
const char* ArrowColour(Arrow arrow);  // "purple" .. "red", for the plan card

// How a side's arrows are decided for the coming match - the pre-match
// "Condition: Home / Away" row. Random draws one per player from his own
// form; a fixed choice gives every player on that side the same arrow.
enum class Policy { RandomPerPlayer = 0, AllTerrible, AllPoor, AllNormal, AllGreat, AllTop, Count };

const char* PolicyName(Policy policy);
// Parse the configured value ("random", "red", "orange", "green", "blue",
// "purple"); anything else is random, which is PES's default.
Policy ParsePolicy(const std::string& text);
std::string SerializePolicy(Policy policy);

// The arrow a policy hands a player. `form01` is the player's Form attribute
// on 0..1 (PES's 1-8 scale normalised, physical_form in the profile) and
// `roll` a uniform in [0, 1): a steady player sits near Normal, a volatile
// one swings to the ends more often. PES's exact draw is unresolved in the
// research (its open-questions table names it), so this is the obvious model
// of what Form is documented to do; the multipliers below are PES's own.
Arrow Draw(Policy policy, float form01, float roll);

// The arrow a given player is dealt for a given match, reproducibly: the same
// (seed, team, player) always yields the same arrow, so the pre-match Game
// Plan card and the Team created at kick-off show one and the same colour.
// `seed` is match_condition_seed, written by the pre-match sheet.
Arrow ArrowForPlayer(Policy policy, float form01, unsigned int seed, int teamID, int playerIndex);

// Which of PES's three arrow tiers an attribute key belongs to. Keys are the
// profile names (physical_velocity ...); unknown keys land in the middle tier.
enum class ArrowTier { Awareness = 0, Technical, Physique };
ArrowTier ArrowTierOf(const char* stat);
float ArrowMultiplier(Arrow arrow, ArrowTier tier);
float ArrowMultiplierFor(Arrow arrow, const char* stat);

// Position familiarity: the rating at the position the player is deployed at.
enum class Familiarity { Unfamiliar = 0, Partial, Natural };

// PES's own tiering for THIS array - speed and jump are technical here and
// athletic for the arrows; the two tables do not share a tier assignment.
enum class FamiliarityTier { Technical = 0, Awareness, Physique };
FamiliarityTier FamiliarityTierOf(const char* stat);
float FamiliarityMultiplier(Familiarity level, FamiliarityTier tier);
float FamiliarityMultiplierFor(Familiarity level, const char* stat);

// The whole per-match product for one attribute, applied to a 0..1 stat and
// CLAMPED to 1.0: a player already at the ceiling on a red arrow gains nothing
// (PES stores ratings 40-99 and the same holds there), and nothing downstream
// - anim selection, velocity tables, the plan card's percentages - ever sees a
// value above the range it was written for. Below zero is impossible (every
// factor is positive) but clamped anyway.
float Apply(float stat01, Arrow arrow, Familiarity level, const char* stat);

// PES's thirteen formation positions, left and right kept apart, in the order
// of its familiarity bytes (docs/21Research.md, Position Index Mapping).
enum class Slot { GK = 0, CB, LB, RB, DMF, CMF, LMF, RMF, AMF, LWF, RWF, SS, CF, Count };
// The slot a GF role occupies; side (-1 left, +1 right, 0 centre) splits the
// paired roles the same way PES does.
Slot SlotFor(e_PlayerRole role, int side);
const char* SlotName(Slot slot);

// A player's thirteen ratings as one string, PES's byte order, one letter
// each: "A" natural, "B" partial, "C" unfamiliar - what the profile_xml
// carries under <position_familiarity>. Missing or short strings read as C
// everywhere except the letters given.
Familiarity Rating(const std::string& ratings, Slot slot);
// The ratings a player gets when the database names none: A at every slot his
// registered roles cover (and the mirror of a flank role at B), C elsewhere.
std::string InferRatings(const std::vector<e_PlayerRole>& roles);

}  // namespace FormState

#endif

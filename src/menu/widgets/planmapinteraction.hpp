// Pure-logic helpers for the game plan pitch (docs/references/pes21_game_plan_prefab.json,
// PES21_VGL26_Day3_Enrichment_Addendum.md "Game Plan layout"). Kept as free
// functions over plain data - no Gui2/TeamData dependency - so the mapping
// and drag math can be unit-tested headlessly; Gui2PlanMap (planmap.*) pulls
// the real squad out of TeamData and calls into this.
//
// gui2 has no pointer device (src/utils/gui2/events.hpp defines no mouse
// event), so "drag" is grab/move/drop over the same directional-input plus
// activate-button primitives every other Gui2 widget already uses: select a
// card, press activate to pick it up, move it with directional input, press
// activate again to drop it. This module is that state machine's math.

#ifndef _HPP_MENU_WIDGETS_PLANMAPINTERACTION
#define _HPP_MENU_WIDGETS_PLANMAPINTERACTION

#include <vector>

#include "../../base/math/vector3.hpp"
#include "../../gametypes.hpp"

namespace PlanMapInteraction {

// A point on the portrait pitch schematic, both axes 0..100 percent, y=0 at
// the attacking (top) end - matching FormationGraphicLayout's panel-point
// convention so the two widgets read the same way.
struct PitchPoint {
  float xPercent = 0.0f;
  float yPercent = 0.0f;
};

// Maps a formation slot's database-space position (x: -1 own goal .. +1
// opposing goal, y: -1 right back's flank .. +1 left back's flank) onto the
// pitch schematic. Outfield players have their depth compressed toward
// mid-pitch (the keeper alone reaches the very back), matching the spacing
// Gui2PlanMap drew before this module existed - kept so existing squads do
// not jump on screen.
PitchPoint DatabaseToPitch(const blunted::Vector3& databasePosition, e_PlayerRole role);

// The exact inverse: what a card dropped at `point` should be written back
// to TeamData::SetFormationEntry as. Round-trips DatabaseToPitch for every
// role, including the keeper's uncompressed depth.
blunted::Vector3 PitchToDatabase(const PitchPoint& point, e_PlayerRole role);

// Keeps a dragged point at least `marginPercent` from every edge, so a card
// dropped off the pitch art still renders on it.
PitchPoint ClampToPitch(PitchPoint point, float marginPercent);

// The closest card to `at`, excluding `excludeIndex` (the one being
// dragged), within `radiusPercent`. -1 when nothing qualifies - a drop in
// open space moves the held card there instead of swapping.
int NearestCardWithinRadius(const PitchPoint& at, const std::vector<PitchPoint>& cards,
                            int excludeIndex, float radiusPercent);

// The card a directional press should move the selection to: the nearest
// neighbour whose offset from `cards[currentIndex]` has a positive
// projection onto `direction`, breaking ties by absolute distance. Returns
// `currentIndex` unchanged when no card lies that way.
int NextSelectionInDirection(const std::vector<PitchPoint>& cards, int currentIndex,
                             const blunted::Vector3& direction);

}  // namespace PlanMapInteraction

#endif

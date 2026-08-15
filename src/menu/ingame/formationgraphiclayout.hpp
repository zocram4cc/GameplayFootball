// Pure-logic helpers for the pre-match "TV-style" formation graphic
// (docs/PRESENTATION_SPEC.md section 1.1). Kept as free functions over plain
// data (no TeamData/Gui2 dependency) so the mapping and timing math can be
// unit-tested headlessly; the Gui2FormationGraphic widget (formationgraphic.*)
// pulls the real squad data out of TeamData/PlayerData and calls into this.
//
// Coordinate systems:
//  - "database position" is FormationEntry::databasePosition: x in
//    [-1 (own goal) .. +1 (opposing goal)], y in [-1 (right back's flank,
//    i.e. e_PlayerRole_RB's default) .. +1 (left back's flank)].
//  - "panel point" is a portrait pitch schematic, both axes 0..100 (percent,
//    matching the Gui2 convention), y=0 at the TOP of the body area and
//    y=100 at the BOTTOM: the goalkeeper (x=-1) sits near the bottom in a
//    goal box, the lone forward (x=+1) sits near the top in a forward arc,
//    exactly as the spec describes.

#ifndef _HPP_MENU_INGAME_FORMATIONGRAPHICLAYOUT
#define _HPP_MENU_INGAME_FORMATIONGRAPHICLAYOUT

#include <vector>

#include "../../base/math/vector3.hpp"
#include "../../gametypes.hpp"

namespace FormationGraphicLayout {

struct PanelPoint {
  float xPercent;
  float yPercent;
};

// Maps a formation slot's database-space position onto the portrait body
// area (0..100 both axes), clamped away from the panel edges.
PanelPoint MapPosition(const blunted::Vector3& databasePosition);

enum class RoleZone { Goalkeeper, Outfield, Forward };
// GK gets the goal-box treatment, CF the forward arc; everyone else is a
// plain jersey icon in the pitch schematic.
RoleZone ZoneForRole(e_PlayerRole role);

// The engine has no dedicated squad-number field (see docs/PES asset import
// notes): the starting XI is numbered 1-11 in formation-slot order and the
// bench continues from 12, matching the reference broadcast's numbering
// scheme ("Substitutes 12-23").
int SquadNumberForSlot(int formationSlotIndex);

struct Connection {
  int fromIndex;
  int toIndex;
};

// Faint tactical-shape lines (spec: "faint connecting lines suggesting the
// tactical shape"). Each player connects forward to the nearest teammate
// occupying a more advanced line (smaller yPercent / larger database x);
// the single most advanced player(s) have no outgoing connection. Ties on
// lateral (x) distance resolve to the lowest index for determinism.
std::vector<Connection> BuildConnections(const std::vector<blunted::Vector3>& databasePositions);

// Pre-match entrance schedule: which team's graphic (if any) should be on
// screen at a given point in the entrance, and its cross-fade alpha (0..1).
// Returns teamID -1 when nothing should be showing (including when the
// entrance is too short to fit both graphics comfortably).
struct DisplayState {
  int teamID = -1;
  float alpha = 0.0f;
};

DisplayState ComputeDisplayState(unsigned long elapsedSinceEntranceStart_ms,
                                 unsigned long entranceDuration_ms);

}  // namespace FormationGraphicLayout

#endif

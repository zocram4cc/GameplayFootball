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

#include <functional>
#include <string>
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

// --- Panel geometry ----------------------------------------------------
//
// Gui2 percentages are anisotropic: x is a fraction of the window's width,
// y a fraction of its height. Anything that has to keep a real-world shape
// (the panel card, the portrait pitch schematic) therefore has to be sized
// against the screen's aspect ratio rather than given a fixed percentage -
// which is what this computes, once, for the whole widget.
//
// Every field except panelX/panelY is relative to the panel's own top-left
// corner, which is what Gui2 child views want.
struct PanelGeometry {
  float panelX = 0.0f, panelY = 0.0f;
  float panelWidth = 0.0f, panelHeight = 0.0f;
  float headerHeight = 0.0f;
  float subsX = 0.0f, subsY = 0.0f, subsWidth = 0.0f, subsHeight = 0.0f;
  float pitchX = 0.0f, pitchY = 0.0f, pitchWidth = 0.0f, pitchHeight = 0.0f;
};

// The panel's own pixel proportions, and the pitch schematic's. Baked into
// the artwork by tools/pes21_import/export_formation_theme.py at exactly
// these ratios so scale-to-fit never distorts the rounded corners.
constexpr float kPanelPixelAspect = 1.16f;  // width / height
constexpr float kPitchPixelAspect = 0.66f;  // portrait, roughly a pitch's 68:105

PanelGeometry ComputePanelGeometry(float screenAspectRatio);

// --- Formation arrangement ---------------------------------------------
//
// MapPosition alone puts an icon wherever the tactics happen to place a
// player, which is how jerseys and nicknames ended up piled on top of each
// other. ArrangeFormation instead reads the XI as a set of lines - back
// line, midfield line(s), forward line - and lays those out as evenly
// spaced rows, which is both what the reference broadcast draws and what
// guarantees the icons cannot collide.
//
// Returns one point per input slot, in pitch-local percent (0..100, y=0 at
// the top / attacking end), in the caller's own slot order.

// The narrowest horizontal gap, in pitch-local percent, that two icons in
// the same row are ever placed at. The jersey icon is sized off this.
constexpr float kMinIconGapPercent = 19.0f;

std::vector<PanelPoint> ArrangeFormation(const std::vector<blunted::Vector3>& databasePositions,
                                         const std::vector<e_PlayerRole>& roles);

// The smallest horizontal distance between any two points sharing a row.
// Returns 100 when no two points share a row.
float MinHorizontalGap(const std::vector<PanelPoint>& points);

// The shape those rows spell out, back line first: "4-4-2", "4-2-3-1". The
// keeper is not counted, as is conventional. Empty for an empty lineup.
std::string FormationLabel(const std::vector<PanelPoint>& points,
                           const std::vector<e_PlayerRole>& roles);

// --- Substitutes column ------------------------------------------------

constexpr float kMinSubsRowHeightPercent = 1.7f;
constexpr float kMaxSubsRowHeightPercent = 2.6f;

struct SubsLayout {
  float headerHeight = 0.0f;  // the "Substitutes" label
  float firstRowY = 0.0f;     // top of the first name row, column-local
  float rowHeight = 0.0f;
  int rowCount = 0;  // how many names actually fit; the rest are dropped
};

// Fits `subCount` bench names into a column `columnHeight` percent tall.
// Rows are spaced out to use the column when the bench is short and packed
// down to kMinSubsRowHeightPercent when it is long; a bench too long even
// for that loses its tail rather than overrunning the panel.
SubsLayout ComputeSubsLayout(int subCount, float columnHeight);

// --- Fitting text into a box -------------------------------------------
//
// Gui2Caption renders at whatever width its text happens to need and simply
// resizes past its box (see caption.cpp), so overlong names have to be
// measured and dealt with by the caller.

// The caption height at which text `naturalWidthPercent` wide (measured at
// `naturalHeightPercent`) fits `maxWidthPercent`, never going below
// `minHeightPercent` and never growing past its natural height.
float FitTextHeight(float naturalWidthPercent, float naturalHeightPercent, float maxWidthPercent,
                    float minHeightPercent);

// Cuts `text` down until it fits `maxWidthPercent`, marking the cut with a
// trailing '.'. `widthOfFirst(n)` measures the first n characters - i.e.
// Gui2Caption::GetTextWidthPercent(n). Never returns an empty string for
// non-empty input, however narrow the box.
std::string TruncateToFit(const std::string& text, float maxWidthPercent,
                          const std::function<float(int)>& widthOfFirst);

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

// The same shape lines, drawn between points that have already been snapped
// into rows by ArrangeFormation - which is what the widget actually draws,
// so that a line always ends on the icon it points at.
std::vector<Connection> BuildConnections(const std::vector<PanelPoint>& points);

}  // namespace FormationGraphicLayout

#endif

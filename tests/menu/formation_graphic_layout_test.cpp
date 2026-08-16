// Formation graphic pitch-schematic mapping and entrance-timing math
// (docs/PRESENTATION_SPEC.md section 1.1). Pure logic, no TeamData/Gui2
// dependency - see src/menu/ingame/formationgraphiclayout.hpp.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "base/math/vector3.hpp"
#include "menu/ingame/formationgraphiclayout.hpp"

using blunted::Vector3;
using FormationGraphicLayout::ArrangeFormation;
using FormationGraphicLayout::BuildConnections;
using FormationGraphicLayout::ComputePanelGeometry;
using FormationGraphicLayout::ComputeSubsLayout;
using FormationGraphicLayout::FitTextHeight;
using FormationGraphicLayout::MapPosition;
using FormationGraphicLayout::MinHorizontalGap;
using FormationGraphicLayout::RoleZone;
using FormationGraphicLayout::SquadNumberForSlot;
using FormationGraphicLayout::TruncateToFit;
using FormationGraphicLayout::ZoneForRole;

namespace {

constexpr float kWide = 16.0f / 9.0f;

// A 4-4-2 in database space: x is depth (-1 own goal .. +1 opposing goal),
// y is the lateral axis (-1 right-back's flank .. +1 left-back's flank).
struct Slot {
  Vector3 position;
  e_PlayerRole role;
};

std::vector<Slot> FourFourTwo() {
  return {
      {Vector3(-1.00f, 0.00f, 0.0f), e_PlayerRole_GK},
      {Vector3(-0.70f, -0.75f, 0.0f), e_PlayerRole_RB},
      {Vector3(-0.72f, -0.25f, 0.0f), e_PlayerRole_CB},
      {Vector3(-0.72f, 0.25f, 0.0f), e_PlayerRole_CB},
      {Vector3(-0.70f, 0.75f, 0.0f), e_PlayerRole_LB},
      {Vector3(-0.05f, -0.75f, 0.0f), e_PlayerRole_RM},
      {Vector3(-0.10f, -0.25f, 0.0f), e_PlayerRole_CM},
      {Vector3(-0.10f, 0.25f, 0.0f), e_PlayerRole_CM},
      {Vector3(-0.05f, 0.75f, 0.0f), e_PlayerRole_LM},
      {Vector3(0.75f, -0.20f, 0.0f), e_PlayerRole_CF},
      {Vector3(0.75f, 0.20f, 0.0f), e_PlayerRole_CF},
  };
}

std::vector<Vector3> PositionsOf(const std::vector<Slot>& slots) {
  std::vector<Vector3> out;
  for (const Slot& s : slots) out.push_back(s.position);
  return out;
}

std::vector<e_PlayerRole> RolesOf(const std::vector<Slot>& slots) {
  std::vector<e_PlayerRole> out;
  for (const Slot& s : slots) out.push_back(s.role);
  return out;
}

// Stand-in for Gui2Caption::GetTextWidthPercent(n): a monospace font whose
// every glyph is one unit wide.
float MonospaceWidth(int characterCount) { return characterCount * 1.0f; }

}  // namespace

TEST(FormationGraphicLayoutTest, GoalkeeperSitsNearTheBottom) {
  const auto p = MapPosition(Vector3(-1.0f, 0.0f, 0.0f));
  EXPECT_GT(p.yPercent, 85.0f);
  EXPECT_NEAR(p.xPercent, 50.0f, 0.01f);
}

TEST(FormationGraphicLayoutTest, LoneForwardSitsNearTheTop) {
  const auto p = MapPosition(Vector3(1.0f, 0.0f, 0.0f));
  EXPECT_LT(p.yPercent, 15.0f);
}

TEST(FormationGraphicLayoutTest, DeeperPlayersAreLowerOnThePanel) {
  const auto back = MapPosition(Vector3(-0.8f, 0.0f, 0.0f));
  const auto mid = MapPosition(Vector3(0.0f, 0.0f, 0.0f));
  const auto fwd = MapPosition(Vector3(0.8f, 0.0f, 0.0f));
  EXPECT_GT(back.yPercent, mid.yPercent);
  EXPECT_GT(mid.yPercent, fwd.yPercent);
}

TEST(FormationGraphicLayoutTest, WidePlayersSpreadTowardsPanelEdgesButStayInBounds) {
  const auto left = MapPosition(Vector3(0.0f, 1.0f, 0.0f));
  const auto right = MapPosition(Vector3(0.0f, -1.0f, 0.0f));
  EXPECT_GT(left.xPercent, 50.0f);
  EXPECT_LT(right.xPercent, 50.0f);
  EXPECT_LE(left.xPercent, 92.0f);
  EXPECT_GE(right.xPercent, 8.0f);
}

TEST(FormationGraphicLayoutTest, OutOfRangeCoordinatesAreClamped) {
  const auto p = MapPosition(Vector3(5.0f, -5.0f, 0.0f));
  const auto edge = MapPosition(Vector3(1.0f, -1.0f, 0.0f));
  EXPECT_NEAR(p.xPercent, edge.xPercent, 0.01f);
  EXPECT_NEAR(p.yPercent, edge.yPercent, 0.01f);
}

TEST(FormationGraphicLayoutTest, RoleZonesPickOutGoalkeeperAndForward) {
  EXPECT_EQ(ZoneForRole(e_PlayerRole_GK), RoleZone::Goalkeeper);
  EXPECT_EQ(ZoneForRole(e_PlayerRole_CF), RoleZone::Forward);
  EXPECT_EQ(ZoneForRole(e_PlayerRole_CB), RoleZone::Outfield);
  EXPECT_EQ(ZoneForRole(e_PlayerRole_CM), RoleZone::Outfield);
}

TEST(FormationGraphicLayoutTest, SquadNumbersFollowFormationOrderStartersThenBench) {
  EXPECT_EQ(SquadNumberForSlot(0), 1);
  EXPECT_EQ(SquadNumberForSlot(10), 11);
  EXPECT_EQ(SquadNumberForSlot(11), 12);
  EXPECT_EQ(SquadNumberForSlot(22), 23);
}

TEST(FormationGraphicLayoutTest, ConnectionsLinkEachPlayerToTheNearestMoreAdvancedTeammate) {
  // GK, two CBs (deliberately un-symmetric so the nearest one is unambiguous
  // regardless of floating-point tie-breaking), one CF: 4-node "shape".
  std::vector<Vector3> positions = {
      Vector3(-1.0f, 0.0f, 0.0f),   // 0: GK (centred, x=50 on the panel)
      Vector3(-0.6f, -0.1f, 0.0f),  // 1: CB, close to centre - nearest to the GK
      Vector3(-0.6f, 0.8f, 0.0f),   // 2: CB, out towards the flank
      Vector3(1.0f, 0.0f, 0.0f),    // 3: CF
  };
  const auto connections = BuildConnections(positions);

  // The CF is the most advanced player: no outgoing connection.
  for (const auto& c : connections) EXPECT_NE(c.fromIndex, 3);

  // Every non-forward player connects forward to exactly one teammate.
  EXPECT_EQ(connections.size(), 3u);

  // GK connects to the laterally-closest CB (CB 1, near the centre).
  bool gkConnectsToCB1 = false;
  for (const auto& c : connections)
    if (c.fromIndex == 0 && c.toIndex == 1) gkConnectsToCB1 = true;
  EXPECT_TRUE(gkConnectsToCB1);

  // Both CBs connect on to the CF (the only player further forward).
  int cbToCF = 0;
  for (const auto& c : connections)
    if (c.toIndex == 3 && (c.fromIndex == 1 || c.fromIndex == 2)) cbToCF++;
  EXPECT_EQ(cbToCF, 2);
}

TEST(FormationGraphicLayoutTest, ConnectionsNeverSelfLoopOrGoOutOfRange) {
  std::vector<Vector3> positions = {
      Vector3(-1.0f, 0.0f, 0.0f),  Vector3(-0.6f, -0.6f, 0.0f), Vector3(-0.6f, 0.6f, 0.0f),
      Vector3(-0.2f, -0.3f, 0.0f), Vector3(-0.2f, 0.3f, 0.0f),  Vector3(0.6f, 0.0f, 0.0f),
  };
  const auto connections = BuildConnections(positions);
  for (const auto& c : connections) {
    EXPECT_NE(c.fromIndex, c.toIndex);
    EXPECT_GE(c.fromIndex, 0);
    EXPECT_LT(c.fromIndex, (int)positions.size());
    EXPECT_GE(c.toIndex, 0);
    EXPECT_LT(c.toIndex, (int)positions.size());
  }
}

// --- Panel geometry ---
//
// The graphic is an alpha-blended overlay on the live 3-D scene (spec 1.1),
// so it has to be big enough to read as a broadcast card yet leave the pitch
// visible around it, and its two content areas - the pitch schematic and the
// substitutes column - have to be laid out against each other rather than
// each being sized independently.

TEST(FormationPanelGeometryTest, PanelIsHorizontallyCentredAndLeavesTheSceneVisible) {
  const auto g = ComputePanelGeometry(kWide);
  EXPECT_NEAR(g.panelX, 100.0f - g.panelX - g.panelWidth, 0.01f);
  EXPECT_GT(g.panelX, 5.0f);
  EXPECT_LT(g.panelX + g.panelWidth, 95.0f);
  EXPECT_GT(g.panelY, 0.0f);
  EXPECT_LT(g.panelY + g.panelHeight, 100.0f);
}

TEST(FormationPanelGeometryTest, SubsColumnAndPitchNeverOverlapAndStayInsideThePanel) {
  const auto g = ComputePanelGeometry(kWide);
  EXPECT_GE(g.subsX, 0.0f);
  EXPECT_LE(g.subsX + g.subsWidth, g.pitchX);           // column ends before the pitch starts
  EXPECT_LE(g.pitchX + g.pitchWidth, g.panelWidth);      // pitch ends inside the panel
  EXPECT_GE(g.pitchY, g.headerHeight);                   // body clears the header bar
  EXPECT_LE(g.pitchY + g.pitchHeight, g.panelHeight);
  EXPECT_LE(g.subsY + g.subsHeight, g.panelHeight);
}

TEST(FormationPanelGeometryTest, PitchKeepsItsPortraitAspectOnAnyScreenShape) {
  // Gui2 percentages are anisotropic (x is a fraction of width, y of height),
  // so the same layout on a 4:3 screen must still produce a portrait pitch of
  // the same real-world proportions - otherwise the schematic squashes.
  const auto wide = ComputePanelGeometry(kWide);
  const auto narrow = ComputePanelGeometry(4.0f / 3.0f);
  const float widePixelAspect = wide.pitchWidth * kWide / wide.pitchHeight;
  const float narrowPixelAspect = narrow.pitchWidth * (4.0f / 3.0f) / narrow.pitchHeight;
  EXPECT_NEAR(widePixelAspect, narrowPixelAspect, 0.001f);
  EXPECT_LT(widePixelAspect, 1.0f);  // portrait
}

TEST(FormationPanelGeometryTest, SubsColumnGetsAWorkableShareOfThePanel) {
  const auto g = ComputePanelGeometry(kWide);
  EXPECT_GT(g.subsWidth, g.pitchWidth * 0.5f);
}

// --- Formation arrangement ---
//
// Raw database positions put icons wherever the tactics happen to place a
// player, which is what let names and jerseys pile on top of each other.
// ArrangeFormation snaps the XI into readable rows instead.

TEST(FormationArrangementTest, GoalkeeperSitsAloneAtTheBottomCentre) {
  const auto slots = FourFourTwo();
  const auto points = ArrangeFormation(PositionsOf(slots), RolesOf(slots));
  ASSERT_EQ(points.size(), slots.size());
  EXPECT_NEAR(points[0].xPercent, 50.0f, 0.01f);
  EXPECT_GT(points[0].yPercent, 85.0f);
  for (size_t i = 1; i < points.size(); i++) EXPECT_LT(points[i].yPercent, points[0].yPercent);
}

TEST(FormationArrangementTest, FourFourTwoResolvesToThreeOutfieldRows) {
  const auto slots = FourFourTwo();
  const auto points = ArrangeFormation(PositionsOf(slots), RolesOf(slots));

  std::vector<float> outfieldRows;
  for (size_t i = 1; i < points.size(); i++) {
    bool seen = false;
    for (float y : outfieldRows)
      if (std::fabs(y - points[i].yPercent) < 0.01f) seen = true;
    if (!seen) outfieldRows.push_back(points[i].yPercent);
  }
  EXPECT_EQ(outfieldRows.size(), 3u);

  // Back four deepest, strikers highest.
  for (int back = 1; back <= 4; back++)
    for (int fwd = 9; fwd <= 10; fwd++) EXPECT_GT(points[back].yPercent, points[fwd].yPercent);
}

TEST(FormationArrangementTest, PlayersKeepTheirLeftToRightOrderWithinARow) {
  const auto slots = FourFourTwo();
  const auto points = ArrangeFormation(PositionsOf(slots), RolesOf(slots));
  // Slots 1..4 are the back line, ordered right-back to left-back in database
  // space (y ascending), which must read left to right across the panel.
  EXPECT_LT(points[1].xPercent, points[2].xPercent);
  EXPECT_LT(points[2].xPercent, points[3].xPercent);
  EXPECT_LT(points[3].xPercent, points[4].xPercent);
}

TEST(FormationArrangementTest, NeighboursInARowStayAtLeastAnIconApart) {
  const auto slots = FourFourTwo();
  const auto points = ArrangeFormation(PositionsOf(slots), RolesOf(slots));
  // The whole point of the rework: a jersey icon is sized off this gap, so it
  // has to be big enough for an icon plus breathing room at any row width.
  EXPECT_GE(MinHorizontalGap(points), FormationGraphicLayout::kMinIconGapPercent);
}

TEST(FormationArrangementTest, ACompactRowStaysNarrowerThanAFlankToFlankRow) {
  // Two central midfielders should not be flung out to the touchlines just
  // because they are the only two in their line.
  std::vector<Vector3> positions = {
      Vector3(-1.0f, 0.0f, 0.0f),  Vector3(-0.7f, -0.9f, 0.0f), Vector3(-0.7f, 0.9f, 0.0f),
      Vector3(0.0f, -0.12f, 0.0f), Vector3(0.0f, 0.12f, 0.0f),
  };
  std::vector<e_PlayerRole> roles = {e_PlayerRole_GK, e_PlayerRole_RB, e_PlayerRole_LB,
                                     e_PlayerRole_CM, e_PlayerRole_CM};
  const auto points = ArrangeFormation(positions, roles);
  const float backSpread = points[2].xPercent - points[1].xPercent;
  const float midSpread = points[4].xPercent - points[3].xPercent;
  EXPECT_LT(midSpread, backSpread);
}

TEST(FormationArrangementTest, ALoneForwardIsCentred) {
  std::vector<Vector3> positions = {
      Vector3(-1.0f, 0.0f, 0.0f), Vector3(-0.7f, -0.5f, 0.0f), Vector3(-0.7f, 0.5f, 0.0f),
      Vector3(0.8f, 0.3f, 0.0f),
  };
  std::vector<e_PlayerRole> roles = {e_PlayerRole_GK, e_PlayerRole_CB, e_PlayerRole_CB,
                                     e_PlayerRole_CF};
  const auto points = ArrangeFormation(positions, roles);
  EXPECT_NEAR(points[3].xPercent, 50.0f, 0.01f);
}

TEST(FormationArrangementTest, EveryPlayerStaysInsideThePitchArea) {
  const auto slots = FourFourTwo();
  const auto points = ArrangeFormation(PositionsOf(slots), RolesOf(slots));
  for (const auto& p : points) {
    EXPECT_GE(p.xPercent, 0.0f);
    EXPECT_LE(p.xPercent, 100.0f);
    EXPECT_GE(p.yPercent, 0.0f);
    EXPECT_LE(p.yPercent, 100.0f);
  }
}

TEST(FormationArrangementTest, AFormationWithoutAKeeperStillArrangesEveryone) {
  std::vector<Vector3> positions = {Vector3(-0.7f, -0.5f, 0.0f), Vector3(-0.7f, 0.5f, 0.0f),
                                    Vector3(0.6f, 0.0f, 0.0f)};
  std::vector<e_PlayerRole> roles = {e_PlayerRole_CB, e_PlayerRole_CB, e_PlayerRole_CF};
  const auto points = ArrangeFormation(positions, roles);
  ASSERT_EQ(points.size(), 3u);
  for (const auto& p : points) EXPECT_LE(p.yPercent, 100.0f);
}

// --- Substitutes column ---

TEST(SubsColumnLayoutTest, EveryRowFitsInsideTheColumn) {
  const float columnHeight = 70.0f;
  const auto layout = ComputeSubsLayout(12, columnHeight);
  EXPECT_GT(layout.rowCount, 0);
  EXPECT_LE(layout.firstRowY + layout.rowCount * layout.rowHeight, columnHeight);
}

TEST(SubsColumnLayoutTest, RowHeightNeverDropsBelowTheLegibilityFloor) {
  // 40 substitutes is absurd, but the column must degrade by dropping rows,
  // not by shrinking the type until it cannot be read.
  const auto layout = ComputeSubsLayout(40, 70.0f);
  EXPECT_GE(layout.rowHeight, FormationGraphicLayout::kMinSubsRowHeightPercent);
  EXPECT_LT(layout.rowCount, 40);
  EXPECT_LE(layout.firstRowY + layout.rowCount * layout.rowHeight, 70.0f);
}

TEST(SubsColumnLayoutTest, AShortBenchGetsGenerousButBoundedSpacing) {
  const auto few = ComputeSubsLayout(3, 70.0f);
  const auto many = ComputeSubsLayout(12, 70.0f);
  EXPECT_EQ(few.rowCount, 3);
  EXPECT_EQ(many.rowCount, 12);
  EXPECT_GE(few.rowHeight, many.rowHeight);
  EXPECT_LE(few.rowHeight, FormationGraphicLayout::kMaxSubsRowHeightPercent);
}

TEST(SubsColumnLayoutTest, NoSubstitutesIsNotACrash) {
  const auto layout = ComputeSubsLayout(0, 70.0f);
  EXPECT_EQ(layout.rowCount, 0);
}

// --- Fitting text into a box ---

TEST(FitTextTest, TextThatAlreadyFitsKeepsItsSize) {
  EXPECT_NEAR(FitTextHeight(4.0f, 2.0f, 10.0f, 1.0f), 2.0f, 0.001f);
}

TEST(FitTextTest, OversizedTextShrinksJustEnoughToFit) {
  // Caption width scales linearly with its height, so half the width needs
  // half the height.
  EXPECT_NEAR(FitTextHeight(20.0f, 2.0f, 10.0f, 0.5f), 1.0f, 0.001f);
}

TEST(FitTextTest, ShrinkingStopsAtTheLegibilityFloor) {
  EXPECT_NEAR(FitTextHeight(100.0f, 2.0f, 10.0f, 1.2f), 1.2f, 0.001f);
}

TEST(FitTextTest, ZeroWidthTextIsNotADivideByZero) {
  EXPECT_NEAR(FitTextHeight(0.0f, 2.0f, 10.0f, 1.0f), 2.0f, 0.001f);
}

TEST(TruncateToFitTest, TextThatFitsIsLeftAlone) {
  EXPECT_EQ(TruncateToFit("SHORT", 20.0f, MonospaceWidth), "SHORT");
}

TEST(TruncateToFitTest, OverlongTextIsCutAndEllipsised) {
  // 10 units of room: 9 characters plus the one-character ellipsis.
  const std::string out = TruncateToFit("ABCDEFGHIJKLMNOP", 10.0f, MonospaceWidth);
  EXPECT_EQ(out, "ABCDEFGHI.");
  EXPECT_LE(MonospaceWidth((int)out.size()), 10.0f);
}

TEST(TruncateToFitTest, AHopelesslyNarrowBoxStillReturnsSomething) {
  const std::string out = TruncateToFit("ABCDEF", 0.5f, MonospaceWidth);
  EXPECT_FALSE(out.empty());
}

TEST(TruncateToFitTest, EmptyTextStaysEmpty) {
  EXPECT_EQ(TruncateToFit("", 10.0f, MonospaceWidth), "");
}

TEST(FormationGraphicLayoutTest, ConnectionsCanBeBuiltFromAlreadyArrangedRows) {
  // Two rows: a back three at y=76 and a lone striker at y=12. Each defender
  // should link forward to the striker, and the striker to nobody.
  std::vector<FormationGraphicLayout::PanelPoint> points = {
      {20.0f, 76.0f}, {50.0f, 76.0f}, {80.0f, 76.0f}, {50.0f, 12.0f}};
  const auto connections = BuildConnections(points);
  EXPECT_EQ(connections.size(), 3u);
  for (const auto& c : connections) {
    EXPECT_NE(c.fromIndex, 3);
    EXPECT_EQ(c.toIndex, 3);
  }
}

TEST(FormationGraphicLayoutTest, ArrangedRowsProduceOneConnectionPerNonForward) {
  const auto slots = FourFourTwo();
  const auto points = ArrangeFormation(PositionsOf(slots), RolesOf(slots));
  const auto connections = BuildConnections(points);
  // Nine of the eleven have someone ahead of them; the two strikers share the
  // top row and so have nobody to point at.
  EXPECT_EQ(connections.size(), 9u);
}

// --- Formation label ---

TEST(FormationLabelTest, AFourFourTwoReadsAsFourFourTwo) {
  const auto slots = FourFourTwo();
  const auto points = ArrangeFormation(PositionsOf(slots), RolesOf(slots));
  EXPECT_EQ(FormationGraphicLayout::FormationLabel(points, RolesOf(slots)), "4-4-2");
}

TEST(FormationLabelTest, TheKeeperIsNotCounted) {
  std::vector<Vector3> positions = {
      Vector3(-1.0f, 0.0f, 0.0f), Vector3(-0.7f, -0.5f, 0.0f), Vector3(-0.7f, 0.5f, 0.0f),
      Vector3(0.8f, 0.0f, 0.0f)};
  std::vector<e_PlayerRole> roles = {e_PlayerRole_GK, e_PlayerRole_CB, e_PlayerRole_CB,
                                     e_PlayerRole_CF};
  const auto points = ArrangeFormation(positions, roles);
  EXPECT_EQ(FormationGraphicLayout::FormationLabel(points, roles), "2-1");
}

TEST(FormationLabelTest, LinesAreCountedFromTheBackForward) {
  // 4-2-3-1: the label must not come out reversed as 1-3-2-4.
  std::vector<Vector3> positions = {
      Vector3(-1.00f, 0.00f, 0.0f),  Vector3(-0.75f, -0.8f, 0.0f), Vector3(-0.76f, -0.3f, 0.0f),
      Vector3(-0.76f, 0.3f, 0.0f),   Vector3(-0.75f, 0.8f, 0.0f),  Vector3(-0.30f, -0.2f, 0.0f),
      Vector3(-0.30f, 0.2f, 0.0f),   Vector3(0.25f, -0.7f, 0.0f),  Vector3(0.25f, 0.0f, 0.0f),
      Vector3(0.25f, 0.7f, 0.0f),    Vector3(0.85f, 0.0f, 0.0f)};
  std::vector<e_PlayerRole> roles = {e_PlayerRole_GK, e_PlayerRole_RB, e_PlayerRole_CB,
                                     e_PlayerRole_CB, e_PlayerRole_LB, e_PlayerRole_DM,
                                     e_PlayerRole_DM, e_PlayerRole_AM, e_PlayerRole_AM,
                                     e_PlayerRole_AM, e_PlayerRole_CF};
  const auto points = ArrangeFormation(positions, roles);
  EXPECT_EQ(FormationGraphicLayout::FormationLabel(points, roles), "4-2-3-1");
}

TEST(FormationLabelTest, AnEmptyLineupHasNoLabel) {
  EXPECT_EQ(FormationGraphicLayout::FormationLabel({}, {}), "");
}

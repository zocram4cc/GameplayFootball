// Formation graphic pitch-schematic mapping and entrance-timing math
// (docs/PRESENTATION_SPEC.md section 1.1). Pure logic, no TeamData/Gui2
// dependency - see src/menu/ingame/formationgraphiclayout.hpp.

#include <gtest/gtest.h>

#include "base/math/vector3.hpp"
#include "menu/ingame/formationgraphiclayout.hpp"

using blunted::Vector3;
using FormationGraphicLayout::BuildConnections;
using FormationGraphicLayout::ComputeDisplayState;
using FormationGraphicLayout::MapPosition;
using FormationGraphicLayout::RoleZone;
using FormationGraphicLayout::SquadNumberForSlot;
using FormationGraphicLayout::ZoneForRole;

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

// --- Entrance display schedule ---

TEST(FormationGraphicDisplayScheduleTest, TooShortAnEntranceShowsNothing) {
  const auto s = ComputeDisplayState(0, 5000);
  EXPECT_EQ(s.teamID, -1);
}

TEST(FormationGraphicDisplayScheduleTest, Team0ShowsBeforeTeam1) {
  // A generous 30s entrance: team0's window should land before team1's.
  const unsigned long duration = 30000;
  int firstTeamSeen = -1;
  for (unsigned long t = 0; t < duration; t += 100) {
    const auto s = ComputeDisplayState(t, duration);
    if (s.teamID != -1) {
      firstTeamSeen = s.teamID;
      break;
    }
  }
  EXPECT_EQ(firstTeamSeen, 0);

  bool sawTeam1AfterTeam0 = false;
  bool sawTeam0 = false;
  for (unsigned long t = 0; t < duration; t += 100) {
    const auto s = ComputeDisplayState(t, duration);
    if (s.teamID == 0) sawTeam0 = true;
    if (s.teamID == 1 && sawTeam0) sawTeam1AfterTeam0 = true;
  }
  EXPECT_TRUE(sawTeam1AfterTeam0);
}

TEST(FormationGraphicDisplayScheduleTest, NothingShowsDuringTheTailClearBeforeKickoff) {
  const unsigned long duration = 30000;
  const auto s = ComputeDisplayState(duration - 500, duration);
  EXPECT_EQ(s.teamID, -1);
}

TEST(FormationGraphicDisplayScheduleTest, AlphaRampsUpThenDownAcrossAWindow) {
  const unsigned long duration = 30000;
  // Find team0's window by scanning.
  unsigned long start = 0, end = 0;
  bool inWindow = false;
  for (unsigned long t = 0; t < duration; t += 50) {
    const auto s = ComputeDisplayState(t, duration);
    if (s.teamID == 0 && !inWindow) {
      start = t;
      inWindow = true;
    }
    if (s.teamID != 0 && inWindow) {
      end = t - 50;
      break;
    }
  }
  ASSERT_GT(end, start);
  const float alphaAtStart = ComputeDisplayState(start, duration).alpha;
  const float alphaMiddle = ComputeDisplayState((start + end) / 2, duration).alpha;
  const float alphaAtEnd = ComputeDisplayState(end, duration).alpha;
  EXPECT_LT(alphaAtStart, alphaMiddle);
  EXPECT_LT(alphaAtEnd, alphaMiddle);
  EXPECT_NEAR(alphaMiddle, 1.0f, 0.01f);
}

TEST(FormationGraphicDisplayScheduleTest, PastTheEntranceShowsNothing) {
  const auto s = ComputeDisplayState(40000, 30000);
  EXPECT_EQ(s.teamID, -1);
}

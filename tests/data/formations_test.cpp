// Tests for selectable team formations (TECHNICAL_ROADMAP.md section 4C). The
// engine already reads arbitrary shapes from formation_xml; this is the set of
// shapes the game itself offers and can switch to mid-match.

#include "data/formations.hpp"

#include <gtest/gtest.h>

#include <set>

TEST(FormationsTest, OffersSeveralShapes) {
  EXPECT_GE(Formations::GetCount(), 6);
}

TEST(FormationsTest, EveryShapeFieldsTenOutfieldPlayers) {
  for (int i = 0; i < Formations::GetCount(); i++) {
    const Formations::Shape shape = Formations::GetShape(Formations::GetFormationAt(i));
    EXPECT_EQ(shape.defenders + shape.midfielders + shape.forwards, 10)
        << Formations::GetName(Formations::GetFormationAt(i));
  }
}

TEST(FormationsTest, NamesReadLikeFormations) {
  EXPECT_EQ(Formations::GetName(Formations::e_Formation_442), "4-4-2");
  EXPECT_EQ(Formations::GetName(Formations::e_Formation_433), "4-3-3");
  EXPECT_EQ(Formations::GetName(Formations::e_Formation_451), "4-5-1");
}

TEST(FormationsTest, NamesRoundTripThroughParse) {
  for (int i = 0; i < Formations::GetCount(); i++) {
    const Formations::e_Formation formation = Formations::GetFormationAt(i);
    EXPECT_EQ(Formations::Parse(Formations::GetName(formation)), formation);
  }
}

TEST(FormationsTest, UnknownNamesFallBackToTheClassicShape) {
  EXPECT_EQ(Formations::Parse(""), Formations::e_Formation_442);
  EXPECT_EQ(Formations::Parse("catenaccio"), Formations::e_Formation_442);
}

TEST(FormationsTest, TheNameMatchesTheShape) {
  for (int i = 0; i < Formations::GetCount(); i++) {
    const Formations::e_Formation formation = Formations::GetFormationAt(i);
    const Formations::Shape shape = Formations::GetShape(formation);
    const std::string expected = std::to_string(shape.defenders) + "-" +
                                 std::to_string(shape.midfielders) + "-" +
                                 std::to_string(shape.forwards);
    EXPECT_EQ(Formations::GetName(formation), expected);
  }
}

TEST(FormationsTest, EveryShapeIsDistinct) {
  std::set<std::string> names;
  for (int i = 0; i < Formations::GetCount(); i++)
    names.insert(Formations::GetName(Formations::GetFormationAt(i)));
  EXPECT_EQ(static_cast<int>(names.size()), Formations::GetCount());
}

// --- Layout: what the engine needs to place the eleven ---

TEST(FormationsLayoutTest, EveryFormationFieldsElevenPlayersWithOneKeeper) {
  for (int i = 0; i < Formations::GetCount(); i++) {
    const Formations::e_Formation formation = Formations::GetFormationAt(i);
    const std::vector<Formations::Slot> layout = Formations::GetLayout(formation);

    ASSERT_EQ(layout.size(), 11u) << Formations::GetName(formation);
    int keepers = 0;
    for (const Formations::Slot& slot : layout) {
      if (slot.role == e_PlayerRole_GK)
        keepers++;
    }
    EXPECT_EQ(keepers, 1) << Formations::GetName(formation);
  }
}

TEST(FormationsLayoutTest, TheRolesMatchTheAdvertisedShape) {
  for (int i = 0; i < Formations::GetCount(); i++) {
    const Formations::e_Formation formation = Formations::GetFormationAt(i);
    const Formations::Shape shape = Formations::GetShape(formation);

    int defenders = 0;
    int midfielders = 0;
    int forwards = 0;
    for (const Formations::Slot& slot : Formations::GetLayout(formation)) {
      switch (slot.role) {
        case e_PlayerRole_CB:
        case e_PlayerRole_LB:
        case e_PlayerRole_RB:
          defenders++;
          break;
        case e_PlayerRole_DM:
        case e_PlayerRole_CM:
        case e_PlayerRole_LM:
        case e_PlayerRole_RM:
        case e_PlayerRole_AM:
          midfielders++;
          break;
        case e_PlayerRole_CF:
          forwards++;
          break;
        default:
          break;
      }
    }

    EXPECT_EQ(defenders, shape.defenders) << Formations::GetName(formation);
    EXPECT_EQ(midfielders, shape.midfielders) << Formations::GetName(formation);
    EXPECT_EQ(forwards, shape.forwards) << Formations::GetName(formation);
  }
}

TEST(FormationsLayoutTest, PositionsStayInsideTheNormalizedPitch) {
  for (int i = 0; i < Formations::GetCount(); i++) {
    for (const Formations::Slot& slot : Formations::GetLayout(Formations::GetFormationAt(i))) {
      EXPECT_GE(slot.position.coords[0], -1.0f);
      EXPECT_LE(slot.position.coords[0], 1.0f);
      EXPECT_GE(slot.position.coords[1], -1.0f);
      EXPECT_LE(slot.position.coords[1], 1.0f);
    }
  }
}

TEST(FormationsLayoutTest, TheKeeperStandsOnHisOwnGoalLineAndForwardsUpField) {
  for (int i = 0; i < Formations::GetCount(); i++) {
    const std::vector<Formations::Slot> layout =
        Formations::GetLayout(Formations::GetFormationAt(i));
    for (const Formations::Slot& slot : layout) {
      if (slot.role == e_PlayerRole_GK)
        EXPECT_LT(slot.position.coords[0], -0.9f);
      if (slot.role == e_PlayerRole_CF)
        EXPECT_GT(slot.position.coords[0], 0.3f);
    }
  }
}

TEST(FormationsLayoutTest, NoTwoPlayersStandOnTheSameSpot) {
  for (int i = 0; i < Formations::GetCount(); i++) {
    const std::vector<Formations::Slot> layout =
        Formations::GetLayout(Formations::GetFormationAt(i));
    for (size_t a = 0; a < layout.size(); a++) {
      for (size_t b = a + 1; b < layout.size(); b++) {
        const float distance = (layout[a].position - layout[b].position).GetLength();
        EXPECT_GT(distance, 0.05f) << Formations::GetName(Formations::GetFormationAt(i));
      }
    }
  }
}

// --- The database format the engine parses ---

TEST(FormationsXmlTest, BuildsElevenEntriesTheEngineCanRead) {
  const std::string xml = Formations::BuildFormationXml(Formations::e_Formation_433);

  for (int player = 1; player <= 11; player++) {
    const std::string tag = "<p" + std::to_string(player) + ">";
    EXPECT_NE(xml.find(tag), std::string::npos) << tag;
  }
  EXPECT_NE(xml.find("<role>GK</role>"), std::string::npos);
  EXPECT_NE(xml.find("<position>"), std::string::npos);
}

TEST(FormationsXmlTest, TheXmlCarriesTheRolesOfTheChosenShape) {
  const std::string xml = Formations::BuildFormationXml(Formations::e_Formation_424);
  int forwards = 0;
  size_t at = xml.find("<role>CF</role>");
  while (at != std::string::npos) {
    forwards++;
    at = xml.find("<role>CF</role>", at + 1);
  }
  EXPECT_EQ(forwards, Formations::GetShape(Formations::e_Formation_424).forwards);
}

// --- Picking a formation for a desired shape (late-game switches) ---

TEST(FormationsShapeTest, FindsTheFormationForAChasingShape) {
  Formations::Shape chase;
  chase.defenders = 3;
  chase.midfielders = 4;
  chase.forwards = 3;
  EXPECT_EQ(Formations::FromShape(chase), Formations::e_Formation_343);

  Formations::Shape allOut;
  allOut.defenders = 4;
  allOut.midfielders = 2;
  allOut.forwards = 4;
  EXPECT_EQ(Formations::FromShape(allOut), Formations::e_Formation_424);
}

TEST(FormationsShapeTest, FallsBackToTheClosestShapeItHas) {
  Formations::Shape odd;
  odd.defenders = 2;
  odd.midfielders = 5;
  odd.forwards = 3;
  const Formations::e_Formation chosen = Formations::FromShape(odd);
  const Formations::Shape got = Formations::GetShape(chosen);
  // Whatever it picks must still be a real, full shape.
  EXPECT_EQ(got.defenders + got.midfielders + got.forwards, 10);
  EXPECT_LE(got.defenders, 3);
}

// --- Arbitrary shapes the user can build for himself ---

TEST(FormationsCustomTest, AnyBandSplitTotallingTenIsValid) {
  EXPECT_TRUE(Formations::IsValidShape(Formations::MakeShape(4, 4, 2)));
  EXPECT_TRUE(Formations::IsValidShape(Formations::MakeShape(6, 3, 1)));
  EXPECT_TRUE(Formations::IsValidShape(Formations::MakeShape(6, 4, 0)));
  EXPECT_TRUE(Formations::IsValidShape(Formations::MakeShape(1, 0, 9)));
  EXPECT_TRUE(Formations::IsValidShape(Formations::MakeShape(0, 0, 10)));
}

TEST(FormationsCustomTest, ShapesThatDoNotAddUpAreRejected) {
  EXPECT_FALSE(Formations::IsValidShape(Formations::MakeShape(4, 4, 4)));
  EXPECT_FALSE(Formations::IsValidShape(Formations::MakeShape(3, 3, 3)));
  EXPECT_FALSE(Formations::IsValidShape(Formations::MakeShape(-1, 5, 6)));
}

TEST(FormationsCustomTest, ShapeNamesReadAsWritten) {
  EXPECT_EQ(Formations::ShapeName(Formations::MakeShape(6, 3, 1)), "6-3-1");
  EXPECT_EQ(Formations::ShapeName(Formations::MakeShape(1, 0, 9)), "1-0-9");
}

TEST(FormationsCustomTest, ParsesAnyValidShapeNotJustThePresets) {
  const Formations::Shape odd = Formations::ParseShape("1-0-9");
  EXPECT_EQ(odd.defenders, 1);
  EXPECT_EQ(odd.midfielders, 0);
  EXPECT_EQ(odd.forwards, 9);

  const Formations::Shape wall = Formations::ParseShape("6-4-0");
  EXPECT_EQ(wall.defenders, 6);
  EXPECT_EQ(wall.forwards, 0);
}

TEST(FormationsCustomTest, NonsenseParsesBackToTheClassicShape) {
  const Formations::Shape fallback = Formations::ParseShape("banana");
  EXPECT_EQ(Formations::ShapeName(fallback), "4-4-2");
  EXPECT_EQ(Formations::ShapeName(Formations::ParseShape("4-4-4")), "4-4-2");
}

TEST(FormationsCustomTest, EveryValidShapeYieldsAPlayableEleven) {
  for (int defenders = 0; defenders <= 10; defenders++) {
    for (int midfielders = 0; midfielders + defenders <= 10; midfielders++) {
      const int forwards = 10 - defenders - midfielders;
      const Formations::Shape shape = Formations::MakeShape(defenders, midfielders, forwards);
      const std::vector<Formations::Slot> layout = Formations::GetLayoutForShape(shape);

      ASSERT_EQ(layout.size(), 11u) << Formations::ShapeName(shape);

      int keepers = 0;
      for (const Formations::Slot& slot : layout) {
        if (slot.role == e_PlayerRole_GK)
          keepers++;
        EXPECT_GE(slot.position.coords[0], -1.0f) << Formations::ShapeName(shape);
        EXPECT_LE(slot.position.coords[0], 1.0f) << Formations::ShapeName(shape);
        EXPECT_GE(slot.position.coords[1], -1.0f) << Formations::ShapeName(shape);
        EXPECT_LE(slot.position.coords[1], 1.0f) << Formations::ShapeName(shape);
      }
      EXPECT_EQ(keepers, 1) << Formations::ShapeName(shape);
    }
  }
}

TEST(FormationsCustomTest, NobodyOverlapsEvenInACrowdedBand) {
  const std::vector<Formations::Slot> layout =
      Formations::GetLayoutForShape(Formations::MakeShape(0, 0, 10));
  for (size_t a = 0; a < layout.size(); a++) {
    for (size_t b = a + 1; b < layout.size(); b++)
      EXPECT_GT((layout[a].position - layout[b].position).GetLength(), 0.05f);
  }
}

TEST(FormationsCustomTest, TheXmlFollowsTheCustomShapeToo) {
  const std::string xml = Formations::BuildFormationXmlForShape(Formations::MakeShape(1, 0, 9));
  int forwards = 0;
  size_t at = xml.find("<role>CF</role>");
  while (at != std::string::npos) {
    forwards++;
    at = xml.find("<role>CF</role>", at + 1);
  }
  EXPECT_EQ(forwards, 9);
  EXPECT_NE(xml.find("<p11>"), std::string::npos);
}

TEST(FormationsCustomTest, ClampingKeepsAUserEditWithinTenPlayers) {
  // Dragging defenders up has to take the players from somewhere.
  const Formations::Shape shape = Formations::MakeShapeClamped(8, 5, 0);
  EXPECT_TRUE(Formations::IsValidShape(shape));
  EXPECT_EQ(shape.defenders, 8);

  const Formations::Shape empty = Formations::MakeShapeClamped(0, 0, 0);
  EXPECT_TRUE(Formations::IsValidShape(empty));
}

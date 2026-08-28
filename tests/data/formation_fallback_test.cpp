// A team whose formation_xml is empty must still line up.
//
// /hdg/ (team 11 in databases/default/database.sqlite) imports with both
// formation_xml and formation_factory_xml empty, so TeamData's parse loop never
// fires and every one of the eleven FormationEntry slots keeps its default:
// position (0,0) and role CM. Captured on a rendered frame of the game plan
// page - all eleven player cards drawn on top of each other on the centre spot,
// one legible portrait and a smear of overlapping names.
//
// The engine has a 4-4-2 of its own (Formations::BuildFormationXml) and PES
// always lines a squad up in *some* shape, so a missing formation is a fallback,
// never a pile.

#include "data/formations.hpp"

#include <gtest/gtest.h>

namespace {

int CountSlots(const std::string& xml) {
  int found = 0;
  for (int i = 1; i <= 11; i++) {
    if (xml.find("<p" + std::to_string(i) + ">") != std::string::npos) found++;
  }
  return found;
}

}  // namespace

TEST(FormationFallbackTest, KeepsTheTeamsOwnFormationWhenItHasOne) {
  const std::string own = Formations::BuildFormationXml(Formations::e_Formation_433);
  const std::string factory = Formations::BuildFormationXml(Formations::e_Formation_352);
  EXPECT_EQ(Formations::ResolveFormationXml(own, factory), own);
}

TEST(FormationFallbackTest, FallsBackToTheFactoryFormation) {
  const std::string factory = Formations::BuildFormationXml(Formations::e_Formation_352);
  EXPECT_EQ(Formations::ResolveFormationXml("", factory), factory);
}

TEST(FormationFallbackTest, FallsBackToFourFourTwoWhenBothAreEmpty) {
  const std::string resolved = Formations::ResolveFormationXml("", "");
  EXPECT_EQ(CountSlots(resolved), 11);
  EXPECT_EQ(resolved, Formations::BuildFormationXml(Formations::e_Formation_442));
}

// The observed defect: whitespace-only and structurally empty XML both have to
// count as "no formation", or the pile comes back through a different door.
TEST(FormationFallbackTest, TreatsFormationXmlWithNoSlotsAsMissing) {
  EXPECT_EQ(CountSlots(Formations::ResolveFormationXml("   \n  ", "")), 11);
  EXPECT_EQ(CountSlots(Formations::ResolveFormationXml("<tactics><x>1</x></tactics>", "")), 11);
}

// A partial formation is still a pile for the slots it omits.
TEST(FormationFallbackTest, TreatsAPartialFormationAsMissing) {
  const std::string partial = "<p1><position>-1.00,0.00</position><role>GK</role></p1>";
  EXPECT_EQ(CountSlots(Formations::ResolveFormationXml(partial, "")), 11);
}

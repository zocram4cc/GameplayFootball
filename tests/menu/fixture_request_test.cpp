// Choosing which two teams an unattended run puts on the pitch.
//
// The team-select page drives itself when menu_smoke_test_full_match is set,
// and it used to take whatever the selectors happened to default to - the first
// team in the first league, and the one after it. That is fine for a smoke test
// and useless for recording a particular fixture, so the run can name the two
// teams by database id ("showcase_team1" / "showcase_team2") and the page
// selects them.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "menu/startmatch/fixturerequest.hpp"

using FixtureRequest::EntryIndexForTeam;

namespace {

// as the selector fills up: teams of one league, ordered by name
const std::vector<std::string> kEntries = {"6", "2", "10", "9"};

}  // namespace

TEST(FixtureRequestTest, ARequestedTeamIsFoundWhereverItSitsInTheList) {
  EXPECT_EQ(EntryIndexForTeam(kEntries, "6"), 0);
  EXPECT_EQ(EntryIndexForTeam(kEntries, "9"), 3);
}

TEST(FixtureRequestTest, NoRequestLeavesTheSelectorAlone) {
  EXPECT_EQ(EntryIndexForTeam(kEntries, ""), -1);
}

TEST(FixtureRequestTest, ATeamThatIsNotInThisLeagueLeavesTheSelectorAlone) {
  // Better to play the default fixture than to silently pick the wrong side.
  EXPECT_EQ(EntryIndexForTeam(kEntries, "4"), -1);
}

TEST(FixtureRequestTest, AnEmptyListIsNotIndexedIntoAtAll) {
  EXPECT_EQ(EntryIndexForTeam({}, "6"), -1);
  // the selector's own "no teams found" placeholder carries an empty id, and an
  // empty request must not match it
  EXPECT_EQ(EntryIndexForTeam({""}, ""), -1);
}

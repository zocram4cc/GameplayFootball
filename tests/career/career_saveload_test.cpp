#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "menu/career/career_database.hpp"

namespace {

using blunted::CareerDatabase;

namespace fs = std::filesystem;

// Writes the given career.save contents into a fresh temp directory and returns
// the directory path for CareerDatabase::Initialize().
std::string WriteSaveFile(const std::string& contents) {
  fs::path dir = fs::temp_directory_path() / "league_soccer_saveload_test";
  fs::create_directories(dir);
  std::ofstream file(dir / "career.save", std::ios::trunc);
  file << contents;
  file.close();
  return dir.string();
}

// A save file riddled with non-numeric values in numeric fields must load
// without throwing: valid fields are kept, bad ones fall back to defaults, and
// every roster row is still parsed.
TEST(CareerSaveLoadTest, CorruptNumericFieldsDoNotThrow) {
  const std::string contents =
      "# Career Save: My Club\n"
      "name=My Club\n"
      "mode=notanumber\n"
      "clubID=12\n"
      "reputation=abc\n"
      "boardConfidence=\n"
      "transferBudget=not-a-budget\n"
      "wageBudget=250000\n"
      "season=oops\n"
      "player.0=Alice|ST|notanage|88|90|1000000|5000\n"
      "player.1=Bob|GK|29|notanovr||badvalue|\n";

  CareerDatabase& db = CareerDatabase::GetInstance();
  db.Initialize(WriteSaveFile(contents));

  ASSERT_NO_THROW({ EXPECT_TRUE(db.LoadCareerSave("My Club")); });

  CareerSave* save = db.GetActiveSave();
  ASSERT_NE(save, nullptr);

  // Valid fields survive.
  EXPECT_EQ(save->name, "My Club");
  EXPECT_EQ(save->club.clubID, 12);
  EXPECT_EQ(save->wageBudget, 250000);

  // Corrupt numeric fields fall back to a default rather than crashing.
  EXPECT_EQ(save->reputation, 0);
  EXPECT_EQ(save->boardConfidence, 0);
  EXPECT_EQ(save->transferBudget, 0);
  EXPECT_EQ(save->season.currentSeason, 0);

  // Both roster rows load; bad per-player fields default, good ones survive.
  ASSERT_EQ(save->roster.size(), 2u);
  EXPECT_EQ(save->roster[0].name, "Alice");
  EXPECT_EQ(save->roster[0].position, "ST");
  EXPECT_EQ(save->roster[0].age, 0);   // "notanage" -> 0
  EXPECT_EQ(save->roster[0].ovr, 88);  // valid
  EXPECT_EQ(save->roster[1].name, "Bob");
  EXPECT_EQ(save->roster[1].ovr, 0);  // "notanovr" -> 0
}

// A well-formed save round-trips its values correctly.
TEST(CareerSaveLoadTest, ValidSaveLoadsValues) {
  const std::string contents =
      "# Career Save: United\n"
      "name=United\n"
      "clubID=7\n"
      "reputation=72\n"
      "transferBudget=5000000\n"
      "player.0=Carol|CM|24|81|86|2000000|9000\n";

  CareerDatabase& db = CareerDatabase::GetInstance();
  db.Initialize(WriteSaveFile(contents));

  ASSERT_TRUE(db.LoadCareerSave("United"));
  CareerSave* save = db.GetActiveSave();
  ASSERT_NE(save, nullptr);

  EXPECT_EQ(save->name, "United");
  EXPECT_EQ(save->club.clubID, 7);
  EXPECT_EQ(save->reputation, 72);
  EXPECT_EQ(save->transferBudget, 5000000);
  ASSERT_EQ(save->roster.size(), 1u);
  EXPECT_EQ(save->roster[0].name, "Carol");
  EXPECT_EQ(save->roster[0].age, 24);
  EXPECT_EQ(save->roster[0].ovr, 81);
  EXPECT_EQ(save->roster[0].wage, 9000);
}

// Older saves wrote only 7 player fields; they must still load, with the newer
// fields keeping their struct defaults rather than being garbage.
TEST(CareerSaveLoadTest, LegacySevenFieldPlayerLoads) {
  const std::string contents =
      "name=Old Save\n"
      "player.0=Dave|CB|30|79|79|3000000|8000\n";

  CareerDatabase& db = CareerDatabase::GetInstance();
  db.Initialize(WriteSaveFile(contents));
  ASSERT_TRUE(db.LoadCareerSave("Old Save"));

  CareerSave* save = db.GetActiveSave();
  ASSERT_NE(save, nullptr);
  ASSERT_EQ(save->roster.size(), 1u);
  EXPECT_EQ(save->roster[0].name, "Dave");
  EXPECT_EQ(save->roster[0].wage, 8000);
  // Newer fields absent in the file -> struct defaults.
  EXPECT_EQ(save->roster[0].morale, 50);
  EXPECT_EQ(save->roster[0].fitness, 100);
  EXPECT_EQ(save->roster[0].careerGoals, 0);
}

// A full save -> load round-trip must preserve player progression (goals,
// morale, etc.), not just the basic identity fields.
TEST(CareerSaveLoadTest, RoundTripPreservesPlayerProgress) {
  CareerDatabase& db = CareerDatabase::GetInstance();
  db.Initialize(WriteSaveFile(""));  // sets save dir; file overwritten on save

  ASSERT_TRUE(db.CreateNewCareer("Rovers", "manager", "Boss"));
  CareerSave* save = db.GetActiveSave();
  ASSERT_NE(save, nullptr);
  save->roster.clear();
  PlayerCareerState striker;
  striker.name = "Emma";
  striker.position = "ST";
  striker.ovr = 84;
  striker.morale = 88;
  striker.matchForm = 73;
  striker.fitness = 91;
  striker.careerGoals = 27;
  striker.careerAssists = 11;
  striker.matchesPlayed = 40;
  save->roster.push_back(striker);
  save->reputation = 64;
  save->transferBudget = 8000000;

  ASSERT_TRUE(db.SaveCareerData());

  // Reload from disk and confirm progression survived.
  ASSERT_TRUE(db.LoadCareerSave("Rovers"));
  CareerSave* loaded = db.GetActiveSave();
  ASSERT_NE(loaded, nullptr);
  ASSERT_EQ(loaded->roster.size(), 1u);
  const PlayerCareerState& e = loaded->roster[0];
  EXPECT_EQ(e.name, "Emma");
  EXPECT_EQ(e.ovr, 84);
  EXPECT_EQ(e.morale, 88);
  EXPECT_EQ(e.matchForm, 73);
  EXPECT_EQ(e.fitness, 91);
  EXPECT_EQ(e.careerGoals, 27);
  EXPECT_EQ(e.careerAssists, 11);
  EXPECT_EQ(e.matchesPlayed, 40);

  // Mirrored/derived fields are kept consistent on load.
  EXPECT_EQ(loaded->reputation, 64);
  EXPECT_EQ(loaded->club.reputation, 64);
  EXPECT_EQ(loaded->finance.transferBudget, loaded->transferBudget);
}

}  // namespace

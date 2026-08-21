// Player model map: an editable text file assigning imported fullbody
// models to database player IDs, one "<id> <directory>" pair per line.

#include <gtest/gtest.h>

#include <sstream>

#include "utils/playermodelmap.hpp"

TEST(PlayerModelMap, ParsesIdDirectoryPairs) {
  std::istringstream in(
      "# imported PES players\n"
      "117 media/imports/pes21/models/player_117\n"
      "\n"
      "204 media/imports/pes21/models/player_204\n");
  auto map = blunted::ParsePlayerModelMap(in);
  ASSERT_EQ(map.size(), 2u);
  EXPECT_EQ(map.at(117), "media/imports/pes21/models/player_117");
  EXPECT_EQ(map.at(204), "media/imports/pes21/models/player_204");
}

TEST(PlayerModelMap, IgnoresMalformedLines) {
  std::istringstream in(
      "not_a_number media/foo\n"
      "42\n"
      "43 media/ok\n");
  auto map = blunted::ParsePlayerModelMap(in);
  ASSERT_EQ(map.size(), 1u);
  EXPECT_EQ(map.at(43), "media/ok");
}

TEST(PlayerModelMap, EmptyInputEmptyMap) {
  std::istringstream in("");
  EXPECT_TRUE(blunted::ParsePlayerModelMap(in).empty());
}

// Paths with spaces in them.
//
// A 4cc export names its portraits for the player - "XXX09 - Dante.png" - and the
// config is meant to be edited by hand, so a user will write a path with a space in
// it sooner or later. Reading one token stopped at the first space and silently
// mapped the player to a truncated path, which loads nothing.

TEST(PlayerModelMap, APathWithSpacesSurvives) {
  std::istringstream in("457 imports/lcg/portraits/XXX09 - Dante.png\n");
  const std::map<int, std::string> map = blunted::ParsePlayerModelMap(in);
  ASSERT_EQ(map.size(), 1u);
  EXPECT_EQ(map.at(457), "imports/lcg/portraits/XXX09 - Dante.png");
}

TEST(PlayerModelMap, TrailingWhitespaceIsNotPartOfThePath) {
  std::istringstream in("1 media/players/custom/lcg_2701   \n");
  const std::map<int, std::string> map = blunted::ParsePlayerModelMap(in);
  ASSERT_EQ(map.size(), 1u);
  EXPECT_EQ(map.at(1), "media/players/custom/lcg_2701");
}

TEST(PlayerModelMap, ALineWithNoPathIsStillRefused) {
  std::istringstream in("1   \n2 real/path\n");
  const std::map<int, std::string> map = blunted::ParsePlayerModelMap(in);
  ASSERT_EQ(map.size(), 1u);
  EXPECT_EQ(map.at(2), "real/path");
}

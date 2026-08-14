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

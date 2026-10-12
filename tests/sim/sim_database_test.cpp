#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "utils/database.hpp"

namespace {

std::string WriteScript(const std::string& name, const std::string& sql) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream(path) << sql;
  return path.string();
}

}  // namespace

TEST(Database, LoadsSqlScriptIntoMemory) {
  const std::string path = WriteScript("gfsim_db_ok.sql",
                                       "CREATE TABLE teams (id INTEGER, name TEXT);\n"
                                       "INSERT INTO teams VALUES (1, 'Home XI'), (2, 'Away XI');\n");
  blunted::Database db;
  ASSERT_TRUE(db.LoadSqlScript(path));
  auto result = db.Query("SELECT name FROM teams ORDER BY id");
  ASSERT_EQ(result->data.size(), 2u);
  EXPECT_EQ(result->data[1][0], "Away XI");
  std::filesystem::remove(path);
}

TEST(Database, RejectsBrokenScriptAndMissingFile) {
  const std::string path = WriteScript("gfsim_db_bad.sql", "CREATE TABLE (;\n");
  blunted::Database db;
  EXPECT_FALSE(db.LoadSqlScript(path));
  EXPECT_FALSE(db.LoadSqlScript(path + ".absent"));
  std::filesystem::remove(path);
}

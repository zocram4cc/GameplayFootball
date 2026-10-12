// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "database.hpp"

#include <fstream>
#include <iterator>

#include "base/log.hpp"
#include "sqlite3.h"

namespace blunted {

Database::Database() : db(0) {}

Database::~Database() {
  if (db)
    sqlite3_close(db);
}

bool Database::Load(const std::string& filename) {
  // close previously opened db
  if (db)
    sqlite3_close(db);

  int value = sqlite3_open_v2(filename.c_str(), &db, SQLITE_OPEN_READWRITE, 0);
  if (value) {
    // Log(e_FatalError, "Database", "Database", "Could not open database '" + filename + "'");
    return false;
  } else
    return true;
}

bool Database::LoadSqlScript(const std::string& filename) {
  std::ifstream in(filename);
  if (!in) return false;
  const std::string sql((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (db) sqlite3_close(db);
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) return false;
  char* errorMsg = nullptr;
  if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMsg) != SQLITE_OK) {
    Log(e_Error, "Database", "LoadSqlScript", filename + ": " + (errorMsg ? errorMsg : "unknown error"));
    sqlite3_free(errorMsg);
    return false;
  }
  return true;
}

std::unique_ptr<DatabaseResult> Database::Query(const std::string& query) {
  int rows, columns;
  char** result;
  char* errorMsg = nullptr;

  int returnValue = sqlite3_get_table(db, query.c_str(), &result, &rows, &columns, &errorMsg);
  if (errorMsg) {
    std::string errorMsgStr = errorMsg;
    Log(e_FatalError, "Database", "Query",
        "SQLite error message: '" + errorMsgStr + "' for query: '" + query + "'");
    sqlite3_free(errorMsg);
  }

  auto dbresult = std::make_unique<DatabaseResult>();

  // header
  for (int i = 0; i < columns; i++) {
    // printf("1st row heading: %s\n", result[i]);
    dbresult->header.push_back(result[i]);
  }

  // data
  for (int r = 0; r < rows; r++) {
    std::vector<std::string> tmpRow;
    for (int c = 0; c < columns; c++) {
      if (result[(r + 1) * columns + c] == nullptr) {
        tmpRow.push_back("");
      } else {
        // printf("data: %s\n", result[(r + 1) * columns + c]);
        tmpRow.push_back(result[(r + 1) * columns + c]);
      }
    }
    dbresult->data.push_back(tmpRow);
  }

  sqlite3_free_table(result);

  return dbresult;
}

}  // namespace blunted

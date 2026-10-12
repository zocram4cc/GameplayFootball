#include "simcontext.hpp"

namespace {
Properties configuration;
Database* database = nullptr;
}  // namespace

Properties* GetConfiguration() {
  return &configuration;
}

Database* GetDB() {
  return database;
}

void SetDB(Database* db) {
  database = db;
}

bool IsReleaseVersion() {
  return !GetConfiguration()->GetBool("debug", false);
}

bool Verbose() {
  return !IsReleaseVersion();
}

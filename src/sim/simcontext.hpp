// Process-wide state the GameplayFootball engine reads through free functions.
// GF's main.cpp owned these; headless, the caller fills them before building a
// Match: the configuration through the Match constructor, the database here.

#ifndef _HPP_SIMCONTEXT
#define _HPP_SIMCONTEXT

#include "base/properties.hpp"
#include "utils/database.hpp"

using namespace blunted;

Properties* GetConfiguration();

// The team/player database the sim reads its squads from. Not owned.
Database* GetDB();
void SetDB(Database* db);

// GF's "debug" configuration key: off is a release run.
bool IsReleaseVersion();
bool Verbose();

#endif

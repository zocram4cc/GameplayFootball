// Editable per-player model assignments: "<databaseID> <directory>" lines,
// '#' comments. The directory holds a fullbody.object/.ase pair exported by
// tools/pes21_import/fmdl_to_fullbody.py.

#ifndef _HPP_UTILS_PLAYERMODELMAP
#define _HPP_UTILS_PLAYERMODELMAP

#include <istream>
#include <map>
#include <string>

namespace blunted {

std::map<int, std::string> ParsePlayerModelMap(std::istream& in);

// Cached load of media/players/playermodels.cfg (empty map if absent);
// returns "" when the player has no custom model.
const std::string& GetPlayerModelDir(int databaseID);

// Cached load of media/players/playerportraits.cfg (same format);
// returns "" when the player has no portrait.
const std::string& GetPlayerPortrait(int databaseID);

}  // namespace blunted

#endif

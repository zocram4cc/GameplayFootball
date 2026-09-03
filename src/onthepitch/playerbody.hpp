// Which player body the match loads, and what follows from that choice.
//
// The default is the imported PES 2021 base model; "player_body" selects another
// <name>.object / models/<name>.ase pair. Neither file ships with the repository,
// so both have to be checked before the ASE loader turns a missing one into a
// fatal error, and the legacy low-poly "fullbody" is the fallback.
//
// Whether the engine's separate hairstyle meshes apply follows the body that was
// actually loaded, not the one that was asked for: the PES body brings its own
// scalp and hair, the legacy body does not.

#ifndef _HPP_ONTHEPITCH_PLAYERBODY
#define _HPP_ONTHEPITCH_PLAYERBODY

#include <string>

namespace PlayerBody {

extern const char* const kLegacyBody;
extern const char* const kDefaultBody;

std::string ObjectPath(const std::string& bodyName);
std::string ModelPath(const std::string& bodyName);

// A per-player model from playermodels.cfg, given its directory: the wrapper
// the loader is handed, and the geometry the wrapper names. The .ase carries
// the directory's own name because it is the resource key, so the two are not
// derivable from each other by the caller.
std::string CustomObjectPath(const std::string& modelDir);
std::string CustomModelPath(const std::string& modelDir);

// The configured body when both its files are present, the legacy body otherwise.
std::string Resolve(const std::string& configured, bool objectExists, bool modelExists);

// Whether this body needs the engine's hairstyle meshes on top.
bool UsesLegacyHairstyles(const std::string& resolvedBody);

}  // namespace PlayerBody

#endif

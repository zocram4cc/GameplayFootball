#include "onthepitch/playerbody.hpp"

namespace PlayerBody {

const char* const kLegacyBody = "fullbody";
const char* const kDefaultBody = "fullbody_pes";

std::string ObjectPath(const std::string& bodyName) {
  return "media/objects/players/" + bodyName + ".object";
}

std::string ModelPath(const std::string& bodyName) {
  return "media/objects/players/models/" + bodyName + ".ase";
}

std::string Resolve(const std::string& configured, bool objectExists, bool modelExists) {
  if (configured.empty()) return kLegacyBody;
  if (configured == kLegacyBody) return kLegacyBody;  // nothing left to fall back to
  if (objectExists && modelExists) return configured;
  return kLegacyBody;
}

bool UsesLegacyHairstyles(const std::string& resolvedBody) {
  return resolvedBody == kLegacyBody;
}

}  // namespace PlayerBody

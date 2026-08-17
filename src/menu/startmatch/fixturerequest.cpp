#include "fixturerequest.hpp"

namespace FixtureRequest {

int EntryIndexForTeam(const std::vector<std::string>& entryIDs, const std::string& wantedID) {
  if (wantedID.empty()) return -1;
  for (unsigned int i = 0; i < entryIDs.size(); i++) {
    if (entryIDs.at(i) == wantedID) return (int)i;
  }
  return -1;
}

}  // namespace FixtureRequest

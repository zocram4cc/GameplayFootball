#include "entrancecast.hpp"

namespace EntranceCast {

bool ShouldBench(bool inEntrance, bool holdingOpeningFrame, bool isStaged) {
  if (!inEntrance) return false;
  if (holdingOpeningFrame) return true;
  return !isStaged;
}

std::string StadiumToken(const std::string& stadiumObjectPath) {
  for (size_t at = stadiumObjectPath.find("st"); at != std::string::npos;
       at = stadiumObjectPath.find("st", at + 1)) {
    size_t digits = at + 2;
    while (digits < stadiumObjectPath.size() &&
           stadiumObjectPath[digits] >= '0' && stadiumObjectPath[digits] <= '9')
      digits++;
    if (digits > at + 2) return stadiumObjectPath.substr(at, digits - at);
  }
  return std::string();
}

}  // namespace EntranceCast

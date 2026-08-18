#include "entrancecast.hpp"

namespace EntranceCast {

bool ShouldBench(bool inEntrance, bool holdingOpeningFrame, bool isStaged) {
  if (!inEntrance) return false;
  if (holdingOpeningFrame) return true;
  return !isStaged;
}

}  // namespace EntranceCast

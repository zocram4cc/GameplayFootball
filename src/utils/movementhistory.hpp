// Cross-thread safe home for an Animation::Apply movement history.
//
// The Put thread (HumanoidBase::Put) reads the vector while the Process thread
// (HumanoidBase::ResetPosition, e.g. when a foul-replay choreography hands
// control back) used to clear() it in place - a torn read that crashed as
// vector::at asserting on a shrunken size during 2HUG showcase matches. The
// writer now only raises a flag; the reader consumes it and clears before its
// next Apply, so the vector is never mutated while being iterated.

#ifndef _HPP_MOVEMENT_HISTORY
#define _HPP_MOVEMENT_HISTORY

#include <atomic>
#include <string>
#include <vector>

#include "base/math/quaternion.hpp"
#include "base/math/vector3.hpp"

#include "base/math/bluntmath.hpp"

namespace blunted {

struct MovementHistoryEntry {
  std::string nodeName;
  Vector3 position;
  Quaternion orientation;
  int timeDiff_ms;
};

using MovementHistory = std::vector<MovementHistoryEntry>;

class MovementHistoryBuffer {
 public:
  // Writer side: called from ResetPosition. Never touches the vector itself,
  // so a concurrent Apply cannot see its storage change underneath it.
  void RequestReset() { resetRequested.store(true, std::memory_order_release); }

  // Reader side: called from the Put thread right before Apply. Returns true
  // when a reset was pending and has now been applied; after this call the
  // vector is empty and stable for the duration of the Apply.
  bool ConsumeResetRequest() {
    if (!resetRequested.exchange(false, std::memory_order_acq_rel)) return false;
    history.clear();
    return true;
  }

  MovementHistory& Get() { return history; }
  const MovementHistory& Get() const { return history; }

 private:
  MovementHistory history;
  std::atomic<bool> resetRequested{false};
};

}  // namespace blunted

#endif  // _HPP_MOVEMENT_HISTORY

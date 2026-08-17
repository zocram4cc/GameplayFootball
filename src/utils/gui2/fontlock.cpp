#include "utils/gui2/fontlock.hpp"

namespace blunted {

std::mutex& FontMutex() {
  // Function-local: initialised on first use, before any thread can render text.
  static std::mutex mutex;
  return mutex;
}

}  // namespace blunted

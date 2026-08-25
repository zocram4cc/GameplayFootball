// The 2HUG crash: Animation::Apply indexed movementHistory->at(node) while the
// owning humanoid's ResetPosition (Process phase, e.g. when a foul-replay
// choreography hands control back) had just clear()ed the same vector from the
// other thread - PutPhase runs outside matchPutBufferMutex. The writer must
// never mutate in place; it requests a reset and the Put thread clears before
// its next Apply.

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include "utils/movementhistory.hpp"

using blunted::MovementHistoryBuffer;
using blunted::MovementHistoryEntry;

TEST(MovementHistoryBufferTest, ResetRequestClearsLazilyOnNextUse) {
  MovementHistoryBuffer buffer;
  MovementHistoryEntry entry;
  entry.nodeName = "player";
  buffer.Get().push_back(entry);

  buffer.RequestReset();  // writer thread: ResetPosition
  buffer.ConsumeResetRequest();  // Put thread: cleared before next Apply
  EXPECT_TRUE(buffer.Get().empty());
}

TEST(MovementHistoryBufferTest, ApplyWithoutResetRequestKeepsHistory) {
  MovementHistoryBuffer buffer;
  MovementHistoryEntry entry;
  entry.nodeName = "leftfoot";
  buffer.Get().push_back(entry);
  EXPECT_FALSE(buffer.ConsumeResetRequest());
  EXPECT_EQ(buffer.Get().size(), 1u);
}

TEST(MovementHistoryBufferTest, ConcurrentWriterDoesNotInvalidateReader) {
  MovementHistoryBuffer buffer;
  std::atomic<bool> stop{false};
  MovementHistoryEntry entry;
  entry.nodeName = "player";

  // Models the real race: the Process thread keeps requesting resets while
  // the Put thread consumes them and repopulates, exactly like successive
  // frames do. The reader's size() read must never see torn state.
  std::thread writer([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      buffer.RequestReset();
    }
  });

  for (int i = 0; i < 100000; ++i) {
    // History accumulates across frames between resets (one entry per body
    // node), so grow by two each round; a pending request must still leave
    // the vector empty the instant it is consumed.
    buffer.Get().push_back(entry);
    buffer.Get().push_back(entry);
    const bool wasPending = buffer.ConsumeResetRequest();
    ASSERT_EQ(wasPending, buffer.Get().empty());
    ASSERT_LE(buffer.Get().size(), 2u * (static_cast<size_t>(i) + 1));
  }
  stop.store(true);
  writer.join();
}

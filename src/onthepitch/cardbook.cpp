#include "cardbook.hpp"

namespace CardBook {

bool ShouldDefer(int foulType) {
  return foulType >= 2;
}

bool ShouldPlayAdvantage(int playerCards, int foulType) {
  if (foulType <= 0) return false;  // no foul to play on
  if (foulType == 3) return false;  // a sending-off always stops play
  return !(playerCards >= 1 && foulType >= 2);  // a second booking does too
}

void OnAdvantageExpired(Book& book, Player* player, int foulType, unsigned long foulTime_ms) {
  if (!ShouldDefer(foulType))
    return;
  book.pending.push_back({player, foulType, foulTime_ms});
}

bool HasPending(const Book& book) {
  return !book.pending.empty();
}

std::vector<DeferredCard> Drain(Book& book) {
  std::vector<DeferredCard> drained;
  drained.swap(book.pending);
  return drained;
}

}  // namespace CardBook

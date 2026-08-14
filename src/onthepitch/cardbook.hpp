// Law 5 bookkeeping: when the referee plays advantage, the free kick is
// waived but a caution or sending-off is not - it is shown at the next
// stoppage. Pure data + free functions so the queue is headless-testable
// (docs/RULESET_AUDIT.md gap 2); the Referee owns when to drain it.

#ifndef _HPP_CARD_BOOK
#define _HPP_CARD_BOOK

#include <vector>

class Player;

namespace CardBook {

struct DeferredCard {
  Player* player = nullptr;
  int foulType = 0;  // same ladder as struct Foul: 2 yellow, 3 red
  unsigned long foulTime_ms = 0;
};

struct Book {
  std::vector<DeferredCard> pending;
};

// Only card-worthy fouls survive advantage expiry; a plain foul is waived
// entirely.
bool ShouldDefer(int foulType);

// Record the sanction of an advantage-eaten foul, if it carries one.
void OnAdvantageExpired(Book& book, Player* player, int foulType, unsigned long foulTime_ms);

bool HasPending(const Book& book);

// Hand out everything recorded, oldest first, and empty the book.
std::vector<DeferredCard> Drain(Book& book);

}  // namespace CardBook

#endif

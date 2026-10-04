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

// Whether the referee may wave this one on. `playerCards` is the fouler's
// running count (Player::GetCards: 1 per yellow, 3 per red), `foulType` the
// same ladder as struct Foul. A man who already has a card does not get an
// advantage for his next booking - he is going off, so the game stops; a
// first caution still plays advantage with the card deferred, and a plain
// foul is not a booking at all.
bool ShouldPlayAdvantage(int playerCards, int foulType);

// Record the sanction of an advantage-eaten foul, if it carries one.
void OnAdvantageExpired(Book& book, Player* player, int foulType, unsigned long foulTime_ms);

bool HasPending(const Book& book);

// Hand out everything recorded, oldest first, and empty the book.
std::vector<DeferredCard> Drain(Book& book);

}  // namespace CardBook

#endif

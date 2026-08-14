// Law 5: if the referee plays advantage for a card-worthy foul, the caution or
// sending-off is still shown at the next stoppage. GF used to throw the card
// away with the foul (docs/RULESET_AUDIT.md gap 2).

#include <gtest/gtest.h>

#include "onthepitch/cardbook.hpp"

namespace {

// The book only stores the pointer; it is never dereferenced.
Player* FakePlayer(uintptr_t id) {
  return reinterpret_cast<Player*>(id);
}

}  // namespace

TEST(CardBookTest, OnlyCardWorthyFoulsSurviveAdvantageExpiry) {
  EXPECT_FALSE(CardBook::ShouldDefer(0));
  EXPECT_FALSE(CardBook::ShouldDefer(1));  // plain foul: free kick waived, done
  EXPECT_TRUE(CardBook::ShouldDefer(2));   // yellow
  EXPECT_TRUE(CardBook::ShouldDefer(3));   // red
}

TEST(CardBookTest, ADeferredCardWaitsForTheNextStoppage) {
  CardBook::Book book;
  EXPECT_FALSE(CardBook::HasPending(book));

  CardBook::OnAdvantageExpired(book, FakePlayer(1), 2, 120000);
  EXPECT_TRUE(CardBook::HasPending(book));

  const std::vector<CardBook::DeferredCard> drained = CardBook::Drain(book);
  ASSERT_EQ(drained.size(), 1u);
  EXPECT_EQ(drained[0].player, FakePlayer(1));
  EXPECT_EQ(drained[0].foulType, 2);
  EXPECT_EQ(drained[0].foulTime_ms, 120000ul);

  // Drained means drained: the same card is never shown twice.
  EXPECT_FALSE(CardBook::HasPending(book));
  EXPECT_TRUE(CardBook::Drain(book).empty());
}

TEST(CardBookTest, NonCardFoulsAreNotRecorded) {
  CardBook::Book book;
  CardBook::OnAdvantageExpired(book, FakePlayer(1), 1, 60000);
  EXPECT_FALSE(CardBook::HasPending(book));
}

TEST(CardBookTest, MultipleDeferredCardsAllComeOutInOrder) {
  CardBook::Book book;
  CardBook::OnAdvantageExpired(book, FakePlayer(1), 2, 60000);
  CardBook::OnAdvantageExpired(book, FakePlayer(2), 3, 61000);

  const std::vector<CardBook::DeferredCard> drained = CardBook::Drain(book);
  ASSERT_EQ(drained.size(), 2u);
  EXPECT_EQ(drained[0].player, FakePlayer(1));
  EXPECT_EQ(drained[0].foulType, 2);
  EXPECT_EQ(drained[1].player, FakePlayer(2));
  EXPECT_EQ(drained[1].foulType, 3);
}

// Two separate advantage-eaten yellows against the same player must both be
// recorded - together they are a sending-off, and the referee that forgets the
// first one has invented a rule.
TEST(CardBookTest, TheSamePlayerCanBeBookedTwice) {
  CardBook::Book book;
  CardBook::OnAdvantageExpired(book, FakePlayer(7), 2, 60000);
  CardBook::OnAdvantageExpired(book, FakePlayer(7), 2, 90000);
  EXPECT_EQ(CardBook::Drain(book).size(), 2u);
}

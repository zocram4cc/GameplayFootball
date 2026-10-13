// PES 2021's match clock, stoppage and period rules, as documented in
// docs/re/pes-re-tactics-clock.md. Every number comes from the user's own
// PES2021.exe at runtime (pes::PesClockConstants); nothing is transcribed.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <string>

#include "onthepitch/pesclock.hpp"
#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace {

// The values docs/re/pes-re-tactics-clock.md 7.2 lists, so the arithmetic is
// exercised without needing an install. Only the loader test asserts the real
// EXE addresses.
pes::PesClockConstants FakeConstants() {
  pes::PesClockConstants c;
  c.gameSpeed = 54.0f;             // .data 0x14351ae54
  c.halfSeconds = 16000.0f;        // .rdata 0x14259d638
  c.added2ndHalf = 9000.0f;        // .rdata 0x142632a6c
  c.addedTier_3_6_2nd = 7200.0f;   // .rdata 0x142632a68
  c.addedTier_1_2_2nd = 3600.0f;   // .rdata 0x142598590
  c.addedExtraTime = 5400.0f;      // .rdata 0x142632a64
  c.addedTier_1_2_ET = 1800.0f;    // .rdata 0x142632a60
  c.addedTier_3_6_ET = 3600.0f;    // .rdata 0x142598590 (the same float)
  c.ticksPerMinute = 960;          // code: 960 ticks per game-minute
  return c;
}

}  // namespace

// --- half length by phase (report 2.1: three tables, 45/90/105/120) ---

TEST(PesClock, PeriodEndMinutesComeFromThePesTables) {
  EXPECT_EQ(pes::PeriodEndMinutes(e_MatchPhase_1stHalf), 45);
  EXPECT_EQ(pes::PeriodEndMinutes(e_MatchPhase_2ndHalf), 90);
  EXPECT_EQ(pes::PeriodEndMinutes(e_MatchPhase_1stExtraTime), 105);
  EXPECT_EQ(pes::PeriodEndMinutes(e_MatchPhase_2ndExtraTime), 120);
  // Phases PES never runs (pre-match, shoot-out) have no clock.
  EXPECT_EQ(pes::PeriodEndMinutes(e_MatchPhase_PreMatch), 0);
  EXPECT_EQ(pes::PeriodEndMinutes(e_MatchPhase_Penalties), 0);
}

// A period end is minutes/2 x 1920 ticks (report 2.2).
TEST(PesClock, PeriodEndTicksUseThePesScale) {
  EXPECT_EQ(pes::PeriodEndTicks(e_MatchPhase_1stHalf), 43200);
  EXPECT_EQ(pes::PeriodEndTicks(e_MatchPhase_2ndHalf), 86400);
  EXPECT_EQ(pes::PeriodEndTicks(e_MatchPhase_1stExtraTime), 100800);
  EXPECT_EQ(pes::PeriodEndTicks(e_MatchPhase_2ndExtraTime), 115200);
  EXPECT_EQ(pes::PeriodEndTicks(e_MatchPhase_PreMatch), 0);
}

// Each half is 45 game-minutes of its own; extra time's are 15 (report 2.1).
TEST(PesClock, HalfLengthMinutesComeFromThePesTables) {
  EXPECT_EQ(pes::HalfLengthMinutes(e_MatchPhase_1stHalf), 45);
  EXPECT_EQ(pes::HalfLengthMinutes(e_MatchPhase_2ndHalf), 45);
  EXPECT_EQ(pes::HalfLengthMinutes(e_MatchPhase_1stExtraTime), 15);
  EXPECT_EQ(pes::HalfLengthMinutes(e_MatchPhase_2ndExtraTime), 15);
  EXPECT_EQ(pes::HalfLengthMinutes(e_MatchPhase_PreMatch), 0);
  EXPECT_EQ(pes::HalfLengthMinutes(e_MatchPhase_Penalties), 0);
}

// --- the clock unit: 1920 ticks per game-minute, 16 per game-second ---

TEST(PesClock, TicksPerGameSecondIsSixteen) {
  // The two OBS anchors agree: 960 ticks per game-minute (report 2.2), and
  // 43,200 ticks over a 45-game-minute (2700-second) half.
  EXPECT_NEAR(pes::TicksPerGameSecond(), 16.0f, 1e-6f);
  EXPECT_EQ(45 * 60 * 16, 43200);
  // 16000 clock units per match second is the same rate at 1000 units/tick.
  EXPECT_EQ(16000 * 60 / 1000, 960);
}

// --- the stored percentage form (report 2.2) ---

TEST(PesClock, PercentageFromTicksAtThePesScale) {
  EXPECT_NEAR(pes::PercentageFromTicks(0, e_MatchPhase_1stHalf), 0.0f, 1e-6f);
  // A full first half: 45:00 is half of a 90-match-game-minute, so 50%.
  // The phase table's own value is the denominator, so a first half at its end
  // (43,200 ticks) reads 5000 - the clock is full - and a second half at 90:00
  // (86,400 ticks) also reads 5000.
  EXPECT_NEAR(pes::PercentageFromTicks(43200, e_MatchPhase_1stHalf), 5000.0f, 1e-6f);
  EXPECT_NEAR(pes::PercentageFromTicks(86400, e_MatchPhase_2ndHalf), 5000.0f, 1e-6f);
  // The same clock in the second half's denominator is half-way.
  EXPECT_NEAR(pes::PercentageFromTicks(43200, e_MatchPhase_2ndHalf), 2500.0f, 1e-6f);
}

// The percentage is the record/network form, so it must round-trip.
TEST(PesClock, PercentageRoundTripsToTicks) {
  EXPECT_EQ(pes::TicksFromPercentage(0.0f, e_MatchPhase_1stHalf), 0);
  // 5000 is the full period end in every phase: the 43,200 ticks of a first
  // half and the 86,400 of a second.
  EXPECT_NEAR(pes::TicksFromPercentage(5000.0f, e_MatchPhase_1stHalf),
              pes::PeriodEndTicks(e_MatchPhase_1stHalf), 1.0);
  EXPECT_NEAR(pes::TicksFromPercentage(5000.0f, e_MatchPhase_2ndHalf),
              pes::PeriodEndTicks(e_MatchPhase_2ndHalf), 1.0);
  // Half of a second half's end.
  EXPECT_NEAR(pes::TicksFromPercentage(2500.0f, e_MatchPhase_2ndHalf), 43200, 1.0);
}

// FUN_140A581C0 floors the period at 20 minutes before scaling.
TEST(PesClock, PercentageFloorsPeriodAt20Minutes) {
  // Pre-match has no period end, so FUN_140A581C0's 20-minute floor sets the
  // scale: the denominator is 20*1920, so half of it (10 game-minutes of the
  // clock at 960 per game-minute) reads 2500.
  EXPECT_NEAR(pes::PercentageFromTicks(10 * 960, e_MatchPhase_PreMatch), 2500.0f, 1e-6f);
}

// --- the display (report 2.6) ---

TEST(PesClock, DisplayMinuteAndSecondFromASecondsCount) {
  int minute = -1, second = -1;
  pes::ClockDisplay(1350.0f, &minute, &second);  // 22:30
  EXPECT_EQ(minute, 22);
  EXPECT_EQ(second, 30);
  pes::ClockDisplay(0.0f, &minute, &second);
  EXPECT_EQ(minute, 0);
  EXPECT_EQ(second, 0);
  pes::ClockDisplay(2700.0f, &minute, &second);  // 45:00
  EXPECT_EQ(minute, 45);
  EXPECT_EQ(second, 0);
}

// FUN_140A6BD80 takes hours/minutes/seconds off a seconds count, so the clock
// never exceeds 59 minutes before the wrap: PES wraps it to 0 instead.
TEST(PesClock, DisplayWrapsTheMinuteAtSixty) {
  int minute = -1, second = -1;
  pes::ClockDisplay(3600.0f, &minute, &second);  // 60:00 -> minute wraps
  EXPECT_EQ(minute, 0);
  EXPECT_EQ(second, 0);
  pes::ClockDisplay(5399.0f, &minute, &second);  // 89:59
  EXPECT_EQ(minute, 29);
  EXPECT_EQ(second, 59);
}

// --- stoppage: the linear formula, evaluated once at half end (report 3.2) ---

TEST(PesClock, AddedTimeIsElapsedSecondsTimesRate) {
  const pes::PesClockConstants c = FakeConstants();
  // 100 match seconds of a second half.
  EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_2ndHalf, 100.0f, 7), 0.0) << "no tier bonus";
  EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_2ndHalf, 100.0f, 4), 720000);
  EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_2ndHalf, 100.0f, 2), 360000);
  // Extra time adds less: 5400 vs 9000 per match second.
  EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_ExtraTime, 100.0f, 2), 180000);
  EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_ExtraTime, 100.0f, 4), 360000);
}

// The tier gates are 1..2, 3..6 and everything else (report 3.2's branching).
TEST(PesClock, AddedTimeTierGatesAreExact) {
  const pes::PesClockConstants c = FakeConstants();
  for (int tier : {0, 7, 8, 100}) {
    EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_2ndHalf, 100.0f, tier), 0.0)
        << "2nd-half tier " << tier;
    EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_ExtraTime, 100.0f, tier), 0.0)
        << "extra-time tier " << tier;
  }
  for (int tier = 1; tier <= 2; ++tier) {
    EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_2ndHalf, 100.0f, tier), 360000)
        << "2nd-half tier " << tier;
    EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_ExtraTime, 100.0f, tier), 180000)
        << "extra-time tier " << tier;
  }
  for (int tier = 3; tier <= 6; ++tier) {
    EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_2ndHalf, 100.0f, tier), 720000)
        << "2nd-half tier " << tier;
    EXPECT_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_ExtraTime, 100.0f, tier), 360000)
        << "extra-time tier " << tier;
  }
}

// Extra time adds less stoppage than the second half (report 3.2).
TEST(PesClock, ExtraTimeAddsLessThanASecondHalf) {
  const pes::PesClockConstants c = FakeConstants();
  EXPECT_DOUBLE_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_ExtraTime, 100.0f, 4),
                   0.5 * pes::AddedTimeUnits(c, pes::e_AddedTimePhase_2ndHalf, 100.0f, 4));
  EXPECT_DOUBLE_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_ExtraTime, 100.0f, 2),
                   0.5 * pes::AddedTimeUnits(c, pes::e_AddedTimePhase_2ndHalf, 100.0f, 2));
}

// No event counters: the formula is linear in the elapsed seconds, so twice the
// elapsed is twice the added time whatever happened in between.
TEST(PesClock, AddedTimeHasNoEventCounters) {
  const pes::PesClockConstants c = FakeConstants();
  EXPECT_DOUBLE_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_2ndHalf, 200.0f, 4),
                 2.0 * pes::AddedTimeUnits(c, pes::e_AddedTimePhase_2ndHalf, 100.0f, 4));
  EXPECT_EQ(pes::AddedTimeTicks(c, pes::e_AddedTimePhase_2ndHalf, 0.0f, 4), 0);
}

// Report 3.1: the fourth official's board. Whole game-minutes, never zero once
// any time at all was lost, and clamped to the nine the HUD shows.
TEST(PesClock, AnnouncedAddedMinutesRoundUpAndClampToNine) {
  const pes::PesClockConstants c = FakeConstants();
  EXPECT_EQ(pes::AnnouncedAddedMinutes(c, pes::e_AddedTimePhase_2ndHalf, 0.0f, 7), 0);
  // Exactly one game-minute's worth of ticks, and a hair over it.
  // Tier 7 earns no bonus at all, so the board reads zero however long the
  // half ran: PES's 9000/s term is the clock's own value, already spent.
  EXPECT_EQ(pes::AnnouncedAddedMinutes(c, pes::e_AddedTimePhase_2ndHalf, 2700.0f, 7), 0);
  // 7200 clock units per second at 1000 per tick is 720 ticks, three quarters
  // of a game-minute, which rounds up to 1.
  EXPECT_EQ(pes::AnnouncedAddedMinutes(c, pes::e_AddedTimePhase_2ndHalf, 1.0f, 4), 1);
  // A whole second half at tier 4: 2700 s x 7200 units/s / 1000 = 19,440 ticks,
  // over twenty game-minutes, which the HUD clamps to nine.
  EXPECT_EQ(pes::AnnouncedAddedMinutes(c, pes::e_AddedTimePhase_2ndHalf, 2700.0f, 4),
            pes::kMaxAnnouncedAddedMinutes);
}

// --- the clock itself (report 8.1) ---

// The clock is a u32 percentage of the period's end, advanced on PES's own
// logic tick: one 1/54-second step moves it by 16/54 game-minute-ticks.
TEST(PesClock, ClockRunsAtSixteenTicksPerGameSecond) {
  const pes::PesClockConstants c = FakeConstants();
  pes::PesClock clock(c);
  clock.SetPhase(e_MatchPhase_1stHalf);
  EXPECT_EQ(clock.Ticks(), 0);

  // 16000 clock units per match second at 1000 units per tick is 16 ticks per
  // game-second, so one 1/54-second logic tick is 16/54 of a tick. The clock
  // keeps the fraction: 54 logic ticks make one match second and 16 ticks.
  for (int i = 0; i < 54; ++i) clock.Advance(1.0f / 54.0f);
  EXPECT_NEAR(clock.Ticks(), 16.0f, 1e-3);
  EXPECT_NEAR(clock.ElapsedSeconds(), 1.0f, 1e-5);

  clock.Advance(1350.0f - 1.0f);  // to 22:30, half of a 45-minute half
  EXPECT_NEAR(clock.Ticks(), 21600, 1.0);
  EXPECT_NEAR(clock.Percentage(), 2500.0f, 1.0);
  EXPECT_NEAR(clock.Ticks(), pes::PeriodEndTicks(e_MatchPhase_1stHalf) / 2, 1.0);

  clock.Advance(1350.0f);  // the half is full
  EXPECT_NEAR(clock.Ticks(), 43200, 1.0);
  EXPECT_NEAR(clock.Percentage(), 5000.0f, 1.0);
  EXPECT_NEAR(clock.PeriodFraction(), 1.0f, 1e-6f);
}

// Report 3.3: a stoppage never adds time, it freezes the clock.
TEST(PesClock, ClockFreezesWhileFrozen) {
  const pes::PesClockConstants c = FakeConstants();
  pes::PesClock clock(c);
  clock.SetPhase(e_MatchPhase_1stHalf);
  clock.Advance(600.0f);
  const uint32_t frozen = clock.Ticks();
  clock.SetFrozen(true);
  for (int i = 0; i < 1000; ++i) clock.Advance(1.0f / 54.0f);
  EXPECT_EQ(clock.Ticks(), frozen) << "a frozen clock must not advance";
  clock.SetFrozen(false);
  clock.Advance(600.0f);
  EXPECT_GT(clock.Ticks(), frozen) << "play resuming must advance it again";
}

// The clock is one monotonic counter over the whole match, so the second half
// picks up where the first left off.
TEST(PesClock, ClockIsMonotonicAcrossHalves) {
  const pes::PesClockConstants c = FakeConstants();
  pes::PesClock clock(c);
  clock.SetPhase(e_MatchPhase_1stHalf);
  clock.Advance(2700.0f);  // a full first half: 45:00
  const uint32_t atTheBreak = clock.Ticks();
  clock.SetPhase(e_MatchPhase_2ndHalf);
  EXPECT_EQ(clock.Ticks(), atTheBreak) << "the half-time break must not rewind it";
  // 45:00 is the whole of a first half but half of the 90 the second half's
  // denominator names.
  EXPECT_NEAR(clock.Percentage(), 2500.0f, 1e-3);
  clock.Advance(2700.0f);  // a full second half
  EXPECT_EQ(clock.Ticks(), 86400);
  EXPECT_NEAR(clock.Percentage(), 5000.0f, 1e-3);
  EXPECT_EQ(clock.Ticks(), pes::PeriodEndTicks(e_MatchPhase_2ndHalf));
}

// Report 2.6: the scoreboard, as PES builds it.
TEST(PesClock, ClockDisplaysThePesMinuteAndSecond) {
  const pes::PesClockConstants c = FakeConstants();
  pes::PesClock clock(c);
  clock.SetPhase(e_MatchPhase_2ndHalf);
  clock.Advance(3255.0f);  // 54:15 into the match
  int minute = -1, second = -1;
  clock.Display(&minute, &second);
  EXPECT_EQ(minute, 54);
  EXPECT_EQ(second, 15);
}

// The referee's end-of-period target: the period's own length plus the added
// time the formula produced.
TEST(PesClock, PeriodEndWithAddedIsTheWhistle) {
  const pes::PesClockConstants c = FakeConstants();
  pes::PesClock clock(c);
  clock.SetPhase(e_MatchPhase_2ndHalf);
  clock.Advance(2700.0f);  // 45:00
  // 2700 s x 7200 units/s / 1000 units per tick = 19,440 ticks, which at 960
  // ticks per game-minute is 20 game-minutes once truncated to whole ones.
  EXPECT_FLOAT_EQ(clock.PeriodEndWithAdded(4), 65.0f);
  // Without a tier the formula adds nothing, and the whistle is the half's own
  // length.
  EXPECT_FLOAT_EQ(clock.PeriodEndWithAdded(7), 45.0f);
}

// Report 2.1: restarting at extra time changes both the period end and what the
// clock reads against it.
TEST(PesClock, ExtraTimeChangesThePeriodEnd) {
  const pes::PesClockConstants c = FakeConstants();
  pes::PesClock clock(c);
  clock.SetPhase(e_MatchPhase_2ndHalf);
  clock.Advance(9000.0f);  // 150:00 on the match clock
  const uint32_t atFullTime = clock.Ticks();
  clock.SetPhase(e_MatchPhase_1stExtraTime);
  // The clock itself does not rewind; only the period it is measured against
  // changes, and a fresh period starts at its own kick-off.
  EXPECT_EQ(clock.Ticks(), atFullTime);
  EXPECT_NEAR(clock.PeriodFraction(), 0.0f, 1e-6f);
  EXPECT_EQ(clock.PeriodElapsedSeconds(), 0.0f);
  EXPECT_EQ(pes::HalfLengthMinutes(e_MatchPhase_1stExtraTime), 15);
  EXPECT_EQ(pes::PeriodEndTicks(e_MatchPhase_1stExtraTime), 100800);
  clock.Advance(900.0f);  // a full 15-minute period of extra time
  EXPECT_TRUE(clock.IsPeriodDue());
  // Extra time's added-time path: 5400 instead of 9000, tier <= 2 gets 1800.
  EXPECT_DOUBLE_EQ(pes::AddedTimeUnits(c, pes::e_AddedTimePhase_ExtraTime, 100.0f, 2), 180000);
  // Extra time's period end on the match clock: its own 15 game-minutes from
  // the 100,800 ticks the table names.
  EXPECT_EQ(clock.Ticks(), atFullTime + 14400);
}

// Every constant is read from the user's own PES2021.exe, never transcribed.
TEST(PesClockInstall, ConstantsComeFromTheExe) {
  const char* root = std::getenv("PES21_ROOT");
  if (!root) GTEST_SKIP() << "PES21_ROOT not set";
  pes::PesVfs vfs;
  std::string err;
  ASSERT_TRUE(vfs.Mount(root, &err)) << err;
  auto exe = pes::PeImage::Open(std::string(root) + "/PES2021.exe", &err);
  ASSERT_TRUE(exe) << err;
  auto c = pes::LoadPesClockConstants(*exe, &err);
  ASSERT_TRUE(c) << err;
  EXPECT_FLOAT_EQ(c->gameSpeed, 54.0f);
  EXPECT_FLOAT_EQ(c->halfSeconds, 16000.0f);
  // The sim step (`.data` 0x14351ad20) is deliberately NOT read: it is written at
  // run time, and the image's 1/48 default is the title screen's value, not a
  // match's (docs/re/pes-re-injury-fatigue.md 4b).
  EXPECT_FLOAT_EQ(c->added2ndHalf, 9000.0f);
  EXPECT_FLOAT_EQ(c->addedTier_3_6_2nd, 7200.0f);
  EXPECT_FLOAT_EQ(c->addedTier_1_2_2nd, 3600.0f);
  EXPECT_FLOAT_EQ(c->addedExtraTime, 5400.0f);
  EXPECT_FLOAT_EQ(c->addedTier_1_2_ET, 1800.0f);
}

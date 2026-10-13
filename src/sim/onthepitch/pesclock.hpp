// PES 2021's match clock, stoppage and period rules, per
// docs/re/pes-re-tactics-clock.md.
//
// The clock is a u32 percentage of `minutes x 1920` ticks, `minutes` being the
// whole match in game-minutes (90 for a regulation match): the stored value is
// `ticks * 100 / (minutes*1920/100)`, so a first half's 43,200 ticks reads 2500
// and full time 86,400 ticks reads 5000. Its tick scale is 960 per game-minute
// (16 per game-second, the other figure the report reads off the image), which
// also makes a period end `minutes/2 x 1920`: 43,200 for a first half, 86,400
// at full time.
//
// The per-second floor that drives the clock is FUN_140922DD0's
// `tickMin = sec * 16000`, i.e. 16000 clock units per match second. One tick is
// 1000 of those units: 16000 x 60 / 1000 = 960 ticks per game-minute, the other
// figure the report reads off the image, and a 45-minute half is 43,200 ticks.
// (Report 2.3's own "16000 x 60 / 500 ~= 1920" reconciliation is the same
// product with the divisor halved; the two OBS anchors - 960 per game-minute
// and 43,200 per half - fix the divisor at 1000.)
//
// PES's own `base` term comes from the live score line (a lead byte), the
// situation kind and the performer's pitch level; none of those exist in this
// engine, so the floor term is what advances the clock here (report 2.4).
//
// Added (stoppage) time is a linear formula in elapsed match seconds, evaluated
// ONCE at the end of a half with no event counters (3.2). A stoppage never adds
// time: the ClockFSM does not run while the state byte is a frozen one, so the
// clock freezes instead (3.3).
//
// Every number that lives in the image is read from the user's own
// PES2021.exe through pes::PeImage::ReadVa (PesClockConstants); nothing here is
// transcribed. The one open item the report records - how the `match_time`
// setting maps onto a per-tick increment (9.1) - does not arise here: this
// engine's clock is driven by its own tick, which PES's own constants scale.

#ifndef _HPP_ONTHEPITCH_PESCLOCK
#define _HPP_ONTHEPITCH_PESCLOCK

#include <cstdint>
#include <optional>
#include <string>

#include "gametypes.hpp"
#include "pes/exe.hpp"

namespace pes {

// docs/re/pes-re-tactics-clock.md 7.2: every clock value lives in the image,
// none of them in a WESYS constant object.
constexpr uint64_t GAME_SPEED_VA = 0x14351ae54;         // .data float, 54.0
constexpr uint64_t HALF_SECONDS_VA = 0x14259d638;        // .rdata 16000.0
constexpr uint64_t ADDED_2ND_HALF_VA = 0x142632a6c;     // .rdata 9000.0
constexpr uint64_t ADDED_TIER_3_6_2ND_VA = 0x142632a68; // .rdata 7200.0
constexpr uint64_t ADDED_TIER_1_2_2ND_VA = 0x142598590; // .rdata 3600.0
constexpr uint64_t ADDED_EX_TIME_VA = 0x142632a64;      // .rdata 5400.0
constexpr uint64_t ADDED_TIER_1_2_ET_VA = 0x142632a60;  // .rdata 1800.0
constexpr uint64_t ADDED_TIER_3_6_ET_VA = 0x142598590;  // .rdata 3600.0

// Report 2.2: FUN_140A581B0 returns 0x780 = 1920, the percentage unit's tick
// count (ticks = matchTime * 1920 / 100). Code literal, not an image constant.
constexpr uint32_t kPercentageUnitTicks = 1920;
// The clock's own rate: 960 ticks per game-minute, 16 per game-second - the
// figure the same section reads off the image, and equal to the 16000 clock
// units per match second at 1000 units per tick.
constexpr uint32_t kTicksPerGameMinute = 960;
constexpr float kClockUnitsPerTick = 1000.0f;
// Report 2.6: the HUD wraps the displayed minute at 100.
constexpr int kDisplayMinuteWrap = 100;
// Report 3.1: the HUD shows at most nine minutes of added time.
constexpr int kMaxAnnouncedAddedMinutes = 9;

struct PesClockConstants {
  float gameSpeed = 0.0f;        // the 54.0 game speed (.data)
  float halfSeconds = 0.0f;      // 16000 clock units per match second (.rdata)
  float added2ndHalf = 0.0f;     // 9000.0 per match second, 2nd half
  float addedTier_3_6_2nd = 0.0f;// 7200.0, perf tiers 3..6, 2nd half
  float addedTier_1_2_2nd = 0.0f;// 3600.0, perf tiers 1..2, 2nd half
  float addedExtraTime = 0.0f;   // 5400.0 per match second, extra time
  float addedTier_1_2_ET = 0.0f; // 1800.0, perf tiers <= 2, extra time
  float addedTier_3_6_ET = 0.0f; // 3600.0, perf tiers 3..6, extra time
  uint32_t ticksPerMinute = kTicksPerGameMinute;
};

bool ReadFloat(const PeImage& exe, uint64_t va, float* out);

// Reads every clock constant out of the install's PES2021.exe.
std::optional<PesClockConstants> LoadPesClockConstants(const PeImage&exe, std::string* error);

// --- the period tables (report 2.1) ---

// The period's end on the match clock, in game-minutes: PES's three tables
// (0x1420BE050 / 0x1420BE140 / 0x1420BE0D0) give 45 / 90 / 105 / 120 by phase.
// GF has no half-time phase, so its 1st and 2nd extra time are PES's EX_1ST and
// EX_2ND.
int PeriodEndMinutes(e_MatchPhase phase);
// The same, in clock ticks: minutes/2 x 1920, i.e. 43,200 for a first half.
uint32_t PeriodEndTicks(e_MatchPhase phase);
// The period's own length in game-minutes: 45 for either half, 15 for a period
// of extra time, and none for the periods PES never runs.
int HalfLengthMinutes(e_MatchPhase phase);
// Ticks per game-second: 960 per game-minute / 60, which is also the
// 16000-units-per-second floor at 1000 units per tick.
float TicksPerGameSecond();

// --- the clock representation (report 2.2) ---

// The stored form PES keeps: `ticks * 10000 / (minutes*1920)`, so a first half
// of a 90-game-minute match (43,200 ticks) reads 2500 and full time (86,400)
// reads 5000 (report 2.2).
float PercentageFromTicks(uint32_t ticks, e_MatchPhase phase);
uint32_t TicksFromPercentage(float percentage, e_MatchPhase phase);

// --- the display (report 2.6) ---

// Minute and second from a seconds count. In a live half the HUD adds the
// previous period's end (FUN_1420BE140's live-half rule); the clock's own
// elapsed seconds already carry it in this engine, so nothing is added here.
void ClockDisplay(float totalSeconds, int* minute_db_pitch, int* second);

// --- stoppage (report 3.2) ---

// Which of the two paths the added-time formula runs: PES evaluates it only at
// the end of the second half and of extra time.
enum e_AddedTimePhase { e_AddedTimePhase_2ndHalf, e_AddedTimePhase_ExtraTime };

// Added time in clock units: the tier bonus only, `elapsedSeconds * rate`. The
// 9000.0/5400.0 per-second term is already the clock's own value at the end of
// the period, so this engine's added time is the bonus over it. `tier` is the
// on-pitch performer's perf tier (PES's TeamSlice + 0x4C00, which has no GF
// equivalent); 0 means no tier is known.
double AddedTimeUnits(const PesClockConstants& c, e_AddedTimePhase phase, float elapsedSeconds,
                      int tier);
uint32_t AddedTimeTicks(const PesClockConstants& c, e_AddedTimePhase phase, float elapsedSeconds,
                        int tier);
// The fourth official's board: whole game-minutes of added time, never zero
// once any time at all was lost, capped at the nine the HUD shows (report 3.1).
int AnnouncedAddedMinutes(const PesClockConstants& c, e_AddedTimePhase phase, float elapsedSeconds,
                          int tier);

// The match clock, as PES computes and displays it.
class PesClock {
 public:
  explicit PesClock(const PesClockConstants& constants);

  // Which period the clock is in. One clock runs over the whole match, so
  // switching periods never rewinds it; the mark records where this period
  // began, so a period end is judged from its own kick-off.
  void SetPhase(e_MatchPhase phase) {
    this->phase = phase;
    periodStartSeconds = elapsedSeconds;
  }
  e_MatchPhase Phase() const { return phase; }

  // Elapsed game-seconds in this call, advanced on PES's own logic tick (one
  // 1/54-second step, report 2.5). GF's tick is 10 ms; see pesclock.cpp for
  // what that mapping assumes.
  void Advance(float gameSeconds);
  // Freezes the clock for the duration of a stoppage (report 3.3).
  void SetFrozen(bool frozen) { this->frozen = frozen; }
  bool IsFrozen() const { return frozen; }
  float ElapsedSeconds() const { return elapsedSeconds; }
  void SetElapsedSeconds(float seconds) { elapsedSeconds = seconds; }

  // Clock ticks: FUN_140922DD0's floor term, `sec * 16000` clock units at 1000
  // units per tick = 16 ticks per game-second.
  uint32_t Ticks() const;
  // The stored percentage form.
  float Percentage() const;
  // The scoreboard minute and second.
  void Display(int* minute_db_pitch, int* second) const;
  // How far the running period has come towards its own end, measured from when
  // that period started: the clock is one counter over the whole match, so the
  // period's share of it is what a period end is judged against.
  float PeriodFraction() const;
  bool IsPeriodDue() const;
  // Elapsed game-seconds inside the running period.
  float PeriodElapsedSeconds() const;

  // Added time for the end of the running period, in ticks and announced
  // game-minutes, and the referee's actual whistle against it.
  uint32_t AddedTicks(int tier) const;
  // The board's whole game-minutes, clamped to the nine the HUD shows.
  int AddedMinutes(int tier) const;
  // The same without that clamp, for the whistle itself.
  int AddedMinutesUnclamped(int tier) const;
  // The referee's actual end of the period: its own length plus the added time.
  float PeriodEndWithAdded(int tier) const;

 private:
  e_AddedTimePhase AddedPhase() const;
  PesClockConstants constants;
  e_MatchPhase phase = e_MatchPhase_PreMatch;
  float elapsedSeconds = 0.0f;
  // The clock's reading when the current period started.
  float periodStartSeconds = 0.0f;
  // The clock's tick count, kept exact so sub-tick advances do not truncate to
  // zero (a 1/54-second logic tick is 0.296 of a clock tick).
  double ticks = 0.0;
  bool frozen = false;
};

}  // namespace pes

#endif

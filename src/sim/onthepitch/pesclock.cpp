#include "onthepitch/pesclock.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pes {

namespace {

template <typename T>
T Load(const uint8_t* p) {
  T v;
  std::memcpy(&v, p, sizeof v);
  return v;
}

}  // namespace

bool ReadFloat(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, sizeof(float), reinterpret_cast<uint8_t*>(out));
}

std::optional<PesClockConstants> LoadPesClockConstants(const PeImage& exe, std::string* error) {
  PesClockConstants c;
  bool ok = true;
  ok &= ReadFloat(exe, GAME_SPEED_VA, &c.gameSpeed);
  ok &= ReadFloat(exe, HALF_SECONDS_VA, &c.halfSeconds);
  ok &= ReadFloat(exe, ADDED_2ND_HALF_VA, &c.added2ndHalf);
  ok &= ReadFloat(exe, ADDED_TIER_3_6_2ND_VA, &c.addedTier_3_6_2nd);
  ok &= ReadFloat(exe, ADDED_TIER_1_2_2ND_VA, &c.addedTier_1_2_2nd);
  ok &= ReadFloat(exe, ADDED_EX_TIME_VA, &c.addedExtraTime);
  ok &= ReadFloat(exe, ADDED_TIER_1_2_ET_VA, &c.addedTier_1_2_ET);
  ok &= ReadFloat(exe, ADDED_TIER_3_6_ET_VA, &c.addedTier_3_6_ET);
  if (!ok) {
    *error = "PES2021.exe: match clock constants unmapped";
    return std::nullopt;
  }
  c.ticksPerMinute = kTicksPerGameMinute;
  return c;
}

int PeriodEndMinutes(e_MatchPhase phase) {
  // Report 2.1: the same 45 / 90 / 105 / 120 table in all three copies
  // (0x1420BE050, 0x1420BE140, 0x1420BE0D0).
  switch (phase) {
    case e_MatchPhase_1stHalf:      // MATCH_PHASE_1ST_HALF
      return 45;
    case e_MatchPhase_2ndHalf:      // MATCH_PHASE_2ND_HALF
      return 90;
    case e_MatchPhase_1stExtraTime: // MATCH_PHASE_EX_1ST
      return 105;
    case e_MatchPhase_2ndExtraTime: // MATCH_PHASE_EX_2ND
      return 120;
    default:
      // Pre-match and the shoot-out have no clock (the tables' default 0).
      return 0;
  }
}

uint32_t PeriodEndTicks(e_MatchPhase phase) {
  // minutes/2 x 1920 ticks, so a first half is 43,200 and full time 86,400
  // (report 2.2).
  return uint32_t(PeriodEndMinutes(phase)) * kPercentageUnitTicks / 2;
}

int HalfLengthMinutes(e_MatchPhase phase) {
  switch (phase) {
    case e_MatchPhase_1stHalf:
    case e_MatchPhase_2ndHalf:
      return 45;
    case e_MatchPhase_1stExtraTime:
    case e_MatchPhase_2ndExtraTime:
      return 15;  // 105-90 and 120-105
    default:
      return 0;
  }
}

float TicksPerGameSecond() {
  // 960 ticks per game-minute over 60, which is exactly the 16000 clock units
  // per match second at 1000 units per tick (report 2.2/2.3).
  return static_cast<float>(kTicksPerGameMinute) / 60.0f;
}

float PercentageFromTicks(uint32_t ticks, e_MatchPhase phase) {
  // FUN_140922DD0: pct = clampToTicks * 10000 / (minutes*1920), with the half
  // length floored at 20 minutes (FUN_140A581C0). Every period end therefore
  // reads 5000: a first half's 43,200 ticks at 90 game-minutes, or 86,400 at the
  // same denominator, both scale against the whole match.
  const int minutes = std::max(PeriodEndMinutes(phase), 20);
  const double denom = static_cast<double>(minutes) * kPercentageUnitTicks;
  return static_cast<float>(std::min(static_cast<double>(ticks) * 10000.0 / denom, 5000.0));
}

uint32_t TicksFromPercentage(float percentage, e_MatchPhase phase) {
  const int minutes = std::max(PeriodEndMinutes(phase), 20);
  const double denom = static_cast<double>(minutes) * kPercentageUnitTicks;
  return static_cast<uint32_t>(std::floor(static_cast<double>(percentage) * denom / 10000.0));
}

void ClockDisplay(float totalSeconds, int* minute_db_pitch, int* second) {
  // FUN_140A6BD80: h = total/3600, m = (total/60)%60, and the HUD block's
  // `if minute >= 100: minute = 0`.
  const int total = static_cast<int>(std::floor(totalSeconds));
  int minute = (total / 60) % 60;
  if (minute >= kDisplayMinuteWrap) minute = 0;
  if (minute_db_pitch) *minute_db_pitch = minute;
  if (second) *second = total % 60;
}

double AddedTimeUnits(const PesClockConstants& c, e_AddedTimePhase phase, float elapsedSeconds,
                      int tier) {
  // Report 3.2: the tier bonus only. The 9000.0 / 5400.0 per-second term is the
  // clock's own value at the end of the period, which this engine's clock
  // already holds.
  if (phase == e_AddedTimePhase_2ndHalf) {
    if (tier >= 3 && tier <= 6)
      return static_cast<double>(elapsedSeconds) * c.addedTier_3_6_2nd;
    if (tier >= 1 && tier <= 2)
      return static_cast<double>(elapsedSeconds) * c.addedTier_1_2_2nd;
    return 0.0;
  }
  if (tier >= 1 && tier <= 2) return static_cast<double>(elapsedSeconds) * c.addedTier_1_2_ET;
  if (tier >= 3 && tier <= 6) return static_cast<double>(elapsedSeconds) * c.addedTier_3_6_ET;
  return 0.0;
}

uint32_t AddedTimeTicks(const PesClockConstants& c, e_AddedTimePhase phase, float elapsedSeconds,
                        int tier) {
  return static_cast<uint32_t>(AddedTimeUnits(c, phase, elapsedSeconds, tier) / kClockUnitsPerTick);
}

int AnnouncedAddedMinutes(const PesClockConstants& c, e_AddedTimePhase phase, float elapsedSeconds,
                          int tier) {
  const uint32_t ticks = AddedTimeTicks(c, phase, elapsedSeconds, tier);
  if (ticks == 0) return 0;
  const float minutes = static_cast<float>(ticks) / static_cast<float>(kTicksPerGameMinute);
  const int announced = static_cast<int>(std::ceil(minutes));
  return std::min(announced, kMaxAnnouncedAddedMinutes);  // report 3.1: at most 9
}

PesClock::PesClock(const PesClockConstants& constants) : constants(constants) {}

void PesClock::Advance(float gameSeconds) {
  if (frozen) return;  // report 3.3: a frozen state stops the clock
  elapsedSeconds += gameSeconds;
  // Advance the clock here rather than deriving it from the seconds, so a
  // frozen stoppage costs nothing and a partial tick is never lost.
  ticks += gameSeconds * TicksPerGameSecond();
}

uint32_t PesClock::Ticks() const {
  // FUN_140922DD0's floor term: sec * 16000 clock units per match second, at
  // 1000 units per tick - 16 ticks per game-second, 43,200 per 45 minutes.
  return static_cast<uint32_t>(ticks);
}

float PesClock::Percentage() const { return PercentageFromTicks(Ticks(), phase); }

e_AddedTimePhase PesClock::AddedPhase() const {
  return (phase == e_MatchPhase_1stExtraTime || phase == e_MatchPhase_2ndExtraTime)
             ? e_AddedTimePhase_ExtraTime
             : e_AddedTimePhase_2ndHalf;
}

uint32_t PesClock::AddedTicks(int tier) const {
  return AddedTimeTicks(constants, AddedPhase(), elapsedSeconds, tier);
}

int PesClock::AddedMinutes(int tier) const {
  // Report 3.1: blackboard shows at most nine minutes of added time.
  return std::min(AddedMinutesUnclamped(tier), kMaxAnnouncedAddedMinutes);
}

int PesClock::AddedMinutesUnclamped(int tier) const {
  const uint32_t ticks = AddedTicks(tier);
  return static_cast<int>(static_cast<float>(ticks) / static_cast<float>(kTicksPerGameMinute));
}

float PesClock::PeriodEndWithAdded(int tier) const {
  const int periodMinutes = HalfLengthMinutes(phase);
  if (periodMinutes == 0) return 0.0f;
  // The whistle uses the formula's own number, not the clamped blackboard.
  return periodMinutes + AddedMinutesUnclamped(tier);
}

void PesClock::Display(int* minute_db_pitch, int* second) const {
  ClockDisplay(elapsedSeconds, minute_db_pitch, second);
}

float PesClock::PeriodElapsedSeconds() const {
  return elapsedSeconds - periodStartSeconds;
}

float PesClock::PeriodFraction() const {
  const int periodMinutes = HalfLengthMinutes(phase);
  if (periodMinutes <= 0) return 0.0f;
  return std::clamp(PeriodElapsedSeconds() / (static_cast<float>(periodMinutes) * 60.0f), 0.0f,
                    1.0f);
}

bool PesClock::IsPeriodDue() const {
  const int periodMinutes = HalfLengthMinutes(phase);
  if (periodMinutes <= 0) return false;
  return PeriodElapsedSeconds() >= static_cast<float>(periodMinutes) * 60.0f;
}

}  // namespace pes

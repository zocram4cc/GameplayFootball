#pragma once

// PES 2021's stamina, fatigue and injury model, docs/re/pes-re-stamina-drain.md.
//
// A player carries three values: a tank `cur` under a `max`, a short-term dash
// reserve `breath` (B), and an injury accumulation. They are integers, they
// live per player, and they change only here.
//
// Every constant comes from the user's own PES2021.exe through
// pes::PeImage::ReadVa - the .rdata floats and the integer weights that live
// in instruction immediates (each immediate's opcode bytes are checked before
// the value is trusted). Nothing here is transcribed, so a match without the
// install cannot be built and MatchSetup requires the constants, exactly like
// the clock's and the ball's.
//
// GF's tick is 10 ms (Match::kTick_ms) where PES's is 1/54 s. PES's per-tick
// drain is an integer rate over game time, so one GF tick accumulates the same
// rate per second of game time: R_GF = R_PES * 54 * 0.01. The breath update is
// an integer delta per PES tick, scaled the same way, so a full breath refills
// in 7.1 s of game time on either clock.

#include <optional>
#include <string>

#include "pes/exe.hpp"
#include "pesrandom.hpp"

namespace pes {

// The speed classes PES's drain reads off the motion object (report 3.3,
// FUN_140a75be0): 0 if the player is not moving on his own, else the speed in
// km/h, banded 0.05 / 8 / 13.5 / 22.
inline constexpr int kSpeedClassCount = 5;
// GF's tick (Match::kTick_ms) against PES's (1/54 s): one GF tick carries
// kGfTick_s / kPesTick_s = 0.54 of a PES tick's rate (pesballphysics.hpp's
// kGfTick_s / kPesTick_s rule).
inline constexpr float kGfTick_s = 0.01f;
inline constexpr float kPesTick_s = 1.0f / 54.0f;

struct StaminaConstants {
  // The tank (report 2).
  int abilityFloor;             // 5
  int abilityScale70_79;        // 110
  int abilityScale80_89;        // 120
  int abilityScale90_94;        // 130
  int abilityScale95_99;        // 140
  int abilityScaleDivisor;      // 100
  int abilityScaleUnit;         // 400: (s' & 0xff) * 400
  float arrowTerriblePenalty;   // -8000
  float arrowPoorPenalty;       // -4000
  float arrowGreatBonus;        // 2000
  float arrowTopBonus;          // 6000
  float fitnessBonus;           // 8000: T == 0
  float tankFloor;              // 16000 * F
  int freshnessFloor;           // 10: the freshness %, floored
  // The per-tick drain (report 3.3).
  float drainScale_tick;  // 60: R * 60 / gs
  int drainPerSpeed[kSpeedClassCount];  // {-M + P} = {+19, +17, +9, -5, -9}
  int drainBreathFullBonus;    // 5: the breath term's scale in R
  int drainNoBallControlCost;  // 2: -2 * (F20 + F13)
  int drainAiFlagCost;         // 3: -3 * F14
  int drainEmptyFloor;         // 10: cur < 10 -> 0
  float speedClassThresholdWalk_kmh;   // 0.05
  float speedClassThresholdJog_kmh;    // 8
  float speedClassThresholdRun_kmh;    // 13.5
  float speedClassThresholdDash_kmh;   // 22
  float gameSpeedIntDivisor;           // gsInt, 54
  // The breath reserve (report 3.2).
  float breathFrameRate;        // 48: r = 48 / gs
  float breathBaseTicks;        // 6: d = (int)(6*r)
  float breathDashCostTicks;    // 9: -9*r
  float breathBallHolderCost;   // 2: -2*r
  float breathDashSpeed_kmh;    // 22
  float breathMinSpeed_kmh;      // 2.5: the velocity floor
  float breathKmhPerMs;         // 3.6
  int breathCapScale;           // 1920
  int breathCapFloor_pct;       // 20: cap = max(pct, 20) * 1920 / 100
  int breathCapDivisor;         // 100
  // The half-time and extra-time break (report 3.4).
  float halfTimeBonus;     // 9000
  float halfTimeT12Bonus;  // 3600
  float halfTimeT36Bonus;  // 7200
  float extraTimeBonus;    // 5400
  float extraTimeT12Bonus;  // 1800
  // The fatigue effect on stats (report 4).
  float a5FloorPct;               // 10
  float a5FullPct;                // 70
  float a5SpanPct;                // 60
  float a5Base;                   // 0.8
  float a5Scale;                  // 0.2
  float breathPenaltyScale;       // 10
  float breathPenaltyFullPct;     // 20
  float breathPenaltyDivisor;     // -20
  // The reserve's top-speed cap, FUN_1406f9a2c (dash-reserve report 3.1):
  // rate = base(G) - drop * (1 - q^2), q from the Speed stat less the reserve
  // penalty, then the group-80 rate -> m/s table inverted.
  float topRateGLo;        // 0.9166: the physique ramp's floor
  float topRateGHi;        // 1.0833: and its top
  float topRateGDrop;      // 0.085: base = 1 - 0.085 * u
  float topRateStatFloor;  // 20: q = 0 at or below
  float topRateStatCeil;   // 99: q = 1 at or above
  float topRateStatSpan;   // 79
  float topRateStatDrop;   // 0.1
  static constexpr int kTopRateMaxKnots = 16;
  int topRateKnots;        // 10
  float topRateSpeed_mps[kTopRateMaxKnots];
  float topRateValue[kTopRateMaxKnots];
  // The injury roll (report 6).
  float injurySpeedScale_kmh;      // 25
  float injurySpeedFloor;          // 0.7
  float injurySpeedSpan;           // 0.3
  float injuryBaseDown;            // 100
  float injuryBaseUp;              // 50
  float injuryAttackerSlide;       // 0.8
  float injuryAttackerSlideFromRun;  // 0.5
  float injurySlowFactor;          // 0.2
  float injuryAngleDivisor;        // 180
  float injuryHalf;                // 0.5: the angle ramp's span
  float injuryCap;                 // 255
  float injuryJackpotSev;          // 85
  float injuryKnockAcc;            // 150
  float injuryInjuredAcc;          // 200
  int injuryJackpotSevCap;         // 200
  int injuryJackpotThr[3];         // {5, 2, 0}: C, B, A
  int injuryResistanceThr[3];      // {30, 50, 70}: C, B, A
  int injuryRngSlot;               // 2: Range(rng, M, 2)
  int injuryRngModulus;            // 100
};

std::optional<StaminaConstants> LoadStaminaConstants(const PeImage& exe, std::string* error);

// One player's stamina state, all integers like PES's.
struct StaminaState {
  int max = 0;
  int cur = 0;
  int breath = 0;
  // Injury severity: this contact's severity, and the running total.
  int lastSeverity = 0;
  int accumulation = 0;
  // 0 clear, 2 knock (plays on), 3 injured.
  int injuryLevel = 0;
};

// Kick-off: the tank, its starting value and the breath reserve (report 2).
// `staminaAbility` is PES's Stamina attribute 40..99, `conditionArrow` 0
// Terrible..4 Top, `fitness` the per-slot value PES calls T (GF has no such
// marker, so a rested 0 - see the ponytail note in pesstamina.cpp), and
// `freshnessPct` the start-of-match percentage, floored at 10.
void InitStamina(const StaminaConstants& k, int staminaAbility, int conditionArrow, int fitness,
                 float matchLengthFactor, int freshnessPct, StaminaState* s);

// The breath cap: max(pct, 20) * 1920 / 100, where pct is the tank percentage.
int BreathCap(const StaminaConstants& k, const StaminaState& s);

// The reserve as the percentage of its cap that every PES reader takes
// (FUN_140a581c0, docs/re/pes-re-dash-reserve.md 2); `breath` itself is raw.
int BreathPct(const StaminaConstants& k, const StaminaState& s);

// The breath reserve update (report 3.2). `speed_kmh` is the 3-D speed, which
// is not the same speed the drain's class reads.
void UpdateBreath(const StaminaConstants& k, const StaminaState& s, float speed_kmh,
                  bool isBallHolder, int* breath);

// The speed class of the drain (report 3.3).
int SpeedClass(const StaminaConstants& k, float speed_kmh);

// One in-play tick of the drain (report 3.3), scaled to GF's tick.
// `breathPct` is the percentage AFTER this tick's breath update; `sideHasBall`
// is PES's F20 (this player's side controls the ball). Returns the new `cur`.
int DrainTick(const StaminaConstants& k, const StaminaState& s, int speedClass, int breathPct,
              bool sideHasBall);

// The fatigue effect on stats (report 4): the factor the ten A5 cells carry.
float FatigueFactor(const StaminaConstants& k, const StaminaState& s);

// The breath penalty on the four breath-sensitive stats (report 4): integer
// points subtracted from the live stat byte.
int BreathPenalty(const StaminaConstants& k, const StaminaState& s);

// The share of his rested top speed a player keeps at this reserve: 1 at or
// above 20 %, falling to the speed a 20-point slower player would have when
// it is empty (dash-reserve report 3.1). `speedStat` is GF's 0..1 Speed.
// ponytail: PES's physique scale G is taken as 1 - GF has no per-player G;
// it only moves the base rate, and the factor is a ratio of two rates.
float ReserveTopSpeedFactor(const StaminaConstants& k, const StaminaState& s, float speedStat);

// The half-time / extra-time break bonus (report 3.4), applied once at the
// whistle. `period` is PES's MI+0x1168: 2 half time, 4 the extra-time break.
// `matchLengthFactor` is F, the match-length scale the break bonuses are
// multiplied by (9000 * F, live 90000 at F = 10).
int BreakBonus(const StaminaConstants& k, const StaminaState& s, int period, int fitness,
               float matchLengthFactor);

// The severity of one contact (report 6.1). `victimSpeed_kmh`,
// `attackerSpeed_kmh`, `approachAngle_deg` and the attacker-state flags are
// PES's own; `rng` is the injury object's generator.
int ContactSeverity(const StaminaConstants& k, int resistance, bool victimDown,
                    float victimSpeed_kmh, float attackerSpeed_kmh, bool attackerSlid,
                    bool attackerSlidFromRun, float approachAngle_deg, Random* rng);

// One recorded contact: severity, accumulation, and the state the player is
// left in (report 6.2/6.3). Returns the severity used (the jackpot's 200).
int RecordContact(const StaminaConstants& k, StaminaState* s, int resistance, bool victimDown,
                  float victimSpeed_kmh, float attackerSpeed_kmh, bool attackerSlid,
                  bool attackerSlidFromRun, float approachAngle_deg, Random* rng);

}  // namespace pes
